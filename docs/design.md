# Temperature probe website — design

ECE:4880 Lab 1, Fall 2026. Two web interfaces for a two-sensor thermometer built around an ESP32-WROOM-32. This document describes what is being built and why, so the team can review it before and during implementation.

## Goal

Satisfy requirements 5, 6 and 7 of the lab sheet (the computer-side interface) in two hosting configurations, so the team can pick either without rework:

- **ESP32-hosted.** The ESP32 serves a single lean page and a small JSON API itself. Any laptop on the same Wi-Fi opens the board's IP.
- **Server-hosted.** The ESP32 posts readings to a backend on the Unraid server, which stores history, sends alerts, and serves a richer page with a 3D thermometer.

Both sites are deployed on the server now, at:

| Site | URL | Backing |
|---|---|---|
| Server site | https://thermo.paulandrewsullivan.com | nginx static + Python backend |
| ESP32 page (mirror) | https://thermo-esp32.paulandrewsullivan.com | nginx static, API proxied to the same backend |

## Requirements this covers (from the lab sheet)

- 5a: both temperatures, large, updated once per second, C or F chosen by the user. "Unplugged sensor" replaces a reading when that sensor is missing. "No data available" replaces both when the box is off.
- 5b: the computer can virtually press either sensor button; response under 1 second.
- 5c: scrolling chart-recorder graph of the past 300 seconds, fixed axis 10 to 50 C (50 to 122 F), x axis labeled in seconds ago (300 to 0), new sample per second on the right. Missing data is visibly different from off-scale data. The graph keeps scrolling while data is missing.
- 6: within 10 seconds of the box turning on, the graph and readings appear.
- 7: email (and text, via email-to-SMS) when a reading goes above a max or below a min. Both messages, both limits and the destination are editable in the UI.
- 8a: the display range is at least -10 to +63 C. The readouts show whatever the sensor reports; the graph clips and marks off-scale values.

Requirements 1 to 4 are hardware and firmware and are out of scope here.

## Repo layout

```
esp32-site/     the lean page (single index.html, no external assets)
server-site/    the full page (index.html, styles, app.js, vendor/three, thermometer.glb)
backend/        Python 3.12 stdlib HTTP server (no pip dependencies)
deploy/         nginx configs, container run scripts, deploy.sh for the Unraid box
docs/           this file, API reference, Resend setup notes
```

## Shared JSON API

The ESP32 in standalone mode and the server backend both expose the same read/control surface, so the lean page works against either without changes.

| Method | Path | Body / response |
|---|---|---|
| GET | `/api/state` | `{ "box": "on"\|"off", "t": <unix ms>, "s1": {"temp": 21.4\|null, "plugged": true, "on": true}, "s2": {...} }` |
| GET | `/api/history` | `{ "t": <unix ms of newest>, "s1": [300 values, newest last, null = missing], "s2": [...] }` |
| POST | `/api/button` | `{ "sensor": 1\|2, "on": true\|false }` → new state |
| GET / PUT | `/api/alerts` | `{ "email": "", "max": 30, "min": 15, "maxMessage": "", "minMessage": "" }` (server backend only) |

Server-only:

| Method | Path | Purpose |
|---|---|---|
| POST | `/ingest` | ESP32 client mode. Header `X-Probe-Token`. Body `{ "s1": 21.4\|null, "s2": null, "b1": true, "b2": false }`. Reply carries wanted button states `{ "b1": true, "b2": false }` so the board applies virtual presses on its next post (twice a second, so under 1 s). |

Temperatures travel in Celsius. Fahrenheit is a display choice made in the browser.

## Backend behavior

- Ring buffer of 300 samples per sensor at 1 Hz, `null` for missing. A sample is taken every second from the latest ingest; if the last ingest is older than 3 seconds the box is considered off and both samples are `null`.
- A sensor reports `null` when the ESP32 says it is unplugged or faulty (DS18B20 returns -127 or 85 on error). The state endpoint exposes `plugged: false` for that case so the UI can say "unplugged sensor" rather than "no data".
- Button state lives on the server. A virtual press changes the wanted state; the ESP32 reads it in the next ingest reply and confirms with its own `b1`/`b2` values.
- Alerts: on every ingest, if a sensor reading exceeds `max` or drops under `min`, send once and hold until the reading returns inside the band (with 0.5 C hysteresis), then rearm. Delivery through the Resend HTTP API. The API key and sender address come from an env file on the server, never from git. Text delivery uses the carrier's email gateway address entered in the email field.
- Alert settings persist to a JSON file in the container's data volume.

