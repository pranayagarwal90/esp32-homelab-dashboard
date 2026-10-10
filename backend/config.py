"""Backend configuration: the only module that reads the environment.

Sources, highest priority first:
  1. the process environment (systemd Environment=, shell exports);
  2. an optional KEY=value file, by default <project root>/.env. Set
     DASHBOARD_ENV_FILE to another path, or to blank for no file. A missing
     file is normal;
  3. the generic defaults below. None of them point at a particular network,
     location or machine.

Blank values: a blank core setting (port, timezone, ...) means "use the
default"; a blank optional integration (weather, host metrics, a service
health URL, the primary AI engine) means "disabled".

Invalid core settings stop startup with ConfigError. An invalid optional
integration is disabled and reported in Config.warnings; it never silently
falls back to a guessed value.

The .env format is deliberately small: KEY=value per line; blank lines and
lines starting with # are ignored; a value may be wrapped in matching single
or double quotes (taken literally, no escapes); in an unquoted value,
whitespace followed by # starts a comment. Nothing is expanded or executed.
"""
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlsplit
from zoneinfo import ZoneInfo
import os
import re

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ENV_FILE = PROJECT_ROOT / ".env"

DEFAULT_BIND = "0.0.0.0"
DEFAULT_PORT = 8090
DEFAULT_TIMEZONE = "UTC"
DEFAULT_HOST_LABEL = "Server"
# Relative paths are resolved against PROJECT_ROOT (where .env lives), never
# against the process working directory.
DEFAULT_PHOTOS_DIR = "./photos-ready"

DEFAULT_AI_PRIMARY_MODEL = "qwen2.5:7b"
DEFAULT_AI_FALLBACK_URL = "http://127.0.0.1:11434"
DEFAULT_AI_FALLBACK_MODEL = "llama3.2:3b"

# Status keys reserved by the API; a world clock may not reuse them.
RESERVED_CLOCK_KEYS = {"local"}

_KEY = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_SERVICE_KEY = re.compile(r"SERVICE_([A-Z0-9_]+)_HEALTH_URL")
_TRUE = {"true", "1", "yes", "on"}
_FALSE = {"false", "0", "no", "off"}


class ConfigError(Exception):
    """A setting is invalid; the backend must not start."""


# --- .env file ------------------------------------------------------------------------------

def parse_env_text(text, origin=".env"):
    """{KEY: value} from KEY=value lines. Malformed lines raise ConfigError."""
    values = {}
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        key = key.strip()
        if not separator or not _KEY.fullmatch(key):
            raise ConfigError(f"{origin}:{number}: expected KEY=value")
        value = value.strip()
        if len(value) >= 2 and value[0] in "\"'" and value[-1] == value[0]:
            value = value[1:-1]
        elif value[:1] in ("\"", "'"):
            raise ConfigError(f"{origin}:{number}: unterminated quote in {key}")
        else:
            value = re.split(r"\s+#", value, maxsplit=1)[0].rstrip()
        values[key] = value
    return values


def read_env_file(path):
    """The file's values, or {} when it does not exist."""
    try:
        text = Path(path).read_text(encoding="utf-8")
    except FileNotFoundError:
        return {}
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"cannot read {path}: {error}") from error
    return parse_env_text(text, str(path))


class Source:
    """Merged lookup: the process environment wins over the file."""

    def __init__(self, environ, file_values):
        self.environ, self.file_values = environ, file_values

    def raw(self, name):
        """The value, or None when the variable is set nowhere."""
        if name in self.environ:
            return self.environ[name]
        return self.file_values.get(name)

    def names(self):
        return set(self.file_values) | set(self.environ)


# --- Typed getters ----------------------------------------------------------------------------
# Core settings: absent or blank -> default; invalid -> ConfigError.

def get_string(source, name, default):
    value = (source.raw(name) or "").strip()
    return value or default


def get_int(source, name, default, minimum=None, maximum=None):
    text = get_string(source, name, "")
    if not text:
        return default
    try:
        value = int(text)
    except ValueError:
        raise ConfigError(f"{name}={text!r}: expected a whole number") from None
    if (minimum is not None and value < minimum) or (maximum is not None and value > maximum):
        raise ConfigError(f"{name}={value}: expected {minimum}-{maximum}")
    return value


def get_bool(source, name, default):
    text = get_string(source, name, "").lower()
    if not text:
        return default
    if text in _TRUE:
        return True
    if text in _FALSE:
        return False
    raise ConfigError(f"{name}={text!r}: expected true/false, 1/0, yes/no or on/off")


