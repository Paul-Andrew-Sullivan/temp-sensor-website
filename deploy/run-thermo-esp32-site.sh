#!/bin/bash
# (Re)create the thermo-esp32-site nginx container (thermo-esp32.paulandrewsullivan.com). Idempotent.
set -euo pipefail
BASE=/mnt/user/appdata/thermo
docker rm -f thermo-esp32-site >/dev/null 2>&1 || true
docker run -d --name thermo-esp32-site \
  --network ai-net \
  --restart unless-stopped \
  --log-opt max-size=20m --log-opt max-file=2 \
  -v "$BASE/esp32-site:/usr/share/nginx/html:ro" \
  -v "$BASE/deploy/nginx-thermo-esp32.conf:/etc/nginx/conf.d/default.conf:ro" \
  nginx:alpine