## The two pages

Both follow `HUMANLIKE-WEBSITES.md` and nothing else. One paper ground per page. Flat chrome. No cards, pills, eyebrow labels, gradients, glow, icons, emoji or rounded-corner middle ground. One accent, spent on the thermometer's red and the alert rule. CTAs are underlined links or one flat rectangle. Copy is specific to this device and this class. Named people in the footer (the team), dated.

### ESP32 page (`esp32-site/index.html`)

Single file, inline CSS and JS, no web fonts (system serif and mono stacks), target under 20 KB so it fits in flash as a string. Structure, top to bottom, left aligned at about 60ch:

1. Title line: "Two-sensor thermometer" plus the box status in words.
2. Two large readings in a mono face, sensor 1 and sensor 2, with a plain C / F switch. A reading is replaced by "unplugged sensor" or "no data available" per 5a.
3. Two virtual buttons, one per sensor, plain flat rectangles that read "Sensor 1 display: on" and toggle.
4. The chart recorder: a `<canvas>` drawn by hand. Fixed y axis, x axis "seconds ago" 300 to 0, one new sample per second scrolling right to left. Missing data draws as a light hatched band; off-scale draws as a flat line pinned to the edge in the accent color with a small label. The chart keeps scrolling while data is missing.
5. Alert settings: email, max, min, two messages, a save link. On the ESP32 build this section is hidden because the board cannot send mail.
6. Footer: team names, course, date built, "page served by the ESP32 at this address".

Polling: state every second, history once at load, then the page appends the state sample itself and refetches history only if it detects a gap.

### Server page (`server-site/`)

Same information, same controls, plus:

- The thermometer model (`thermometer.glb`, Armature Studios on Sketchfab, CC-BY-4.0, credited in the footer) rendered with three.js from a vendored copy. The red column is the model's own mercury mesh; its visible height is set with a clipping plane at the temperature mapped between -10 C (bulb) and 63 C (top of the column). The column eases to each new reading over about 600 ms. One model, showing whichever sensor is selected; a plain link swaps between sensor 1 and sensor 2.
- Web fonts allowed here (Literata for headings and running text, a mono for digits), self-hosted or from Google Fonts.
- The chart recorder is the same canvas code, wider.
- Alerts section fully working, with a "last alert sent" line.

Motion is limited to the mercury easing and the chart scroll. No scroll effects, no hover lifts.

## Deployment on the Unraid box

- Two `nginx:alpine` containers, `thermo-site` and `thermo-esp32-site`, on `ai-net`, document roots bind-mounted from `/mnt/user/appdata/thermo/`.
- One `python:3.12-alpine` container, `thermo-api`, on `ai-net`, code at `/mnt/user/appdata/thermo/backend`, data at `/mnt/user/appdata/thermo/data`, env at `/mnt/user/appdata/thermo/.env` (mode 600).
- nginx proxies `/api/` and `/ingest` to `thermo-api:8080` on both hosts.
- Two ingress lines added to `/mnt/user/appdata/cloudflared/config.yml` (the `resume` tunnel that already carries paulandrewsullivan.com) and two proxied CNAMEs created through the Cloudflare API, since the tunnel's cert.pem is a token stub.
- `deploy/deploy.sh` rsyncs the repo to the box and restarts what changed. Run from the Mac.

When the team moves to real hardware in client mode, the ESP32 posts to `https://thermo.paulandrewsullivan.com/ingest` with the shared token. In standalone mode the ESP32 serves `esp32-site/index.html` verbatim.

## Testing

- Backend: unit tests with `unittest` for the ring buffer, box-off detection, alert hysteresis, and the ingest/state/history/button handlers using the stdlib test client.
- Pages: opened in a real browser against the backend, checked for the five display states (normal, unplugged one sensor, box off, off-scale high, off-scale low) by posting the matching readings to `/ingest` with curl.
- Deployment: curl through the Cloudflare edge for both hosts and `/api/state`.

## Out of scope for this pass

ESP32 firmware (both sketches), the physical box, and SMS through a paid gateway. Resend covers email; carrier gateways cover text.
