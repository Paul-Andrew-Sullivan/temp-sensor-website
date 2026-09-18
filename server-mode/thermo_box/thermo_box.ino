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

  The website lives on the server, not here. The box joins the hotspot named
  in secrets.h and posts its readings to INGEST_URL twice a second, and the
  reply carries the button states the web page wants. Requirement 5a.ii asks
  the computer to read "no data available" while the box is switched off, and
  requirement 6 asks for the readings back within ten seconds of switching it
  on; a page served by the box could do neither, because the thing serving it
  is the thing that is off. The server is up either way, so it can.

  The box keeps its own 300 second history for the mail and the LCD, but the
  graph on the computer is drawn from the server's copy, which survives the
  box being switched off. The seconds the box was away stay empty there, which
  is what requirement 5c.v asks for.

  Alerts: the box sends them, because the box is the part that has to be
  powered. Requirement 7 wants the limits, the messages and the address
  changed from the computer, so the page saves them to the server and the
  box reads them back every ten seconds, keeping a copy in flash for when
  the server cannot be reached. One message per crossing, per sensor,
  rearmed when the reading comes back inside the band by half a degree.
  A send takes a few seconds and the loop waits for it, so the LCD and the
  page pause briefly.

  Wiring, from the schematic (2026-09-09):
    DS18B20 x2  DQ -> GPIO32, 4.7k pull-up to 3V3
    SW1         GPIO34, 10k pull-up, 1k series, 0.1uF, pressed = LOW
    SW2         GPIO35, same
    LCD HY1602E RS=GPIO25  E=GPIO26  D4=GPIO18  D5=GPIO19  D6=GPIO23  D7=GPIO27
                R/W to ground, 4-bit mode, DB0..DB3 left open
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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

const char *HOSTNAME = "thermo-box";     // http://thermo-box.local on the joined network

// Requirement 5b gives a press made on the web page under a second to reach the
// box, so the box talks to the server twice a second. After a failed post it
// waits longer, so an unreachable server cannot stall the sampling loop.
const unsigned long POST_EVERY_MS = 500;
const unsigned long POST_BACKOFF_MS = 5000;
const unsigned long ALERT_FETCH_MS = 10000;   // how often the box rereads the settings

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

volatile bool pressPending[2] = {false, false};   // set by the button interrupt
volatile unsigned long buttonChangedMs[2] = {0, 0};

unsigned long lcdHoldUntil = 0;    // readings stay off the LCD until this time
volatile bool lcdDirty = false;    // the network core asks for a redraw this way
volatile bool haveNotice = false;  // an address to show, handed over from that core
char noticeLine[17] = "";
bool wifiUp = false;               // joined WIFI_SSID and holding an address
bool mdnsStarted = false;

unsigned long nextPostMs = 0;      // when the next report to the server is due
unsigned long nextAlertFetchMs = 0;
WiFiClientSecure postClient;       // kept apart from the one the mail uses
// The settings fetch gets its own connection. Sharing one with the report meant
// each fetch closed the report's connection, and both then paid for a fresh TLS
// handshake: a 1.7 s stall every ten seconds, right on requirement 5b's path.
WiFiClientSecure alertClient;
// Global, not a local in the fetch. A local HTTPClient is destroyed when the
// function returns, which closes the connection whatever setReuse says, so
// every fetch paid for a fresh TLS handshake: about 1.9 s on a phone hotspot.
HTTPClient alertHttp;
HTTPClient http;                   // holds the connection open between posts

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

void IRAM_ATTR onButton(void *arg);   // the IDE writes no prototype for this one
void networkLoop(void *arg);

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN[0], INPUT);
  pinMode(BUTTON_PIN[1], INPUT);
  // FALLING: the pins idle high on their external pull-ups and a press pulls low.
  attachInterruptArg(BUTTON_PIN[0], onButton, (void *)0, FALLING);
  attachInterruptArg(BUTTON_PIN[1], onButton, (void *)1, FALLING);

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

  postClient.setInsecure();   // no CA bundle on the board; the token is the check
  alertClient.setInsecure();
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // returns at once; watchWifi() sees it join
  Serial.println();
  Serial.print("Joining ");
  Serial.println(WIFI_SSID);
  lcdLine(0, "Thermo box");
  lcdLine(1, String("joining ") + WIFI_SSID);
  // No delay here on purpose. Requirement 6 gives the box ten seconds from
  // being switched on to data on the screen, and a pause spends them.

  server.on("/", sendPage);
  server.on("/api/state", sendState);
  server.on("/api/history", sendHistory);
  server.on("/api/button", HTTP_POST, pressButton);
  server.on("/api/alerts", HTTP_GET, sendAlertSettings);
  server.on("/api/alerts", HTTP_PUT, saveAlertSettings);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();

  xTaskCreatePinnedToCore(networkLoop, "net", 16384, NULL, 1, NULL, 0);

  lastSampleMs = millis();
}