def get_float(source, name, default, minimum=None, maximum=None):
    value = get_optional_float(source, name, minimum, maximum)
    return default if value is None else value


def get_optional_float(source, name, minimum=None, maximum=None):
    """None when absent or blank; ConfigError when not a finite number in range."""
    text = get_string(source, name, "")
    if not text:
        return None
    try:
        value = float(text)
    except ValueError:
        raise ConfigError(f"{name}={text!r}: expected a number") from None
    if value != value or value in (float("inf"), float("-inf")):
        raise ConfigError(f"{name}={text!r}: expected a finite number")
    if (minimum is not None and value < minimum) or (maximum is not None and value > maximum):
        raise ConfigError(f"{name}={text}: expected {minimum} to {maximum}")
    return value


def valid_timezone(zone):
    """True for an IANA name zoneinfo can load (ZoneInfoNotFoundError is a
    KeyError; malformed keys raise ValueError; "America" is a directory)."""
    try:
        ZoneInfo(zone)
    except (KeyError, ValueError, OSError):
        return False
    return True


def get_timezone(source, name, default):
    zone = get_string(source, name, default)
    if not valid_timezone(zone):
        raise ConfigError(f"{name}={zone!r}: unknown IANA timezone")
    return zone


def http_url_problem(url):
    """Why url is unusable as a plain http(s) endpoint, or None."""
    try:
        parts = urlsplit(url)
        parts.port  # Raises ValueError for a bad port.
    except ValueError as error:
        return str(error)
    if parts.scheme not in {"http", "https"}:
        return "expected an http:// or https:// URL"
    if not parts.hostname:
        return "missing host"
    if parts.username is not None or parts.password is not None:
        return "credentials in URLs are not supported"
    if parts.fragment:
        return "fragments are not supported"
    return None


# --- Settings ---------------------------------------------------------------------------------

@dataclass(frozen=True)
class WorldClock:
    key: str    # /api/status timezones key, from the label ("New York" -> "new_york").
    label: str
    zone: str


@dataclass(frozen=True)
class AiConfig:
    primary_url: str      # "" = no primary engine.
    primary_model: str
    fallback_url: str
    fallback_model: str


@dataclass(frozen=True)
class Config:
    bind: str = DEFAULT_BIND
    port: int = DEFAULT_PORT
    timezone: str = DEFAULT_TIMEZONE
    host_label: str = DEFAULT_HOST_LABEL
    photos_dir: Path = PROJECT_ROOT / "photos-ready"
    world_clocks: tuple = ()
    weather_location: tuple = None          # (latitude, longitude), or None = disabled.
    host_metrics_url: str = ""              # "" = disabled.
    docker_status_enabled: bool = True
    service_urls: dict = field(default_factory=dict)  # Lowercase name -> URL; configured only.
    ai: AiConfig = AiConfig("", DEFAULT_AI_PRIMARY_MODEL, DEFAULT_AI_FALLBACK_URL, DEFAULT_AI_FALLBACK_MODEL)
    env_file: Path = None                   # The file that was read, if any.
    warnings: tuple = ()


def parse_world_clocks(text, name="DASHBOARD_WORLD_CLOCKS"):
    """'London=Europe/London,Tokyo=Asia/Tokyo' -> WorldClock tuple; blank -> ()."""
    text = text.strip()
    if not text:
        return ()
    clocks, keys = [], set()
    for item in text.split(","):
        label, separator, zone = (part.strip() for part in item.partition("="))
        if not separator or not label or not zone:
            raise ConfigError(f"{name}: {item.strip()!r}: expected Label=Area/City")
        if len(label) > 24 or not re.fullmatch(r"[A-Za-z0-9 ._-]+", label):
            raise ConfigError(f"{name}: label {label!r}: use up to 24 letters, digits, spaces, . _ -")
        key = re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")
        if not key or key in RESERVED_CLOCK_KEYS or key in keys:
            raise ConfigError(f"{name}: label {label!r} is reserved or repeated")
        if not valid_timezone(zone):
            raise ConfigError(f"{name}: {zone!r}: unknown IANA timezone")
        keys.add(key)
        clocks.append(WorldClock(key, label, zone))
    return tuple(clocks)


def resolve_path(text):
    """Absolute paths as given (after ~ expansion); relative ones from PROJECT_ROOT."""
    path = Path(text).expanduser()
    return Path(os.path.normpath(path if path.is_absolute() else PROJECT_ROOT / path))


