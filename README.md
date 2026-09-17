# Temperature probe website

ECE:4880 Lab 1, fall 2026. The computer-side interface for a two-sensor thermometer built around an ESP32-WROOM-32 and two DS18B20 probes. Two hosting options are built and both are live on the server:

| Site | URL | What it is |
|---|---|---|
| Server site | https://thermo.paulandrewsullivan.com | Full page: 3D thermometer with a try-it slider, readings, virtual buttons, 300 s chart recorder. Alert settings are built but hidden for now. Backed by `backend/`. |
| ESP32 page | https://thermo-esp32.paulandrewsullivan.com | The single lean file the board can serve by itself (no external assets, under 20 KB). Hosted here as a mirror against the same backend. |

Both pages read "No data available" until a board reports. See **The board** below.

## Layout

```
esp32-site/     index.html — the lean page, one file, inline CSS and JS
server-site/    index.html, styles.css, app.js, chart.js, thermo3d.js, vendor/ (three.js r160), models/thermometer.glb
backend/        server.py (HTTP API), state.py (ring buffer, buttons), alerts.py (thresholds, Resend)
deploy/         nginx configs, container run scripts, deploy.sh, env.example
firmware/       thermo_box/ — the standalone sketch. thermo_simple/ — probes and LCD only
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
- **Standalone mode** (ESP32-hosted): `firmware/thermo_box/thermo_box.ino` does this. Open the folder in Arduino IDE (board "ESP32 Dev Module", libraries OneWire, DallasTemperature, LiquidCrystal, ArduinoJson, ReadyMail), copy `secrets.example.h` to `secrets.h` and fill in the hotspot and the Gmail sender (a throwaway account with an app password), flash it, join the board's Wi-Fi network `thermo-box` and open http://192.168.4.1. The board also joins the hotspot named in `secrets.h` so it can send the alert emails; on that network the page is at http://thermo-box.local. `page.h` holds `esp32-site/index.html` as a raw string; after editing the page, rebuild it with

  ```bash
  { printf '// The page the board serves at "/". This is esp32-site/index.html from the\n// repo, stored in flash as one raw string. Keep the two files identical.\nconst char PAGE[] PROGMEM = R"HTML(\n'; cat esp32-site/index.html; printf ')HTML";\n'; } > firmware/thermo_box/page.h
  ```

  The API it serves is the one in `docs/api.md`. Everything on the page comes from the probes; there is no made-up data.
  The display buttons work from either end: a press on the box and the switch on the page set the same flag, so the page
  reads `turned off` or `unplugged` exactly where the LCD does. The alert settings are always on the page; where there is no `/api/alerts` behind them they stay empty and saving says so.

## Credits

Thermometer model by Armature Studios on Sketchfab, CC BY 4.0. three.js r160 (MIT), vendored in `server-site/vendor/`.
