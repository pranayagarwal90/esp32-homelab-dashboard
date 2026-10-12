# HomeDeck Mini backend: the same backend/server.py as a native install,
# standard library only. Configuration comes from the environment (compose.yaml
# passes .env); nothing secret is baked into the image. Release signing keys
# never belong here: releases are signed offline (tools/), and the backend
# only serves the signed files it is given (a read-only mount).
FROM python:3.12-slim

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    DASHBOARD_ENV_FILE="" \
    DASHBOARD_BIND=0.0.0.0 \
    DASHBOARD_PORT=8090

# Unprivileged runtime user; the code is root-owned and read-only to it.
RUN useradd --system --uid 10001 --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin homedeck

WORKDIR /app
# Only the server's own modules (.dockerignore allows nothing else).
COPY backend/server.py backend/config.py backend/services.py backend/ai_assistant.py \
     backend/firmware_updates.py /app/backend/

USER homedeck
EXPOSE 8090

# Liveness only: /health does no host, Docker, weather, AI or firmware work.
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD ["python", "-c", "import os, urllib.request; urllib.request.urlopen('http://127.0.0.1:%s/health' % os.environ.get('DASHBOARD_PORT', '8090'), timeout=3).read()"]

# Exec form: Python is PID 1 and handles SIGTERM itself (server.py).
CMD ["python", "backend/server.py"]
