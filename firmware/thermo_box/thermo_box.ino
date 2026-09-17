/*
  thermo_box.ino
  ECE:4880 Lab 1, fall 2026. Two-sensor thermometer box.

  Board: ESP32-WROOM-32 dev kit, Arduino IDE board "ESP32 Dev Module".
  Libraries: OneWire, DallasTemperature, ArduinoJson, ReadyMail, and the
  LiquidCrystal, WiFi, WebServer, ESPmDNS, Preferences libraries that ship
  with the IDE or the esp32 core.

  Once a second the sketch
    - reads both DS18B20 probes on the one-wire bus,
    - shows them on the 16x2 LCD, unless that sensor's button hid it,
    - prints them on the serial monitor (115200 baud),
    - stores them in a 300 second history for the graph on the web page,
    - checks them against the alert limits and emails when one is crossed.

  Wi-Fi: the board joins the network named in secrets.h, a phone hotspot,
  and does not run one of its own. The page is at http://thermo-box.local,
  or the address printed on the LCD and the serial monitor when it joins.
  Nothing reaches the box until that network is up: no page, no mail, and
  no clock.

  The graph survives a power cut. Every 30 seconds the last 300 readings go
  to a file in flash, stamped with the wall clock of the newest one. On the
  next boot, once the time server has answered, every saved reading is placed
  by its own age and anything older than 300 seconds is dropped. The seconds
  the box was off hold no readings, so the graph shows the break rather than
  drawing a line across it. The time comes from the network, which is why the
  box can measure how long it was off without a battery.

  Alerts: the page's "Email or text when it goes out of range" form is
  stored on the board (it survives power cycles) and read back with GET
  and PUT /api/alerts, the same JSON as docs/api.md. The rule matches the
  server: one message per crossing, per sensor, rearmed when the reading
  comes back inside the band by half a degree. A send takes a few seconds
  and the loop waits for it, so the LCD and page pause briefly.

  Wiring, from the schematic (2026-09-09):
    DS18B20 x2  DQ -> GPIO32, 4.7k pull-up to 3V3
    SW1         GPIO34, 10k pull-up, 1k series, 0.1uF, pressed = LOW
    SW2         GPIO35, same
    LCD HY1602E RS=GPIO25  E=GPIO26  D4=GPIO18  D5=GPIO19  D6=GPIO23  D7=GPIO27
                R/W to ground, 4-bit mode, DB0..DB3 left open
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal.h>
#include <time.h>

#define ENABLE_SMTP
#include <ReadyMail.h>

#include "page.h"
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, MAIL_ADDRESS, MAIL_APP_PASSWORD

const int ONEWIRE_PIN = 32;
const int BUTTON_PIN[2] = {34, 35};   // input-only pins, pull-ups are on the board
LiquidCrystal lcd(25, 26, 18, 19, 23, 27);   // RS, E, D4, D5, D6, D7

const char *AP_NAME = "thermo-box";
const char *AP_PASSWORD = "twoprobes";   // WPA2 wants at least 8 characters
const char *HOSTNAME = "thermo-box";     // http://thermo-box.local on the joined network

OneWire bus(ONEWIRE_PIN);
DallasTemperature probes(&bus);
WebServer server(80);
WiFiClientSecure sslClient;
SMTPClient smtp(sslClient);
Preferences store;   // the alert settings, kept in flash

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

unsigned long lcdHoldUntil = 0;    // readings stay off the LCD until this time
bool wifiUp = false;               // joined WIFI_SSID and holding an address
bool mdnsStarted = false;

// ---- alert settings and state ---------------------------------------------

const float HYSTERESIS_C = 0.5;

struct AlertConfig {
  String email;                    // blank = alerts off
  float maxC = 30.0;
  float minC = 15.0;
  String maxMessage = "Temperature is above the limit.";
  String minMessage = "Temperature is below the limit.";
};
AlertConfig alerts;

enum Latch { NONE, ABOVE, BELOW };
Latch latched[2] = {NONE, NONE};

// The last message, for the page's "Last message sent" line.
bool sentAny = false;
time_t sentAt = 0;
int sentSensor = 0;
Latch sentKind = NONE;
bool sentOk = false;
String sentTo;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN[0], INPUT);
  pinMode(BUTTON_PIN[1], INPUT);

  for (int i = 0; i < 2; i++)
    for (int k = 0; k < HISTORY; k++) history[i][k] = NAN;

  lcd.begin(16, 2);
  lcdLine(0, "Thermo box");
  lcdLine(1, "starting");

  loadAlerts();

  probes.begin();
  probes.setResolution(12);             // 0.0625 C steps, 750 ms per conversion
  probes.setWaitForConversion(false);   // never block the loop on a conversion
  scanBus();
  probes.requestTemperatures();

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, AP_PASSWORD);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // returns at once; watchWifi() sees it join
  String ip = WiFi.softAPIP().toString();
  Serial.println();
  Serial.print("Wi-Fi network: ");
  Serial.println(AP_NAME);
  Serial.print("Page: http://");
  Serial.println(ip);
  Serial.print("Joining ");
  Serial.print(WIFI_SSID);
  Serial.println(" for mail");
  lcdLine(0, String("wifi ") + AP_NAME);
  lcdLine(1, ip);
  delay(3000);   // long enough to read the address off the screen

  server.on("/", sendPage);
  server.on("/api/state", sendState);
  server.on("/api/history", sendHistory);
  server.on("/api/button", HTTP_POST, pressButton);
  server.on("/api/alerts", HTTP_GET, sendAlertSettings);
  server.on("/api/alerts", HTTP_PUT, saveAlertSettings);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();

  lastSampleMs = millis();
}

void loop() {
  server.handleClient();
  pollButtons();
  watchWifi();

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
    checkAlerts();
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
  if (millis() < lcdHoldUntil) return;   // a notice is on the screen
  for (int i = 0; i < 2; i++) {
    String line = "Sensor " + String(i + 1) + " ";
    if (!shown[i]) line += "off";                       // the words requirement 4 asks for
    else if (isnan(tempC[i])) line += "error";          // requirement 4d, the probe is silent
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
      lcdHoldUntil = 0;   // requirement 4a wants the answer on screen inside 20 ms,
      updateLcd();        // so a press clears whatever notice is holding the screen
    }
  }
}

// ---- the joined network ----------------------------------------------------

// Runs every loop. The first time the box joins WIFI_SSID it prints the
// address, answers to thermo-box.local, and asks an NTP server for the time
// so the mail carries a real date.
void watchWifi() {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up == wifiUp) return;
  wifiUp = up;
  if (!up) {
    Serial.println("Lost the mail network, trying again");
    return;
  }
  String ip = WiFi.localIP().toString();
  Serial.print("Joined ");
  Serial.print(WIFI_SSID);
  Serial.print(", page at http://");
  Serial.print(ip);
  Serial.print(" or http://");
  Serial.print(HOSTNAME);
  Serial.println(".local");
  if (!mdnsStarted) mdnsStarted = MDNS.begin(HOSTNAME);
  configTime(0, 0, "pool.ntp.org");
  lcdLine(0, String("on ") + WIFI_SSID);
  lcdLine(1, ip);
  lcdHoldUntil = millis() + 3000;
}

// ---- alerts ----------------------------------------------------------------

void loadAlerts() {
  store.begin("alerts", true);
  alerts.email = store.getString("email", "");
  alerts.maxC = store.getFloat("max", alerts.maxC);
  alerts.minC = store.getFloat("min", alerts.minC);
  alerts.maxMessage = store.getString("maxMessage", alerts.maxMessage);
  alerts.minMessage = store.getString("minMessage", alerts.minMessage);
  store.end();
}

void saveAlerts() {
  store.begin("alerts", false);
  store.putString("email", alerts.email);
  store.putFloat("max", alerts.maxC);
  store.putFloat("min", alerts.minC);
  store.putString("maxMessage", alerts.maxMessage);
  store.putString("minMessage", alerts.minMessage);
  store.end();
}

// Once a second, after the probes are read. A sensor that has already sent
// stays quiet until its reading is back inside the band by HYSTERESIS_C.
void checkAlerts() {
  if (alerts.email.length() == 0) return;
  for (int i = 0; i < 2; i++) {
    float t = tempC[i];
    if (isnan(t)) continue;
    if (latched[i] == ABOVE && t < alerts.maxC - HYSTERESIS_C) latched[i] = NONE;
    if (latched[i] == BELOW && t > alerts.minC + HYSTERESIS_C) latched[i] = NONE;
    if (latched[i] != NONE) continue;
    if (t > alerts.maxC) latched[i] = ABOVE;
    else if (t < alerts.minC) latched[i] = BELOW;
    else continue;
    sendAlert(i, latched[i], t);
  }
}

// Same subject and body the server sends.
void sendAlert(int i, Latch kind, float t) {
  bool above = kind == ABOVE;
  float limit = above ? alerts.maxC : alerts.minC;
  String subject = "Thermometer alert: sensor " + String(i + 1) +
                   (above ? " above " : " below ") + limitText(limit) + " C";
  String body = (above ? alerts.maxMessage : alerts.minMessage) + "\r\n\r\n" +
                "Sensor " + String(i + 1) + " read " + String(t, 1) + " C (" +
                String(t * 9 / 5 + 32, 1) + " F), " +
                (above ? "above the maximum" : "below the minimum") +
                " of " + limitText(limit) + " C.\r\n";

  sentAny = true;
  sentAt = time(nullptr);
  sentSensor = i + 1;
  sentKind = kind;
  sentTo = alerts.email;
  sentOk = sendMail(alerts.email, subject, body);
}

// 30 -> "30", 30.5 -> "30.5"
String limitText(float v) {
  String s = String(v, 1);
  if (s.endsWith(".0")) s.remove(s.length() - 2);
  return s;
}

// One message through Gmail. This blocks for a few seconds (TLS handshake,
// then the SMTP conversation); nothing else runs until it returns.
bool sendMail(const String &to, const String &subject, const String &body) {
  if (!wifiUp) {
    Serial.println("Alert not sent: not on the mail network");
    return false;
  }
  if (strlen(MAIL_ADDRESS) == 0 || strlen(MAIL_APP_PASSWORD) == 0) {
    Serial.println("Alert not sent: MAIL_ADDRESS or MAIL_APP_PASSWORD is empty in secrets.h");
    return false;
  }
  Serial.print("Sending mail to ");
  Serial.println(to);

  sslClient.setInsecure();   // the board has no root certificate store
  bool ok = false;
  if (smtp.connect("smtp.gmail.com", 465, smtpStatus) &&
      smtp.authenticate(MAIL_ADDRESS, MAIL_APP_PASSWORD, readymail_auth_password)) {
    SMTPMessage msg;
    msg.headers.add(rfc822_from, String("Thermo box <") + MAIL_ADDRESS + ">");
    msg.headers.add(rfc822_to, to);
    msg.headers.add(rfc822_subject, subject);
    msg.text.body(body);
    if (time(nullptr) > 100000) msg.timestamp = time(nullptr);   // only once NTP has answered
    ok = smtp.send(msg);
  }
  smtp.stop();
  Serial.println(ok ? "Mail sent" : "Mail failed");
  return ok;
}

void smtpStatus(SMTPStatus status) {
  Serial.println(status.text);
}

// ---- web -------------------------------------------------------------------

void sendPage() {
  server.send_P(200, "text/html", PAGE);
}

void sendJson(const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void reject(const char *why) {
  server.send(400, "application/json", String("{\"error\":\"") + why + "\"}");
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

String alertsJson() {
  JsonDocument doc;
  doc["email"] = alerts.email;
  doc["max"] = alerts.maxC;
  doc["min"] = alerts.minC;
  doc["maxMessage"] = alerts.maxMessage;
  doc["minMessage"] = alerts.minMessage;
  if (!sentAny) {
    doc["lastSent"] = nullptr;
  } else {
    JsonObject last = doc["lastSent"].to<JsonObject>();
    last["t"] = (uint64_t)sentAt * 1000;   // milliseconds, what the page's Date() wants
    last["sensor"] = sentSensor;
    last["kind"] = sentKind == ABOVE ? "max" : "min";
    last["ok"] = sentOk;
    last["to"] = sentTo;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void sendAlertSettings() {
  sendJson(alertsJson());
}

// The page sends the number fields as text. False when it is not a number.
bool readNumber(JsonVariant v, float &out) {
  if (v.is<float>()) {
    out = v.as<float>();
    return true;
  }
  String s = v.as<String>();
  s.trim();
  if (s.length() == 0) return false;
  char *end;
  out = strtod(s.c_str(), &end);
  return *end == '\0';
}

// Body is any subset of the five settings. Saving rearms both sensors.
void saveAlertSettings() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    reject("body must be JSON");
    return;
  }
  AlertConfig next = alerts;
  float n;
  if (!doc["email"].isNull()) {
    next.email = doc["email"].as<String>();
    next.email.trim();
  }
  if (!doc["max"].isNull()) {
    if (!readNumber(doc["max"], n)) { reject("max must be a number"); return; }
    next.maxC = n;
  }
  if (!doc["min"].isNull()) {
    if (!readNumber(doc["min"], n)) { reject("min must be a number"); return; }
    next.minC = n;
  }
  if (!doc["maxMessage"].isNull()) next.maxMessage = doc["maxMessage"].as<String>();
  if (!doc["minMessage"].isNull()) next.minMessage = doc["minMessage"].as<String>();
  if (next.maxC <= next.minC) {
    reject("max must be greater than min");
    return;
  }
  alerts = next;
  saveAlerts();
  latched[0] = latched[1] = NONE;
  sendJson(alertsJson());
}
