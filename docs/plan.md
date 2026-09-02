# Temperature probe sites — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two web interfaces (lean ESP32 page, full server page with a 3D thermometer) and a Python backend, deployed on the Unraid box at thermo.paulandrewsullivan.com and thermo-esp32.paulandrewsullivan.com.

**Architecture:** A stdlib Python HTTP server owns the 300 s ring buffer, box state, button state, alerts (Resend) and a demo generator. Two static nginx sites call its JSON API; the lean page also works unchanged against an ESP32 serving the same API. Cloudflare tunnel `resume` carries both hostnames.

**Tech Stack:** Python 3.12 stdlib (http.server, json, urllib, unittest), plain HTML/CSS/JS, three.js r160 vendored (GLTFLoader, OrbitControls off), nginx:alpine, cloudflared, Cloudflare API.

**Spec:** `docs/design.md`

## Global Constraints

- No pip dependencies in the backend. No build step for either site.
- `esp32-site/index.html` is one file, no external requests, under 20 KB.
- Design follows `~/Desktop/Web Projects/humanlike-lab/HUMANLIKE-WEBSITES.md` only: one ground per page, flat chrome, no cards/pills/eyebrows/gradients/glow/icons/emoji, no 8–20 px radii, single accent (`#b3261e` red), underlined links or one flat rectangle button style, no scroll effects.
- Temperatures are Celsius on the wire. F is a browser display choice.
- Secrets (Resend key, ingest token) live in `/mnt/user/appdata/thermo/.env` on the server, never in git.
- Commits authored as Paul Sullivan, no co-author trailers, short imperative subjects.

---

### Task 1: Backend core state (ring buffer, box on/off, buttons)

**Files:**
- Create: `backend/state.py`
- Test: `backend/tests/test_state.py`

**Interfaces (produces):**
- `class ProbeState(history_len=300, box_timeout=3.0)`
  - `ingest(s1: float|None, s2: float|None, b1: bool, b2: bool, now: float) -> dict` returns `{"b1": wanted, "b2": wanted}`
  - `tick(now: float) -> None` appends one sample per sensor (None when box off or sensor unplugged)
  - `press(sensor: int, on: bool) -> dict` sets wanted button state
  - `state(now: float) -> dict` per spec `/api/state`
  - `history(now: float) -> dict` per spec `/api/history`
  - `box_on(now) -> bool`
- Sensor error values `-127.0` and `85.0` are treated as unplugged.

- [ ] Write tests: fresh state is box off, history all None; ingest then state shows temps; ingest older than 3 s reads box off; -127 reads `plugged: false`; tick fills newest-last; ring never exceeds 300; press returns wanted state and ingest reply echoes it.
- [ ] Run `python3 -m unittest discover -s backend/tests -v`, confirm failure.
- [ ] Implement `state.py`.
- [ ] Run tests, pass. Commit `Add backend state model with ring buffer and button state`.

### Task 2: Alerts with hysteresis and Resend delivery

**Files:**
- Create: `backend/alerts.py`
- Test: `backend/tests/test_alerts.py`

**Interfaces:**
- `class AlertConfig` dataclass: `email, max, min, max_message, min_message`, `load(path)`, `save(path)`.
- `class AlertMonitor(config, sender: Callable[[str, str, str], bool])`: `check(sensor: int, temp: float|None, now: float) -> str|None` returns `"max"|"min"|None` when it fires; rearms after the reading returns inside the band by 0.5 C. Records `last_sent` (`{"t", "sensor", "kind", "ok"}`).
- `resend_sender(api_key, from_addr)` returns a sender using `urllib.request` to `POST https://api.resend.com/emails`.

- [ ] Tests: fires once on crossing max, not again while above, rearms after dropping below max-0.5, fires on min, None temps never fire, disabled when email blank; a fake sender records subject/body containing the message and temperature.
- [ ] Implement, run, commit `Add threshold alerts with hysteresis and Resend sender`.

### Task 3: Demo generator

**Files:**
- Create: `backend/demo.py`
- Test: `backend/tests/test_demo.py`

**Interfaces:** `class Demo(scenario="normal")` with `.on: bool`, `.scenario`, `sample(now) -> tuple[s1, s2]` producing slow sine drift around 22 C; scenarios `unplugged1`, `unplugged2` return None for that sensor, `boxoff` returns `None, None` and marks box absent (caller skips ingest), `high` climbs to 58 C, `low` sinks to 4 C.

- [ ] Tests for each scenario's shape. Implement, commit `Add demo reading generator`.

### Task 4: HTTP server

**Files:**
- Create: `backend/server.py`, `backend/config.py`
- Test: `backend/tests/test_server.py` (spin server on a free port in a thread, use urllib)

**Routes:** per spec. `GET /api/state`, `GET /api/history`, `POST /api/button`, `GET|PUT /api/alerts`, `POST /api/demo`, `POST /ingest` (checks `X-Probe-Token` against env `PROBE_TOKEN`; a real ingest turns demo off), `GET /healthz`. A background thread calls `tick()` once per second and feeds demo samples through `ingest()` when demo is on and scenario is not `boxoff`. JSON only, `Cache-Control: no-store`. Env: `PORT`, `DATA_DIR`, `PROBE_TOKEN`, `RESEND_API_KEY`, `ALERT_FROM`, `DEMO_DEFAULT` (`1` starts in demo).

