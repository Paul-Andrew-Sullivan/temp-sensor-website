#!/bin/bash
# (Re)create the thermo-api container on the Unraid box. Idempotent.
# Expects the repo synced to /mnt/user/appdata/thermo and a .env there (mode 600).
set -euo pipefail
BASE=/mnt/user/appdata/thermo
mkdir -p "$BASE/data"
docker rm -f thermo-api >/dev/null 2>&1 || true
docker run -d --name thermo-api \
  --network ai-net \
  --restart unless-stopped \
  --log-opt max-size=20m --log-opt max-file=2 \
  -v "$BASE/backend:/app:ro" \
  -v "$BASE/data:/data" \
  --env-file "$BASE/.env" \
  -e DATA_DIR=/data -e PORT=8080 -e QUIET=1 -e PYTHONUNBUFFERED=1 \
  -w /app \
  python:3.12-alpine python3 server.py
