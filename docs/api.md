# API reference

All temperatures are Celsius. Times are Unix milliseconds. Every response is JSON with `Cache-Control: no-store`.

The first four routes are the shared surface: the ESP32 in standalone mode serves them itself, and the server backend serves the same shapes, so `esp32-site/index.html` works unchanged against either.

## GET /api/state

```json
{
  "box": "on",
  "t": 1788363892889,
  "s1": { "temp": 21.4, "plugged": true, "on": true },
  "s2": { "temp": null, "plugged": false, "on": false },
}
```

- `box` is `"off"` when no report has arrived for 3 seconds. Then both `temp` values are `null` and both `plugged` are `false`.
- `plugged: false` with `box: "on"` means that sensor is unplugged or faulty (the DS18B20 returned -127 or 85).
- `on` is the sensor's display button. It reflects the last state the board reported, except for up to 2 seconds after a virtual press, when it shows the requested state.

## GET /api/history

```json
{ "t": 1788363892000, "s1": [ ...300 numbers or null, newest last... ], "s2": [ ... ] }
```

One sample per second. `null` is missing data (box off or sensor unplugged at that second). Off-scale readings are real numbers; the page draws them pinned to the graph edge.

## POST /api/button

Body `{ "sensor": 1, "on": false }`. Reply `{ "b1": true, "b2": false }` with the wanted state of both buttons. The board picks the change up on its next report.

## GET and PUT /api/alerts

```json
{
  "email": "name@example.com",
  "max": 30.0,
  "min": 15.0,
  "maxMessage": "Temperature is above the limit.",
  "minMessage": "Temperature is below the limit.",
  "lastSent": { "t": 1788363892889, "sensor": 1, "kind": "max", "ok": true, "to": "name@example.com" }
}
```

PUT accepts any subset of the first five fields. `max` must be greater than `min`. A blank `email` disables sending. Saving resets the per-sensor latches. `lastSent` is `null` until the first message.

Alert rule: when a sensor reading goes above `max` (or below `min`) one message is sent. Nothing more is sent until that reading comes back inside the band by 0.5 °C, after which the next crossing sends again. Each sensor latches independently.

## POST /ingest (server only, the board's route)

Header `X-Probe-Token: <PROBE_TOKEN from the server's .env>`. Body:

```json
{ "s1": 21.4, "s2": null, "b1": true, "b2": false }
```

- `s1`/`s2`: Celsius, or `null` / -127 / 85 for a missing sensor.
- `b1`/`b2`: the box's current display button states.

Reply `{ "b1": true, "b2": false }` is the wanted button state. If it differs from what the board sent, the board should apply it as if the physical button had been pressed and report the new state on its next post. Post twice a second so a virtual press lands within the lab's 1 second.

401 without the right token. Threshold alerts are checked on every ingest.

## GET /healthz

`{ "ok": true, "uptime": 123, "box": true }`

## Electric eye (Lab 2)

The IR beam interrupter reports to the same backend with the same `PROBE_TOKEN`. The board sends its own beam-broken mail; the server only stores the settings so `eye.html` can edit them.

### POST /eye/ingest (the board's route)

Header `X-Probe-Token`. Body:

```json
{ "beam": "broken", "level": 0.412, "on": 0.30, "off": 0.21, "breaches": 3, "lastAlert": { "t": 1788363892889, "ok": true, "to": "name@example.com" } }
```

- `beam`: `"clear"` or `"broken"`. Anything else is a 400.
- `level`: the receiver's envelope level. `on`/`off`: the board's two thresholds.
- `breaches`: the board's own count of breaks.
- `lastAlert`: the last message the board sent, or `null`.

Reply `{ "ok": true }`. 401 without the right token, 400 on bad JSON.

### GET /api/eye

```json
{
  "online": true,
  "t": 1788363892889,
  "beam": "clear",
  "level": 0.412, "on": 0.30, "off": 0.21,
  "breaches": 3,
  "lastAlert": null,
  "lastSeen": 1788363892500,
  "history": { "level": [ ...300 numbers or null... ], "beam": [ ...300 of "clear", "broken" or null... ] },
  "events": [ { "t": 1788363880000, "state": "broken" }, { "t": 1788363884000, "state": "clear" } ]
}
```

- `online` is `false` when no report has arrived for 3 seconds. Then `beam` is `"unknown"` and `level`, `on` and `off` are `null`. `breaches` and `lastAlert` keep their last values.
- `lastSeen` is the time of the last report, `null` if there has never been one.
- `history` is one sample per second, newest last, `null` while offline.
- `events` is every beam change the server has seen, at most the last 50, newest last. The first report after the server starts is not counted as a change.

### GET and PUT /api/eye/alerts

```json
{ "email": "", "subject": "Electric eye: beam broken", "message": "The beam was interrupted." }
```

PUT accepts any subset of the three fields. Values are trimmed and capped at 200 characters (500 for `message`). Saved to `eye_alerts.json` in the data directory.