- [ ] Tests: state 200 JSON; button toggles; alerts round-trip and persist to file; ingest 401 without token, 200 with, and disables demo; history has 300 entries.
- [ ] Implement, run all tests, commit `Add HTTP API server`.

### Task 5: Lean ESP32 page

**Files:**
- Create: `esp32-site/index.html`
- Create: `esp32-site/README.md` (how to embed: `xxd -i` or PROGMEM raw string, and the API it expects)

**Content per spec section "ESP32 page".** Chart recorder implementation: canvas 600×220 CSS px, devicePixelRatio aware; y from 10 to 50 C mapped to plot height; per column x = width × (1 − secondsAgo/300); missing sample = 45° hatch fill on that column in `#d9d3c7`; off-scale = value clamped and drawn as a 2 px line pinned to top/bottom edge in accent with the text "off scale" once per run; x axis ticks every 60 s labeled `300 … 0`, axis title "seconds ago". Polling: `fetch('/api/state')` every 1000 ms, `fetch('/api/history')` at load and whenever a state tick's timestamp jumps by more than 2 s. A local `samples` array holds 300 values per sensor; each state tick shifts a new value in. Units switch converts at draw time; y labels switch to 50 … 122 F.

- [ ] Build the page. Verify size `wc -c < esp32-site/index.html` under 20480. Open against the backend in demo mode (scenarios normal, unplugged1, boxoff, high, low) with Playwright screenshots. Commit `Add lean ESP32 page`.

### Task 6: Server page with 3D thermometer

**Files:**
- Create: `server-site/index.html`, `server-site/styles.css`, `server-site/app.js`, `server-site/chart.js` (same chart code as Task 5, exported as `makeChart(canvas, opts)`), `server-site/thermo3d.js`, `server-site/vendor/three.module.min.js`, `server-site/vendor/GLTFLoader.js`, `server-site/models/thermometer.glb`

**thermo3d.js:** import three from `./vendor/`; load GLB; remove node `Thermometer.001_9` (a hidden scaled-to-zero duplicate); find mesh with material name starting `Thermo_Mercury`; set `renderer.localClippingEnabled = true`, material `side = DoubleSide`, `clippingPlanes = [plane]` where plane normal is `(0,-1,0)` in world space; compute world-space y of the mercury mesh's bounding box: `yBottom` = top of the bulb (bbox min + 12% of height) and `yTop` = bbox max; `setTemp(c)` maps `-10 → yBottom`, `63 → yTop`, clamps, eases with `current += (target-current)*0.12` per frame; `setMissing()` drops the column to `yBottom`. Camera fixed, slight auto-rotate off; single hemisphere light + one directional; transparent glass material kept from the GLB. Canvas sits on the page ground, no box.

- [ ] Build page, wire chart + state polling from `app.js` (shared with the lean page logic but in modules), alerts form with "last alert sent", demo line with toggle, sensor selector for the model. Verify with Playwright screenshots in all five scenarios plus the model at 10, 22, 40, 60 C. Commit `Add server page with 3D thermometer`.

### Task 7: Deploy scaffolding

**Files:**
- Create: `deploy/nginx-thermo.conf`, `deploy/nginx-thermo-esp32.conf`, `deploy/run-thermo-api.sh`, `deploy/run-thermo-site.sh`, `deploy/run-thermo-esp32-site.sh`, `deploy/deploy.sh`, `deploy/env.example`, `docs/api.md`, `docs/resend.md`, `README.md`

**deploy.sh:** rsync `backend/ server-site/ esp32-site/ deploy/` to `root@192.168.1.200:/mnt/user/appdata/thermo/`, create `.env` from example if missing (mode 600, random `PROBE_TOKEN`), run the three run scripts (idempotent `docker rm -f` + `docker run` on `ai-net`), `docker exec ... nginx -s reload`.

- [ ] Write files, run `deploy.sh`, check `docker ps` and `curl` inside ai-net. Commit `Add deploy scripts and docs`.

### Task 8: Tunnel ingress + DNS + public verification

- [ ] Append two ingress rules above the 404 catch-all in `/mnt/user/appdata/cloudflared/config.yml` (backup first with a dated copy). Restart `cloudflared` container if logs show no reload.
- [ ] Cloudflare API with the token at `/mnt/user/appdata/cloudflared-thruandrewslens/cf-api-token`: find zone id for `paulandrewsullivan.com`, create proxied CNAMEs `thermo` and `thermo-esp32` → `c58355fa-62e4-4257-a153-d9e6057e8b4a.cfargotunnel.com`.
- [ ] Verify via Cloudflare DoH + `curl --resolve` from the Mac: both pages 200, `/api/state` JSON, GLB 200.
- [ ] Push repo. Record deployment in `agent-handoff/homelab/SOLUTIONS.md` + `STATUS.md`, memory file for the project.
