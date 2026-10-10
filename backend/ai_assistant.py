"""Read-only AI explanations of the homelab status, from local Ollama.

The ESP32 asks for one of five fixed modes and sends only its active alert
codes (the dashboard's alert rules, with their hysteresis, live in the
firmware). Everything else comes from the backend's own status. The model
only phrases a summary and one safe check; the title, the facts it may use and
the checks on its answer are deterministic, so the AI can never disagree with
the dashboard's HEALTHY / ATTENTION / CRITICAL state.

Nothing here executes anything: no commands, restarts or remote actions.
"""
import hashlib
import json
import re
import threading
import time
from datetime import datetime
from urllib.error import URLError
from urllib.request import Request, urlopen

# Two local Ollama engines (nothing leaves the network), configured in
# config.py (AiConfig):
#   primary  - an optional GPU machine on the LAN, used when reachable;
#              AI_PRIMARY_OLLAMA_URL (blank: no primary), AI_PRIMARY_OLLAMA_MODEL
#              (default qwen2.5:7b, fits an 8 GB GPU).
#   fallback - the backend host's own CPU Ollama; AI_FALLBACK_OLLAMA_URL /
#              AI_FALLBACK_OLLAMA_MODEL (default llama3.2:3b, the fastest small
#              CPU model that follows the format; AI_OLLAMA_URL / AI_OLLAMA_MODEL
#              still work).

HEALTH_TIMEOUT_SECONDS = 0.8   # /api/tags and /api/ps probes; never a generation.
HEALTH_SECONDS = 10            # Probe results are reused this long.
FAILURE_COOLDOWN_SECONDS = 30  # After a failed primary generation: straight to fallback.
# Generation timeouts, measured: laptop 0.2-0.6 s warm, ~15 s cold load;
# HomeServer 1.6-4 s warm, ~25 s cold load.
PRIMARY_TIMEOUT = (8, 20)      # (model loaded, cold)
FALLBACK_TIMEOUT = (15, 30)
TOTAL_BUDGET_SECONDS = 50      # Primary + fallback; the ESP32 waits 55 s.
KEEP_ALIVE = "30m"             # Ours; a model another client pinned stays pinned.
CACHE_SECONDS = 60
MAX_REQUEST_BYTES = 2048
# Options per engine; warm-up uses the same ones, since Ollama reloads a model
# whose num_ctx changes. 2048 on the laptop matches the blog's qwen2.5:7b.
BASE_OPTIONS = {"temperature": 0.2, "top_p": 0.9, "num_predict": 100, "seed": 42}
PRIMARY_OPTIONS = {**BASE_OPTIONS, "num_ctx": 2048}
FALLBACK_OPTIONS = {**BASE_OPTIONS, "num_ctx": 1024}

TITLE_MAX = 40
SUMMARY_MAX = 220
ACTION_MAX = 140

MODES = {
    "status": "Summarize the current overall state of the homelab.",
    "alerts": "Explain the active alerts and how important each one is.",
    "attention": "Say what currently deserves attention, highest priority first.",
    "action": "Suggest the single safest next troubleshooting check.",
    "summary": "Give a concise HomeServer health summary.",
}

# The dashboard's alertable services (firmware AlertLogic.h) and the services
# shown on its SERVICES page (firmware ServicesLogic.h), in display order.
ALERT_SERVICES = ("jellyfin", "navidrome", "metube", "ollama", "cloudflare")
SERVICE_LABELS = {
    "jellyfin": "Jellyfin", "navidrome": "Navidrome", "metube": "MeTube", "ollama": "Ollama",
    "cloudflare": "Cloudflare", "nextcloud": "Nextcloud", "immich": "Immich",
    "technical_blog": "Technical Blog",
}
ALERT_IDS = set(ALERT_SERVICES) | {"ram", "disk", "stale"}
LEVELS = ("critical", "warning")
MAX_ALERTS = 12