void loop() {
  server.handleClient();
  pollButtons();
  if (haveNotice) {                      // an address arrived from the other core
    haveNotice = false;
    lcdLine(0, String("on ") + WIFI_SSID);
    lcdLine(1, noticeLine);
    lcdHoldUntil = millis() + 3000;
  }
  if (lcdDirty) { lcdDirty = false; updateLcd(); }

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
// The pins interrupt rather than being read in the loop. Posting to the server
// blocks for a few hundred milliseconds, and a press that began and ended inside
// one of those was simply never seen: the pin went low and high again while the
// loop was waiting on the network. An interrupt catches it whatever the loop is
// doing. Debouncing happens here too, since the bounce is on the same edge.
void IRAM_ATTR onButton(void *arg) {
  int i = (int)(intptr_t)arg;
  unsigned long now = millis();
  if (now - buttonChangedMs[i] < 40) return;
  buttonChangedMs[i] = now;
  pressPending[i] = true;
}

void pollButtons() {
  for (int i = 0; i < 2; i++) {
    if (!pressPending[i]) continue;
    pressPending[i] = false;
    shown[i] = !shown[i];
    lcdHoldUntil = 0;   // requirement 4a wants the answer on screen quickly, so a
    updateLcd();        // press clears whatever notice is holding the screen
  }
}

// Everything that waits on the network runs on the other core. Requirement 4a
// allows about 20 ms between a press and the screen, and a single HTTPS post
// blocks for hundreds of milliseconds, so the loop cannot be the thing doing it.
// Core 1 keeps the buttons, the screen and the probes; core 0 does the talking.
void networkLoop(void *arg) {
  for (;;) {
    watchWifi();
    reportToServer();
    fetchAlertSettings();
    checkAlerts();            // sending mail blocks for seconds, so not on core 1
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---- reporting to the server -----------------------------------------------

// Sends the two readings and the two button states, and takes back the button
// states the web page wants. When they differ the box obeys the page, which is
// how requirement 5b lets the computer press a button that is not in the room.
//
// The post blocks the loop while it runs, so the timeout is short and a failure
// backs off. Keeping the connection open matters more than it looks: a TLS
// handshake costs this chip a second or two, far more than the gap between
// posts, so setReuse holds one connection open across them.
void reportToServer() {
  if (!wifiUp || millis() < nextPostMs) return;

  String body = "{\"s1\":" + numberOrNull(tempC[0]) +
                ",\"s2\":" + numberOrNull(tempC[1]) +
                ",\"b1\":" + trueOrFalse(shown[0]) +
                ",\"b2\":" + trueOrFalse(shown[1]) + "}";

  http.setReuse(true);
  http.setConnectTimeout(2000);
  http.setTimeout(2000);
  if (!http.begin(postClient, INGEST_URL)) {
    nextPostMs = millis() + POST_BACKOFF_MS;
    return;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Probe-Token", PROBE_TOKEN);

  unsigned long began = millis();
  int code = http.POST(body);
  unsigned long took = millis() - began;
  // The loop cannot watch anything while this runs, so say when it runs long.
  if (took > 200) { Serial.print("slow post: "); Serial.print(took); Serial.println(" ms"); }
  if (code == 200) {
    // Read the reply as JSON rather than looking for text in it. The server
    // writes {"b1": true}, with a space, and a search for "b1":true finds
    // nothing there and quietly turns both displays off twice a second.
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      bool want[2] = { doc["b1"] | shown[0], doc["b2"] | shown[1] };
      for (int i = 0; i < 2; i++) {
        if (want[i] == shown[i]) continue;
        shown[i] = want[i];        // the page pressed it, so the screen follows
        lcdHoldUntil = 0;
        lcdDirty = true;           // the loop core does the drawing
      }
    }
    nextPostMs = millis() + POST_EVERY_MS;
  } else {
    if (code == 401) Serial.println("Server refused the report: check PROBE_TOKEN");
    else {
      Serial.print("Report failed: ");
      Serial.println(code);
    }
    nextPostMs = millis() + POST_BACKOFF_MS;
  }
  http.end();
}

// Requirement 7 wants the limits, the messages and the address changed from the
// computer. The page saves them to the server, so the box reads them back from
// there and keeps its own copy in flash for when the server cannot be reached.
// The box is what sends the mail, because the box is what has to be powered.
void fetchAlertSettings() {
  if (!wifiUp || millis() < nextAlertFetchMs) return;
  nextAlertFetchMs = millis() + ALERT_FETCH_MS;

  String url = INGEST_URL;
  url.replace("/ingest", "/api/alerts");

  HTTPClient &get = alertHttp;
  get.setReuse(true);
  get.setConnectTimeout(2000);
  get.setTimeout(2000);
  if (!get.begin(alertClient, url)) return;
  unsigned long began = millis();
  int code = get.GET();
  unsigned long took = millis() - began;
  if (took > 200) { Serial.print("slow alert fetch: "); Serial.print(took); Serial.println(" ms"); }
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, get.getString())) {
      AlertConfig next;
      next.email = doc["email"] | "";
      next.maxC = doc["max"] | alerts.maxC;
      next.minC = doc["min"] | alerts.minC;
      next.maxMessage = doc["maxMessage"] | alerts.maxMessage;
      next.minMessage = doc["minMessage"] | alerts.minMessage;
      bool changed = next.email != alerts.email || next.maxC != alerts.maxC ||
                     next.minC != alerts.minC || next.maxMessage != alerts.maxMessage ||
                     next.minMessage != alerts.minMessage;
      if (changed) {
        alerts = next;
        saveAlerts();               // only on a change, so the flash is not worn out
        latched[0] = latched[1] = NONE;   // new limits, so judge them fresh
        Serial.println("Alert settings updated from the server");
      }
    }
  }
  get.end();
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
  ip.toCharArray(noticeLine, sizeof(noticeLine));
  haveNotice = true;
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
