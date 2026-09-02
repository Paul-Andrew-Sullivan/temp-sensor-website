#!/bin/bash
# (Re)create the thermo-site nginx container (thermo.paulandrewsullivan.com). Idempotent.
set -euo pipefail
BASE=/mnt/user/appdata/thermo
docker rm -f thermo-site >/dev/null 2>&1 || true
docker run -d --name thermo-site \
  --network ai-net \
  --restart unless-stopped \
  --log-opt max-size=20m --log-opt max-file=2 \
  -v "$BASE/server-site:/usr/share/nginx/html:ro" \
  -v "$BASE/deploy/nginx-thermo.conf:/etc/nginx/conf.d/default.conf:ro" \
  nginx:alpine
