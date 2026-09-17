# Server mode (the build we are using)

The website is on the server. The box measures, shows, and reports; the server
holds the page, the history, and the settings.

    probes -> ESP32 -> POST /ingest -> server -> website
                 ^                                  |
                 +------ button states in the reply -+

## Why this way round

Requirement 5a.ii wants the computer to read "no data available" while the box
is switched off, and requirement 6 wants the readings back within ten seconds of
switching it on. A page served by the box can do neither, because the thing that
would serve it is the thing that is off. The server is up either way.

Requirement 5c.v also asks that the graph keep scrolling with the data shown as
missing while the box is off. The server's history does that on its own, because
it never loses power.

The older all-on-the-box version is kept in `../standalone/`.

## What is here

| | |
|---|---|
| `thermo_box/` | the sketch. Copy `secrets.example.h` to `secrets.h` and fill it in |

The website and the API it talks to are `../server-site/` and `../backend/`, at
the top of the repo because `deploy/deploy.sh` rsyncs them from there.

## The box

Joins the hotspot in `secrets.h` and posts to `INGEST_URL` twice a second:

```json
{ "s1": 21.4, "s2": null, "b1": true, "b2": false }
```

with header `X-Probe-Token`. The reply is `{ "b1": true, "b2": false }`, the
button states the web page wants. When they differ from the box's own, the box
follows the page and the LCD changes with it. That is requirement 5b, and twice
a second keeps it inside the one second the requirement allows.

The LCD reads `Sensor 1 off` when a button is off and `Sensor 1 error` when a
probe is silent, which are requirements 4 and 4d.

Libraries: OneWire, DallasTemperature, LiquidCrystal, ArduinoJson, ReadyMail.
Builds to about 1.18 MB, 90 % of the default partition.

**Do not commit a filled-in `secrets.h`.** The repository is public. `.gitignore`
covers `secrets.h` anywhere, but keeping the real one in the Arduino sketchbook
rather than the repo is safer still.
