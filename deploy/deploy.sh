#!/bin/bash
# Deploy both sites and the backend to the Unraid box. Run from the Mac, from the repo root:
#   deploy/deploy.sh            sync + restart what changed
#   deploy/deploy.sh --recreate force-recreate all three containers
#
# Host: root@192.168.1.200 (LAN). Files land in /mnt/user/appdata/thermo.
set -euo pipefail
cd "$(dirname "$0")/.."
HOST=${THERMO_HOST:-root@192.168.1.200}
BASE=/mnt/user/appdata/thermo
RECREATE=${1:-}

echo "== sync"
ssh "$HOST" "mkdir -p $BASE/data"
rsync -az --delete --exclude '__pycache__' --exclude 'tests' backend/     "$HOST:$BASE/backend/"
rsync -az --delete                                              server-site/ "$HOST:$BASE/server-site/"
rsync -az --delete                                              esp32-site/  "$HOST:$BASE/esp32-site/"
rsync -az --delete                                              deploy/      "$HOST:$BASE/deploy/"

echo "== env"
ssh "$HOST" bash -s <<'EOF'
set -e
BASE=/mnt/user/appdata/thermo
if [ ! -f "$BASE/.env" ]; then
  tok=$(head -c 24 /dev/urandom | base64 | tr -d '/+=' | head -c 24)
  sed "s/^PROBE_TOKEN=.*/PROBE_TOKEN=$tok/" "$BASE/deploy/env.example" > "$BASE/.env"
  chmod 600 "$BASE/.env"
  echo "created $BASE/.env with a fresh PROBE_TOKEN (fill in RESEND_API_KEY when ready)"
fi
chmod +x "$BASE"/deploy/*.sh
EOF

echo "== containers"
if [ "$RECREATE" = "--recreate" ] || ! ssh "$HOST" "docker ps --format '{{.Names}}' | grep -qx thermo-api"; then
  ssh "$HOST" "$BASE/deploy/run-thermo-api.sh"
else
  ssh "$HOST" "docker restart thermo-api" >/dev/null
fi
for name in thermo-site thermo-esp32-site; do
  if [ "$RECREATE" = "--recreate" ] || ! ssh "$HOST" "docker ps --format '{{.Names}}' | grep -qx $name"; then
    ssh "$HOST" "$BASE/deploy/run-$name.sh"
  else
    ssh "$HOST" "docker exec $name nginx -s reload" >/dev/null
  fi
done

echo "== check"
ssh "$HOST" "sleep 2; docker ps --format '{{.Names}}\t{{.Status}}' | grep thermo; \
  docker run --rm --network ai-net curlimages/curl:latest -s -m 5 http://thermo-api:8080/healthz; echo; \
  docker run --rm --network ai-net curlimages/curl:latest -s -m 5 -o /dev/null -w 'thermo-site %{http_code}\n' -H 'Host: thermo.paulandrewsullivan.com' http://thermo-site/; \
  docker run --rm --network ai-net curlimages/curl:latest -s -m 5 -o /dev/null -w 'thermo-esp32-site %{http_code}\n' -H 'Host: thermo-esp32.paulandrewsullivan.com' http://thermo-esp32-site/"