SYSTEM_PROMPT = """You write short status notes for a homelab dashboard screen (320x240).
Rules:
- Use ONLY the facts in the telemetry. It is a single snapshot: never describe trends, history or changes.
- PROBLEMS lists everything that is wrong. Never call anything else a problem, high, low or full.
- Never mention a service, metric or problem that is not in the telemetry.
- Items marked unknown are not problems.
- If PROBLEMS is none, say everything looks healthy and the action is none.
- Critical problems first, then warnings.
- Causes are uncertain: use may, could or check. Never claim a root cause.
- Suggest only one safe read-only check (look at status, logs or usage). Never suggest restarting, stopping, deleting, upgrading, adding or changing anything, and never give commands.
- Never say that you did anything.
- Plain text. No markdown, bullets, emojis, URLs or JSON.
Answer in exactly two lines:
SUMMARY: <one or two short sentences, at most 200 characters>
ACTION: <one short sentence, at most 120 characters, or none>"""


class RequestError(Exception):
    """Invalid /api/ai/explain request (HTTP 400)."""


# --- Request validation -----------------------------------------------------------------

def parse_request(body):
    """(mode, refresh, alerts) from the POST body. Only fixed modes, a refresh
    flag and dashboard alert codes are accepted: never prompt text."""
    if len(body) > MAX_REQUEST_BYTES:
        raise RequestError("request too large")
    try:
        data = json.loads(body or b"{}")
    except ValueError as error:
        raise RequestError("request is not JSON") from error
    if not isinstance(data, dict):
        raise RequestError("request must be an object")
    unknown = set(data) - {"mode", "refresh", "alerts"}
    if unknown:
        raise RequestError("unsupported field: " + sorted(unknown)[0])
    mode = data.get("mode")
    if not isinstance(mode, str) or mode not in MODES:
        raise RequestError("unsupported mode")
    refresh = data.get("refresh", False)
    if not isinstance(refresh, bool):
        raise RequestError("refresh must be a boolean")
    alerts = data.get("alerts")
    if alerts is not None:
        alerts = parse_alerts(alerts)
    return mode, refresh, alerts


def parse_alerts(items):
    """Dashboard alert codes: [{"id": "metube"|"ram"|"disk"|"stale"|...,
    "level": "warning"|"critical", "subject": "C:" (disk only)}]."""
    if not isinstance(items, list) or len(items) > MAX_ALERTS:
        raise RequestError("alerts must be a short list")
    alerts = []
    for item in items:
        if not isinstance(item, dict) or set(item) - {"id", "level", "subject"}:
            raise RequestError("bad alert")
        alert_id, level = item.get("id"), item.get("level")
        if alert_id not in ALERT_IDS or level not in LEVELS:
            raise RequestError("bad alert")
        subject = item.get("subject")
        if alert_id == "disk":
            if not isinstance(subject, str) or not re.fullmatch(r"[A-Za-z0-9]{1,3}:?", subject):
                raise RequestError("bad disk alert")
        elif subject is not None:
            raise RequestError("bad alert")
        alerts.append({"id": alert_id, "level": level, "subject": subject})
    return alerts


# --- Facts and context ------------------------------------------------------------------

