# Temperature probe website

ECE:4880 Lab 1, fall 2026. The computer-side interface for a two-sensor thermometer built around an ESP32-WROOM-32 and two DS18B20 probes. Two hosting options are built and both are live on the server:

| Site | URL | What it is |
|---|---|---|
| Server site | https://thermo.paulandrewsullivan.com | Full page: 3D thermometer with a try-it slider, readings, virtual buttons, 300 s chart recorder. Alert settings are built but hidden for now. Backed by `backend/`. |
| ESP32 page | https://thermo-esp32.paulandrewsullivan.com | The single lean file the board can serve by itself (no external assets, under 20 KB). Hosted here as a mirror against the same backend. |

Both pages read "No data available" until a board posts to `/ingest`. See **The board** below.

## Layout

```
esp32-site/     index.html — the lean page, one file, inline CSS and JS
server-site/    index.html, styles.css, app.js, chart.js, thermo3d.js, vendor/ (three.js r160), models/thermometer.glb
backend/        server.py (HTTP API), state.py (ring buffer, buttons), alerts.py (thresholds, Resend)
deploy/         nginx configs, container run scripts, deploy.sh, env.example
docs/           design.md, plan.md, api.md, resend.md
```

## Run locally

```bash
cd backend
PORT=8080 STATIC_DIR=../server-site python3 server.py     # http://localhost:8080
PORT=8081 STATIC_DIR=../esp32-site  python3 server.py     # http://localhost:8081
```

No dependencies beyond Python 3.11+. `STATIC_DIR` makes the API serve a site folder too, which is only for local work; in production nginx serves the files.

## Deploy

`deploy/deploy.sh` from the Mac rsyncs to `/mnt/user/appdata/thermo` on the Unraid box and (re)starts three containers on `ai-net`: `thermo-api`, `thermo-site`, `thermo-esp32-site`. The Cloudflare tunnel already carrying paulandrewsullivan.com routes the two hostnames to the two nginx containers. Secrets live in `/mnt/user/appdata/thermo/.env` (see `deploy/env.example`), never in git.

## The board

- **Client mode** (server-hosted): post `{ "s1": 21.4, "s2": null, "b1": true, "b2": true }` to `https://thermo.paulandrewsullivan.com/ingest` with header `X-Probe-Token` twice a second. The reply carries the wanted button states. Details in `docs/api.md`.
- **Standalone mode** (ESP32-hosted): serve `esp32-site/index.html` at `/` and implement `/api/state`, `/api/history`, `/api/button` as described in `docs/api.md`. The page hides its alert section when `/api/alerts` returns 404.

The standalone HTML includes its styles, chart, and scripts; it needs no CDN, web fonts, or JavaScript libraries. Serve it as `text/html; charset=utf-8` from flash (for example, PROGMEM) or a filesystem such as LittleFS, rather than assembling the page in RAM. Rendering runs in the visitor's browser. The board must still implement the API routes above.

Firmware is not in this repo yet.

## Credits

Thermometer model by Armature Studios on Sketchfab, CC BY 4.0. three.js r160 (MIT), vendored in `server-site/vendor/`.
