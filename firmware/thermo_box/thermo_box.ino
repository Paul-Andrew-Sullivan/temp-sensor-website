/*
  thermo_box.ino
  ECE:4880 Lab 1, fall 2026. Two-sensor thermometer box.

  Board: ESP32-WROOM-32 dev kit, Arduino IDE board "ESP32 Dev Module".
  Libraries: OneWire, DallasTemperature, LiquidCrystal (built in), plus the
  WiFi and WebServer libraries that ship with the esp32 core.

  Once a second the sketch
    - reads both DS18B20 probes on the one-wire bus,
    - shows them on the 16x2 LCD, unless that sensor's button hid it,
    - prints them on the serial monitor (115200 baud),
    - stores them in a 300 second history for the graph on the web page.

  The board runs its own Wi-Fi network so nothing depends on the lab's
  Wi-Fi. Join "thermo-box" (password below) and open http://192.168.4.1.
  The page and the /api/state, /api/history and /api/button routes match
  docs/api.md, so the same page works against the server backend too.

  Wiring, from the schematic (2026-09-09):
    DS18B20 x2  DQ -> GPIO32, 4.7k pull-up to 3V3
    SW1         GPIO34, 10k pull-up, 1k series, 0.1uF, pressed = LOW
    SW2         GPIO35, same
    LCD HY1602E RS=GPIO25  E=GPIO26  D4=GPIO18  D5=GPIO19  D6=GPIO23  D7=GPIO27
                R/W to ground, 4-bit mode, DB0..DB3 left open
*/

#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal.h>
#include "page.h"

const int ONEWIRE_PIN = 32;
const int BUTTON_PIN[2] = {34, 35};   // input-only pins, pull-ups are on the board
LiquidCrystal lcd(25, 26, 18, 19, 23, 27);   // RS, E, D4, D5, D6, D7

const char *AP_NAME = "thermo-box";
const char *AP_PASSWORD = "twoprobes";   // WPA2 wants at least 8 characters

OneWire bus(ONEWIRE_PIN);
DallasTemperature probes(&bus);
WebServer server(80);

// Each probe keeps its slot by ROM id, so unplugging sensor 1 does not turn
// sensor 2 into sensor 1. A slot is filled the first time a new probe shows
// up on the bus, and the id is printed on the serial monitor.
DeviceAddress rom[2];
bool haveRom[2] = {false, false};
float tempC[2] = {NAN, NAN};       // NAN = unplugged, or no reading yet
bool shown[2] = {true, true};      // the display button for each sensor

const int HISTORY = 300;           // seconds kept for the graph
float history[2][HISTORY];
int histNext = 0;                  // index the next sample is written to

unsigned long lastSampleMs = 0;
int secondsSinceScan = 0;

int buttonLevel[2] = {HIGH, HIGH};
unsigned long buttonChangedMs[2] = {0, 0};

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN[0], INPUT);
  pinMode(BUTTON_PIN[1], INPUT);

  for (int i = 0; i < 2; i++)
    for (int k = 0; k < HISTORY; k++) history[i][k] = NAN;

  lcd.begin(16, 2);
  lcdLine(0, "Thermo box");
  lcdLine(1, "starting");

  probes.begin();
  probes.setResolution(12);             // 0.0625 C steps, 750 ms per conversion
  probes.setWaitForConversion(false);   // never block the loop on a conversion
  scanBus();
  probes.requestTemperatures();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, AP_PASSWORD);
  String ip = WiFi.softAPIP().toString();
  Serial.println();
  Serial.print("Wi-Fi network: ");
  Serial.println(AP_NAME);
  Serial.print("Page: http://");
  Serial.println(ip);
  lcdLine(0, String("wifi ") + AP_NAME);
  lcdLine(1, ip);
  delay(3000);   // long enough to read the address off the screen

  server.on("/", sendPage);
  server.on("/api/state", sendState);
  server.on("/api/history", sendHistory);
  server.on("/api/button", HTTP_POST, pressButton);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();

  lastSampleMs = millis();
}

void loop() {
  server.handleClient();
  pollButtons();

  if (millis() - lastSampleMs >= 1000) {
    lastSampleMs = millis();
    readProbes();                  // the conversion started one second ago
    if (++secondsSinceScan >= 5) { // pick up a probe plugged in after boot
      secondsSinceScan = 0;
      scanBus();
    }
    probes.requestTemperatures();  // start the next conversion
    recordSample();
    updateLcd();
    printReadings();
  }
}

// ---- probes ----------------------------------------------------------------