def _percent(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return int(round(number)) if number == number else None


def _clean_name(value, limit=24):
    """Host or disk names from Windows: letters, digits and a few separators."""
    return re.sub(r"[^A-Za-z0-9 :._-]", "", str(value or ""))[:limit].strip()


def build_facts(status, alerts):
    """Deterministic facts from the backend status and the dashboard alerts."""
    host_ok = bool(status.get("host_available"))
    disks = {}
    for disk in status.get("disks") or []:
        name = _clean_name(disk.get("name"), 4)
        if name:
            disks[name.upper()] = _percent(disk.get("percent"))
    services = status.get("services") or {}
    hostname = str(status.get("hostname") or "")

    problems = []
    if alerts is not None:
        for alert in alerts:
            if alert["id"] in ALERT_SERVICES:
                text = f"{SERVICE_LABELS[alert['id']]} is offline"
                subject = SERVICE_LABELS[alert["id"]]
            elif alert["id"] == "ram":
                ram = _percent((status.get("memory") or {}).get("percent")) if host_ok else None
                text = f"RAM usage is {ram}%" if ram is not None else "RAM usage is high (current value unavailable)"
                subject = "RAM"
            elif alert["id"] == "disk":
                name = alert["subject"].upper()
                name = name if name.endswith(":") else name + ":"
                value = disks.get(name) if host_ok else None
                text = f"Disk {name} is {value}% full" if value is not None else f"Disk {name} is nearly full"
                subject = f"Disk {name}"
            else:
                text = "The dashboard missed several status updates"
                subject = "status"
            problems.append({"level": alert["level"], "text": text, "subject": subject, "id": alert["id"]})
        problems.sort(key=lambda p: LEVELS.index(p["level"]))

    online, offline, unknown = [], [], []
    for key, label in SERVICE_LABELS.items():
        value = services.get(key)
        (online if value is True else offline if value is False else unknown).append(label)
    # The dashboard's alert wins if the backend's own check disagrees (race).
    alerted = {p["subject"] for p in problems}
    online = [name for name in online if name not in alerted]
    return {
        # A Windows computer name, or the default: never free text.
        "hostname": hostname if re.fullmatch(r"[A-Za-z0-9-]{1,15}", hostname) else "HOMESERVER",
        "host_available": host_ok,
        "alerts_known": alerts is not None,
        "problems": problems,
        "online": online,
        # Offline services the dashboard does not alert on (shown red on SERVICES).
        "offline_other": [name for name in offline if name not in alerted],
        "unknown": unknown,
        "cpu": _percent((status.get("cpu") or {}).get("percent")) if host_ok else None,
        "ram": _percent((status.get("memory") or {}).get("percent")) if host_ok else None,
        "disks": disks if host_ok else {},
        "uptime_hours": status.get("uptime_hours") if host_ok else None,
    }


def counts(facts):
    critical = sum(p["level"] == "critical" for p in facts["problems"])
    return critical, len(facts["problems"]) - critical


def title_for(facts):
    """Same verdict as the dashboard: derived from its alerts only."""
    if not facts["alerts_known"]:
        return "Alert state not available"
    critical, warnings = counts(facts)
    if critical and warnings:
        return f"{critical} critical, {warnings} warning{'s' if warnings > 1 else ''}"
    if critical:
        return f"{critical} critical issue{'s' if critical > 1 else ''}"
    if warnings:
        return f"{warnings} item{'s need' if warnings > 1 else ' needs'} attention"
    return "All systems healthy" if facts["host_available"] else "No alerts, host metrics unavailable"


def build_context(facts):
    """Compact prompt telemetry. Numbers appear only for problems; healthy
    items are listed by name so the model cannot misjudge a value."""
    lines = [f"{facts['hostname']} telemetry snapshot"]
    critical, warnings = counts(facts)
    if not facts["alerts_known"]:
        lines.append("OVERALL: UNKNOWN (dashboard alerts not provided)")
        lines.append("PROBLEMS: cannot be determined")
    else:
        verdict = "CRITICAL" if critical else "ATTENTION" if warnings else "HEALTHY"
        lines.append(f"OVERALL: {verdict} ({critical} critical, {warnings} warnings)")
        if facts["problems"]:
            lines.append("PROBLEMS (dashboard alerts):")
            lines += [f"{p['level'].upper()}: {p['text']}" for p in facts["problems"]]
        else:
            lines.append("PROBLEMS (dashboard alerts): none")
    if facts["host_available"]:
        alerted = {p["id"] for p in facts["problems"]}
        ok = ["CPU"] + ([] if "ram" in alerted else ["RAM"])
        alerted_disks = {p["subject"] for p in facts["problems"] if p["id"] == "disk"}
        ok += [f"disk {name}" for name in facts["disks"] if f"Disk {name}" not in alerted_disks]
        lines.append("OK: " + ", ".join(ok))
    else:
        lines.append("NOT AVAILABLE: host metrics (CPU, RAM and disks cannot be determined)")
    if facts["online"]:
        lines.append("OK services: " + ", ".join(n for n in facts["online"]))
    if facts["offline_other"]:
        lines.append("OFFLINE, no dashboard alert: " + ", ".join(facts["offline_other"]))
    if facts["unknown"]:
        lines.append("UNKNOWN (not reported, not a problem): " + ", ".join(facts["unknown"]))
    return "\n".join(lines)


def fingerprint(mode, facts):
    """What makes two answers interchangeable: alerts, services and coarse
    host values (RAM and disks to 5%). CPU is left out: it is not in the
    prompt as a number, and generating an answer itself moves it."""
    coarse = lambda v, step: None if v is None else int(v // step)
    key = {
        "mode": mode,
        "alerts_known": facts["alerts_known"],
        "problems": [(p["level"], p["text"] if p["id"] not in ("ram", "disk") else p["subject"])
                     for p in facts["problems"]],
        "online": facts["online"], "offline": facts["offline_other"], "unknown": facts["unknown"],
        "host": facts["host_available"], "ram": coarse(facts["ram"], 5),
        "disks": {k: coarse(v, 5) for k, v in facts["disks"].items()},
    }
    return hashlib.sha256(json.dumps(key, sort_keys=True).encode()).hexdigest()


# --- Output cleanup and checks ----------------------------------------------------------

_ASCII = {"‘": "'", "’": "'", "“": '"', "”": '"', "–": "-", "—": "-",
          "…": "...", " ": " ", "•": " "}
_UNSAFE_ACTION = re.compile(
    r"`|\$\(|\bsudo\b|\bsystemctl\b|\bdocker\s+\w+|\bkubectl\b|\bpowershell\b|\brm\s|\bkill\b|"
    r"\brestart|\breboot|\bshut\s*down|\bstop\b|\bstopping\b|\bdelete|\bremov|\bupgrad|\binstall|"
    r"\bincreas|\badd(ing)?\s+more|\bexpand|\bfree\s+up|\bdisabl|\bformat\b|\breplac",
    re.IGNORECASE)
_NEGATED = re.compile(r"\b(no|not|without|nothing|none|zero)\b(\s+\w+){0,2}", re.IGNORECASE)
_PROBLEM_WORDS = re.compile(r"\b(high|low|full|critical|warning|elevated|offline|down|fail\w*|error\w*|outage)\b",
                            re.IGNORECASE)


def clean_text(text, limit):
    """Plain ASCII sentence(s): no markdown, URLs, emojis or line breaks,
    cut at a word boundary with '...' when too long."""
    text = "".join(_ASCII.get(ch, ch) for ch in str(text or ""))
    text = re.sub(r"https?://\S+|www\.\S+", "", text)
    text = re.sub(r"[*_#`>|]+", "", text)
    text = re.sub(r"^\s*(?:[-+]|\d+[.)])\s+", "", text)
    text = "".join(ch for ch in text if 32 <= ord(ch) < 127)
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) > limit:
        cut = text[: limit - 3].rsplit(" ", 1)[0].rstrip(" ,;:.-")
        text = cut + "..."
    return text


def parse_model_text(content):
    """(summary, action) from 'SUMMARY: ... / ACTION: ...'. Unlabelled text
    becomes the summary."""
    summary = action = None
    rest = []
    for raw in str(content or "").splitlines():
        line = re.sub(r"^[\s*_#>-]+", "", raw)
        match = re.match(r"(?i)(title|summary|action)\s*[:\-]\s*(.*)", line)
        if match:
            field, value = match.group(1).lower(), match.group(2).lstrip("*_ ")
            if field == "summary":
                summary = value
            elif field == "action":
                action = value
        elif line.strip():
            rest.append(line)
    if summary is None and rest:
        summary = " ".join(rest)
    return summary or "", action or ""


def safe_action(action):
    """Drops clauses that would change something or read as a command."""
    clauses = re.split(r"(?i),\s*|;\s*|\s+and\s+|\s+then\s+", action)
    kept = [c for c in clauses if c.strip() and not _UNSAFE_ACTION.search(c)]
    if not kept or len(" ".join(kept)) < 12:
        return ""
    text = ", then ".join(c.strip().rstrip(".") for c in kept) + "."
    return text[0].upper() + text[1:]


def rules_summary(facts):
    if not facts["alerts_known"]:
        return "The dashboard did not send its alerts, so problems cannot be determined."
    parts = [p["text"] + "." for p in facts["problems"]]
    if not parts:
        parts.append("No active alerts; monitored services are responding.")
    if facts["offline_other"]:
        parts.append(", ".join(facts["offline_other"]) + " is offline (no alert).")
    if not facts["host_available"]:
        parts.append("Host metrics are unavailable.")
    return clean_text(" ".join(parts), SUMMARY_MAX)


def rules_action(facts):
    if not facts["problems"]:
        return "Check that the Windows metrics agent is running." if not facts["host_available"] else ""
    top = facts["problems"][0]
    if top["id"] in ALERT_SERVICES:
        return f"Check the {SERVICE_LABELS[top['id']]} container status and logs."
    if top["id"] == "ram":
        return "Review which processes are using the most memory."
    if top["id"] == "disk":
        return f"Review what is using space on {top['subject'][5:]}."
    return "Check the dashboard's Wi-Fi and the HomeServer connection."


def mentions(problem, text):
    """Whether the summary talks about this problem."""
    words = {
        "ram": r"\b(ram|memory)\b",
        "disk": r"\b(disk|drive)\s+" + re.escape(problem["subject"][5:].rstrip(":").lower()) + r"\b",
        "stale": r"\b(status|update)",
    }.get(problem["id"], r"\b" + re.escape(problem["subject"].lower()) + r"\b")
    return re.search(words, text.lower()) is not None


def finalize(content, facts):
    """(summary, action, source): the model's text if it passes the checks,
    otherwise deterministic text built from the facts ("rules")."""
    summary, action = parse_model_text(content)
    summary = clean_text(summary, SUMMARY_MAX)
    action = clean_text(action, ACTION_MAX)
    if action.lower().rstrip(".") in ("none", "n/a", "no action needed", "no action", "nothing"):
        action = ""
    action = clean_text(safe_action(action), ACTION_MAX) if action else ""

    problems = facts["problems"]
    usable = len(summary) >= 12
    if usable and facts["alerts_known"] and not problems:
        # Healthy dashboard: the summary must not report problems.
        words = _PROBLEM_WORDS.findall(_NEGATED.sub(" ", summary))
        allowed = {"offline"} if facts["offline_other"] else set()
        usable = all(w.lower() in allowed for w in words)
        action = "" if usable else action
    elif usable and problems:
        # Must mention at least one real problem, and call something critical
        # only when the dashboard does.
        usable = any(mentions(p, summary) for p in problems)
        if usable and not any(p["level"] == "critical" for p in problems):
            usable = not re.search(r"\bcritical", _NEGATED.sub(" ", summary), re.IGNORECASE)
    if usable:
        # A service the telemetry shows online must not be called offline.
        for name in facts["online"]:
            if re.search(r"\b" + re.escape(name) + r"\b(?:\s+(?:is|are|was|also|currently|now|appears|seems|to|be))*"
                         r"\s+(offline|down|not responding|failing|unreachable)\b", summary, re.IGNORECASE):
                usable = False
    if not usable:
        return rules_summary(facts), rules_action(facts), "rules"
    if problems and not action:
        action = rules_action(facts)
    return summary, action, "ai"


# --- Ollama -----------------------------------------------------------------------------

class AiUnavailable(Exception):
    def __init__(self, message):
        super().__init__(message)
        self.message = message


def _post(url, payload, timeout):
    request = Request(url, json.dumps(payload).encode(), {"Content-Type": "application/json"})
    with urlopen(request, timeout=timeout) as response:
        return json.loads(response.read(65536))


def _get(url, timeout):
    with urlopen(url, timeout=timeout) as response:
        return json.loads(response.read(262144))


class Engine:
    """One Ollama endpoint: cheap health probes (cached), keep-alive policy and
    generation. Never exposes its URL outside the backend."""

    PINNED_SECONDS = 365 * 86400  # Loaded "forever" (keep_alive -1) by someone.

    def __init__(self, name, url, model, timeouts, options, clock=time.time, post=None, get=None):
        self.name, self.url, self.model = name, url, model
        self.timeouts, self.options, self.clock = timeouts, options, clock
        self.post, self.get = post or _post, get or _get
        self.lock = threading.Lock()  # One generation per engine at a time.
        self.checked = None           # (time, health)
        self.failed_until = 0
        self.warming = False
        self.last_warm = 0

    def health(self):
        """{"reachable", "installed", "loaded", "pinned"} from /api/tags and
        /api/ps, cached HEALTH_SECONDS. Never generates."""
        now = self.clock()
        if self.checked and now - self.checked[0] < HEALTH_SECONDS:
            return self.checked[1]
        health = {"reachable": False, "installed": False, "loaded": False, "pinned": False}
        try:
            tags = self.get(self.url + "/api/tags", HEALTH_TIMEOUT_SECONDS)
            health["reachable"] = True
            names = {m.get("name") for m in tags.get("models", []) if isinstance(m, dict)}
            health["installed"] = self.model in names or (":" not in self.model and self.model + ":latest" in names)
            if health["installed"]:
                for entry in self.get(self.url + "/api/ps", HEALTH_TIMEOUT_SECONDS).get("models", []):
                    if isinstance(entry, dict) and entry.get("name") == self.model:
                        health["loaded"] = True
                        health["pinned"] = _seconds_until(entry.get("expires_at"), now) > self.PINNED_SECONDS
        except Exception:
            pass
        self.checked = (now, health)
        return health

    def usable(self):
        health = self.health()
        return health["reachable"] and health["installed"] and self.clock() >= self.failed_until

    def mark_failed(self):
        """A failed generation: skip this engine for a while; afterwards the
        next probe decides (so a returning laptop is used again)."""
        self.failed_until = self.clock() + FAILURE_COOLDOWN_SECONDS
        self.checked = None

    def keep_alive(self):
        # Never shorten another client's permanent load (the blog pins its models).
        return -1 if self.health()["pinned"] else KEEP_ALIVE

    def timeout(self):
        return self.timeouts[0] if self.health()["loaded"] else self.timeouts[1]

    def warm_up(self):
        """Loads the model in the background (no generation), unless loaded."""
        now = self.clock()
        if self.warming or now - self.last_warm < 300 or self.health()["loaded"]:
            return False
        self.warming, self.last_warm = True, now
        payload = {"model": self.model, "keep_alive": self.keep_alive(), "options": self.options}

        def load():
            try:
                self.post(self.url + "/api/generate", payload, 60)
            except Exception:
                pass
            finally:
                self.warming = False
                self.checked = None

        threading.Thread(target=load, name=f"ai-warm-{self.name}", daemon=True).start()
        return True

    def generate(self, messages, timeout):
        """The model's text, or AiUnavailable with a user-facing reason."""
        started = self.clock()
        if not self.lock.acquire(timeout=timeout):
            raise AiUnavailable("AI is busy, try again")
        try:
            remaining = max(1.0, timeout - (self.clock() - started))
            payload = {"model": self.model, "stream": False, "keep_alive": self.keep_alive(),
                       "messages": messages, "options": self.options}
            try:
                reply = self.post(self.url + "/api/chat", payload, remaining)
            except (TimeoutError, OSError) as error:
                reason = getattr(error, "reason", error)
                if isinstance(error, TimeoutError) or isinstance(reason, TimeoutError) or "timed out" in str(error):
                    raise AiUnavailable("AI response timed out") from error
                if isinstance(error, URLError) and getattr(error, "code", None) == 404:
                    raise AiUnavailable("AI model is not installed") from error
                raise AiUnavailable("AI service is not responding") from error
            except ValueError as error:
                raise AiUnavailable("AI returned an unusable answer") from error
            message = reply.get("message") if isinstance(reply, dict) else None
            if not isinstance(message, dict) or not isinstance(message.get("content"), str):
                if isinstance(reply, dict) and "not found" in str(reply.get("error", "")):
                    raise AiUnavailable("AI model is not installed")
                raise AiUnavailable("AI returned an unusable answer")
            return message["content"]
        finally:
            self.lock.release()


def _seconds_until(stamp, now):
    """Seconds from now until an Ollama expires_at (RFC 3339, ns precision)."""
    try:
        text = re.sub(r"\.(\d{6})\d*", r".\1", str(stamp)).replace("Z", "+00:00")
        return datetime.fromisoformat(text).timestamp() - now
    except (TypeError, ValueError):
        return 0


def default_engines(ai, clock=time.time):
    """(primary or None, fallback) from a config.AiConfig."""
    primary = Engine("primary", ai.primary_url, ai.primary_model, PRIMARY_TIMEOUT, PRIMARY_OPTIONS, clock) \
        if ai.primary_url else None
    fallback = Engine("fallback", ai.fallback_url, ai.fallback_model, FALLBACK_TIMEOUT, FALLBACK_OPTIONS, clock)
    return primary, fallback


class AiAssistant:
    """Routes each question: the primary (GPU laptop) when it is reachable
    and has its model, otherwise, or after it fails, the fallback exactly
    once. The ESP32 gets one answer and only learns "primary" or "fallback"."""

    def __init__(self, status_source, primary, fallback, clock=time.time):
        self.status_source = status_source  # The backend's own get_status().
        self.primary, self.fallback = primary, fallback
        self.clock = clock
        self.cache = {}            # fingerprint -> (time, response)
        self.cache_lock = threading.Lock()

    # /api/ai/status

    def status(self):
        primary_ok = bool(self.primary and self.primary.usable())
        fallback_health = self.fallback.health()
        fallback_ok = fallback_health["reachable"] and fallback_health["installed"]
        preferred = "primary" if primary_ok else "fallback" if fallback_ok else None
        result = {"available": preferred is not None, "provider": "ollama", "preferred": preferred,
                  "primary_available": primary_ok, "fallback_available": fallback_ok}
        if preferred:
            # Load only the engine that will answer: no 2.3 GB CPU model on the
            # HomeServer while the GPU laptop is available.
            (self.primary if primary_ok else self.fallback).warm_up()
        elif not fallback_health["reachable"]:
            result["error"] = "AI service is not responding"
        else:
            result["error"] = "AI model is not installed"
        return result

    # /api/ai/explain

    parse_request = staticmethod(parse_request)

    def explain(self, mode, refresh=False, alerts=None):
        facts = build_facts(self.status_source(), alerts)
        key = fingerprint(mode, facts)
        now = self.clock()
        if not refresh:
            with self.cache_lock:
                hit = self.cache.get(key)
            if hit and now - hit[0] < CACHE_SECONDS:
                return {**hit[1], "cached": True}  # Keeps the engine that made it.

        try:
            engine, content = self._generate(mode, facts)
        except AiUnavailable as error:
            return {"available": False, "mode": mode, "error": error.message}
        summary, action, source = finalize(content, facts)
        response = {
            "available": True, "mode": mode, "title": clean_text(title_for(facts), TITLE_MAX),
            "summary": summary, "action": action, "source": source, "engine": engine,
            "generated_at": int(self.clock()), "cached": False,
        }
        with self.cache_lock:
            self.cache = {k: v for k, v in self.cache.items() if now - v[0] < CACHE_SECONDS}
            self.cache[key] = (now, response)
        return response

    def _generate(self, mode, facts):
        """(engine name, text). Primary first when usable; on any primary
        failure the fallback is tried exactly once. Output that fails the
        answer checks is not retried elsewhere (the rules handle it)."""
        messages = [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": build_context(facts) + "\n\nTask: " + MODES[mode]},
        ]
        started = self.clock()
        if self.primary and self.primary.usable():
            try:
                return "primary", self.primary.generate(messages, self.primary.timeout())
            except AiUnavailable:
                self.primary.mark_failed()
        health = self.fallback.health()
        if not health["reachable"]:
            raise AiUnavailable("AI service is not responding")
        if not health["installed"]:
            raise AiUnavailable("AI model is not installed")
        remaining = TOTAL_BUDGET_SECONDS - (self.clock() - started)
        timeout = max(5.0, min(self.fallback.timeout(), remaining))
        return "fallback", self.fallback.generate(messages, timeout)
