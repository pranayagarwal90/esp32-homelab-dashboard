"""The services the dashboard knows: one registry for config, server and AI.

Which ones are monitored is configuration (SERVICES in config.py); a
service's health URL is SERVICE_<ID>_HEALTH_URL. /api/status reports the
monitored ones in `services_list` as {id, label, status}, in registry order;
the firmware renders, alerts on and labels whatever that list holds, so the
registry (not the firmware) owns ids and labels. The legacy `services`
object (every id: true/false/null) is still sent for older firmware.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class ServiceDefinition:
    id: str
    label: str
    # Without a health URL, the service is up when a running Docker container
    # name contains one of these. Empty: no Docker identity (URL only).
    docker_names: tuple = ()

    @property
    def env(self):
        return f"SERVICE_{self.id.upper()}_HEALTH_URL"


SERVICE_DEFINITIONS = (
    ServiceDefinition("jellyfin", "Jellyfin", ("jellyfin",)),
    ServiceDefinition("navidrome", "Navidrome", ("navidrome",)),
    ServiceDefinition("metube", "MeTube", ("metube",)),
    ServiceDefinition("bazarr", "Bazarr", ("bazarr",)),
    ServiceDefinition("ollama", "Ollama", ("ollama",)),
    ServiceDefinition("cloudflare", "Cloudflare", ("cloudflared", "cloudflare")),
    ServiceDefinition("mcp", "MCP", ("mcp",)),
    ServiceDefinition("nextcloud", "Nextcloud"),
    ServiceDefinition("immich", "Immich"),
    ServiceDefinition("technical_blog", "Technical Blog"),
)
SERVICES_BY_ID = {service.id: service for service in SERVICE_DEFINITIONS}
SERVICE_IDS = tuple(SERVICES_BY_ID)

# Firmware limits (ServicesLogic.h): at most MAX_SERVICES entries, ids of
# [a-z0-9_-] and labels of printable ASCII, each at most 23 characters.
MAX_SERVICES = 16
SERVICE_ID_MAX = 23
SERVICE_LABEL_MAX = 23