// Walk the bus and give any probe we have not seen before the first empty slot.
void scanBus() {
  DeviceAddress found;
  bus.reset_search();
  while (bus.search(found)) {
    if (OneWire::crc8(found, 7) != found[7]) continue;   // garbled id
    if (found[0] != 0x28) continue;                       // not a DS18B20
    if (slotOf(found) >= 0) continue;                     // already placed
    for (int i = 0; i < 2; i++) {
      if (haveRom[i]) continue;
      memcpy(rom[i], found, sizeof(DeviceAddress));
      haveRom[i] = true;
      Serial.print("Sensor ");
      Serial.print(i + 1);
      Serial.print(" is probe ");
      for (int b = 0; b < 8; b++) {
        if (found[b] < 16) Serial.print('0');
        Serial.print(found[b], HEX);
      }
      Serial.println();
      break;
    }
  }
}

int slotOf(const DeviceAddress a) {
  for (int i = 0; i < 2; i++)
    if (haveRom[i] && memcmp(rom[i], a, sizeof(DeviceAddress)) == 0) return i;
  return -1;
}

// -127 means the probe did not answer. 85 is the power-on value a DS18B20
// reports before its first conversion finishes. Both count as no reading.
void readProbes() {
  for (int i = 0; i < 2; i++) {
    tempC[i] = NAN;
    if (!haveRom[i]) continue;
    float t = probes.getTempC(rom[i]);
    if (t == DEVICE_DISCONNECTED_C || t == 85.0) continue;
    tempC[i] = t;
  }
}

void recordSample() {
  history[0][histNext] = tempC[0];
  history[1][histNext] = tempC[1];
  histNext = (histNext + 1) % HISTORY;
}

// ---- display ---------------------------------------------------------------

// Line 1 is sensor 1, line 2 is sensor 2. Each line is padded to 16 characters
// so a shorter message wipes the old one without a clear() flicker.
void updateLcd() {
  for (int i = 0; i < 2; i++) {
    String line = "S" + String(i + 1) + "  ";
    if (!shown[i]) line += "not shown";
    else if (isnan(tempC[i])) line += "unplugged";
    else line += String(tempC[i], 1) + (char)223 + "C";   // 223 is the degree sign
    lcdLine(i, line);
  }
}

void lcdLine(int row, String text) {
  while (text.length() < 16) text += ' ';
  lcd.setCursor(0, row);
  lcd.print(text);
}

void printReadings() {
  for (int i = 0; i < 2; i++) {
    Serial.print("S");
    Serial.print(i + 1);
    Serial.print(": ");
    if (isnan(tempC[i])) Serial.print("unplugged");
    else {
      Serial.print(tempC[i], 2);
      Serial.print(" C");
    }
    Serial.print(i == 0 ? "   " : "\n");
  }
}

// A press toggles that sensor's display. Level changes closer than 40 ms to
// the last accepted one are ignored, which covers the contact bounce.
void pollButtons() {
  for (int i = 0; i < 2; i++) {
    int level = digitalRead(BUTTON_PIN[i]);
    if (level == buttonLevel[i]) continue;
    if (millis() - buttonChangedMs[i] < 40) continue;
    buttonChangedMs[i] = millis();
    buttonLevel[i] = level;
    if (level == LOW) {
      shown[i] = !shown[i];
      updateLcd();
    }
  }
}

// ---- web -------------------------------------------------------------------

void sendPage() {
  server.send_P(200, "text/html", PAGE);
}

void sendJson(const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

String numberOrNull(float v) {
  if (isnan(v)) return "null";
  return String(v, 2);
}

String trueOrFalse(bool b) {
  return b ? "true" : "false";
}

void sendState() {
  String j = "{\"box\":\"on\",\"t\":";
  j += millis();
  for (int i = 0; i < 2; i++) {
    j += ",\"s" + String(i + 1) + "\":{\"temp\":" + numberOrNull(tempC[i]);
    j += ",\"plugged\":" + trueOrFalse(!isnan(tempC[i]));
    j += ",\"on\":" + trueOrFalse(shown[i]) + "}";
  }
  j += "}";
  sendJson(j);
}

// Oldest sample first, newest last, the order the page expects.
void sendHistory() {
  String j;
  j.reserve(2 * HISTORY * 7 + 64);
  j = "{\"t\":";
  j += millis();
  for (int i = 0; i < 2; i++) {
    j += ",\"s" + String(i + 1) + "\":[";
    for (int k = 0; k < HISTORY; k++) {
      if (k) j += ',';
      j += numberOrNull(history[i][(histNext + k) % HISTORY]);
    }
    j += ']';
  }
  j += '}';
  sendJson(j);
}

// Body is {"sensor":1,"on":false}, exactly as the page sends it.
void pressButton() {
  String body = server.arg("plain");
  int i = body.indexOf("\"sensor\":2") >= 0 ? 1 : 0;
  shown[i] = body.indexOf("\"on\":true") >= 0;
  updateLcd();
  sendJson("{\"b1\":" + trueOrFalse(shown[0]) + ",\"b2\":" + trueOrFalse(shown[1]) + "}");
}