def _weather(source, warnings):
    try:
        latitude = get_optional_float(source, "WEATHER_LATITUDE", -90, 90)
        longitude = get_optional_float(source, "WEATHER_LONGITUDE", -180, 180)
    except ConfigError as error:
        warnings.append(f"weather disabled: {error}")
        return None
    if latitude is None and longitude is None:
        return None
    if latitude is None or longitude is None:
        warnings.append("weather disabled: set both WEATHER_LATITUDE and WEATHER_LONGITUDE")
        return None
    return latitude, longitude


def _optional_url(source, name, warnings, what):
    url = get_string(source, name, "")
    problem = url and http_url_problem(url)
    if problem:
        warnings.append(f"{what} disabled: {name}: {problem}")
        return ""
    return url


def _ai(source, warnings):
    def legacy(name, old_name, default):
        # The new name wins whenever it is set (even blank), then the old one.
        for key in (name, old_name):
            value = source.raw(key)
            if value is not None:
                return key, value.strip()
        return name, default

    primary_url = _optional_url(source, "AI_PRIMARY_OLLAMA_URL", warnings, "AI primary engine")
    fallback_name, fallback_url = legacy("AI_FALLBACK_OLLAMA_URL", "AI_OLLAMA_URL", DEFAULT_AI_FALLBACK_URL)
    problem = fallback_url and http_url_problem(fallback_url)
    if problem:
        warnings.append(f"AI fallback engine unavailable: {fallback_name}: {problem}")
        fallback_url = ""
    elif not fallback_url:
        warnings.append(f"AI fallback engine unavailable: {fallback_name} is blank")
    _, fallback_model = legacy("AI_FALLBACK_OLLAMA_MODEL", "AI_OLLAMA_MODEL", DEFAULT_AI_FALLBACK_MODEL)
    return AiConfig(
        primary_url=primary_url.rstrip("/"),
        primary_model=get_string(source, "AI_PRIMARY_OLLAMA_MODEL", DEFAULT_AI_PRIMARY_MODEL),
        fallback_url=fallback_url.rstrip("/"),
        fallback_model=fallback_model or DEFAULT_AI_FALLBACK_MODEL,
    )


def _services(source, warnings):
    urls = {}
    for name in sorted(source.names()):
        match = _SERVICE_KEY.fullmatch(name)
        if not match:
            continue
        url = get_string(source, name, "")
        if not url:
            continue  # Blank: not configured.
        problem = http_url_problem(url)
        if problem:
            # Kept, so the check fails and the service reports down rather
            # than quietly disappearing.
            warnings.append(f"{name}: {problem}; the service will report down")
        urls[match.group(1).lower()] = url
    return urls


def load(environ=None, env_file=None):
    """Build the Config. environ defaults to os.environ; env_file to
    $DASHBOARD_ENV_FILE or <project root>/.env (blank: no file)."""
    environ = os.environ if environ is None else environ
    if env_file is None:
        chosen = environ.get("DASHBOARD_ENV_FILE")
        env_file = DEFAULT_ENV_FILE if chosen is None else (chosen.strip() or None)
    path = resolve_path(env_file) if env_file else None
    file_values = read_env_file(path) if path else {}
    source = Source(environ, file_values)
    warnings = []

    bind = get_string(source, "DASHBOARD_BIND", DEFAULT_BIND)
    if re.search(r"\s", bind):
        raise ConfigError(f"DASHBOARD_BIND={bind!r}: expected an address or host name")

    return Config(
        bind=bind,
        port=get_int(source, "DASHBOARD_PORT", DEFAULT_PORT, 1, 65535),
        timezone=get_timezone(source, "DASHBOARD_TIMEZONE", DEFAULT_TIMEZONE),
        host_label=get_string(source, "DASHBOARD_HOST_LABEL", DEFAULT_HOST_LABEL),
        photos_dir=resolve_path(get_string(source, "DASHBOARD_PHOTOS_DIR", DEFAULT_PHOTOS_DIR)),
        world_clocks=parse_world_clocks(get_string(source, "DASHBOARD_WORLD_CLOCKS", "")),
        weather_location=_weather(source, warnings),
        host_metrics_url=_optional_url(source, "HOST_METRICS_URL", warnings, "host metrics"),
        docker_status_enabled=get_bool(source, "DOCKER_STATUS_ENABLED", True),
        service_urls=_services(source, warnings),
        ai=_ai(source, warnings),
        env_file=path if path and path.is_file() else None,
        warnings=tuple(warnings),
    )
