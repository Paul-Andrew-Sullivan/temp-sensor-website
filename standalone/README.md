# Standalone build (everything on the ESP32)

A frozen copy of the version where the box ran the whole system on its own: it
served the web page from its own flash, kept the 300 second history in its own
RAM, and sent the alert mail itself. Nothing here needs the server.

Kept as of commit `595edef`, also tagged `standalone-esp32`.

## Why it was set aside

Requirement 5a.ii wants the computer to show "no data available" while the box
is switched off, and requirement 6 wants the readings back within ten seconds of
the box being switched on. A page served by the box cannot do either, because
the thing that would serve it is the thing that is off. The main build therefore
reports to the server and the page is served from there.

This copy is still the better answer if the box has to work with no server and
no internet, so it is worth keeping.

## What is here

| | |
|---|---|
| `thermo_box/` | the sketch, complete. `page.h` is `index.html` stored as one raw string |
| `index.html` | the page the board serves at `/` |

## Running it

Copy `thermo_box/` into the Arduino sketchbook, copy `secrets.example.h` to
`secrets.h` in that folder and fill in the hotspot and the Gmail sender, then
flash it as "ESP32 Dev Module".

The box makes its own Wi-Fi network `thermo-box`, password `twoprobes`. Join it
and open http://192.168.4.1. It also joins the hotspot named in `secrets.h` so
it can reach Gmail, and on that network the page is at http://thermo-box.local.

Libraries: OneWire, DallasTemperature, LiquidCrystal, ArduinoJson, ReadyMail.
It builds to about 1.17 MB, 89 % of the default partition.

**Do not put a real `secrets.h` in this folder.** The repository is public and
only `firmware/thermo_box/secrets.h` is ignored by git. Keep the filled-in copy
in the sketchbook, where it is not in a repository at all.
