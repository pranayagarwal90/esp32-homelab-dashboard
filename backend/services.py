"""The services the dashboard knows: one registry for config, server and AI.

The ids are the /api/status `services` keys the firmware reads; order is the
order they appear in that object. Which ones are monitored is configuration
(SERVICES in config.py); a service's health URL is SERVICE_<ID>_HEALTH_URL.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class ServiceDefinition:
    id: str
    label: str
    # Without a health URL, the service is up when a running Docker container
    # name contains one of these. Empty: no Docker identity (URL only).
    docker_names: tuple = ()
    # Shown on the firmware's SERVICES page (and so in AI context).
    displayed: bool = True

    @property
    def env(self):
        return f"SERVICE_{self.id.upper()}_HEALTH_URL"


SERVICE_DEFINITIONS = (
    ServiceDefinition("jellyfin", "Jellyfin", ("jellyfin",)),
    ServiceDefinition("navidrome", "Navidrome", ("navidrome",)),
    ServiceDefinition("metube", "MeTube", ("metube",)),
    ServiceDefinition("bazarr", "Bazarr", ("bazarr",), displayed=False),
    ServiceDefinition("ollama", "Ollama", ("ollama",)),
    ServiceDefinition("cloudflare", "Cloudflare", ("cloudflared", "cloudflare")),
    ServiceDefinition("mcp", "MCP", ("mcp",), displayed=False),
    ServiceDefinition("nextcloud", "Nextcloud"),
    ServiceDefinition("immich", "Immich"),
    ServiceDefinition("technical_blog", "Technical Blog"),
)
SERVICES_BY_ID = {service.id: service for service in SERVICE_DEFINITIONS}
SERVICE_IDS = tuple(SERVICES_BY_ID)
