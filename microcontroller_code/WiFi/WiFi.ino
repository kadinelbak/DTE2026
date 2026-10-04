/*
  ESP32 button -> Apple Watch notification (with LED feedback)
  ------------------------------------------------------------
  Press the button and your Apple Watch buzzes with a notification.
  Uses ntfy.sh (free, no account) over WiFi: install the "ntfy" app on your
  iPhone and subscribe to NTFY_TOPIC. iPhone notifications mirror to the Watch.

  LEDs:
    Yellow (STATUS_LED)  blinking = connecting to WiFi, solid = sending
    Green  (OK_LED)      flashes once = notification sent
    Red    (ERR_LED)     flashes 3x   = send failed (check WiFi / topic)

  Wiring (resistors are 220-330 ohm):
    Button:      one leg -> GPIO 13, other leg -> GND   (internal pull-up, no resistor needed)
    Yellow LED:  GPIO 25 -> resistor -> LED long leg; LED short leg -> GND
    Green LED:   GPIO 26 -> resistor -> LED long leg; LED short leg -> GND
    Red LED:     GPIO 27 -> resistor -> LED long leg; LED short leg -> GND

  Board:   any ESP32 (Tools > Board > esp32 > "ESP32 Dev Module")
  Library: all built into the ESP32 Arduino core
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ---- Change these ----
#include "secrets.h"   // WIFI_SSID and WIFI_PASS (copy secrets.example.h to secrets.h)
const char *NTFY_TOPIC = "dte-esp32-change-me-8f3k2";      // ESP32 -> phone. Make it random; anyone who knows it can read it
const char *NTFY_CMD_TOPIC = "dte-esp32-change-me-8f3k2-cmd";  // phone -> ESP32
 
#define BUTTON_PIN  13
#define STATUS_LED  25
#define OK_LED      26
#define ERR_LED     27
 
#define DEBOUNCE_MS 50
#define COOLDOWN_MS 2000   // ignore presses for 2 s after sending (ntfy rate-limits spam)
#define POLL_MS     3000   // how often to check for commands from the phone
 
int pressCount = 0;
int lastReading = HIGH;
int buttonState = HIGH;
unsigned long lastChange = 0;
unsigned long lastSent = 0;
unsigned long lastPoll = 0;
String lastCmdId = "";     // id of the last command handled, so each runs once
 
void flash(int pin, int times, int ms) {
  int before = digitalRead(pin);   // restore afterwards, so a phone-set "on" stays on
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH); delay(ms);
    digitalWrite(pin, LOW);  delay(ms);
  }
  digitalWrite(pin, before);
}
 
void listNetworks() {
  // Prints every WiFi network the ESP32 can see. If yours isn't listed, it's
  // probably 5 GHz only (ESP32 needs 2.4 GHz) or out of range.
  Serial.println("Networks the ESP32 can see:");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Serial.printf("  \"%s\"  signal %d dBm  %s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_WPA2_ENTERPRISE ? "(enterprise login, won't work)" : "");
  }
  if (n == 0) Serial.println("  (none found)");
  WiFi.scanDelete();
}
 
bool wifiStarted = false;
 
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
 
  if (!wifiStarted) {
    Serial.printf("Connecting to WiFi \"%s\"", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);               // phone hotspots often drop ESP32s that use WiFi power-saving
    WiFi.begin(WIFI_SSID, WIFI_PASS);   // only called once; calling it again mid-attempt causes "sta is connecting"
    wifiStarted = true;
  } else {
    Serial.print("WiFi dropped, reconnecting");
    WiFi.reconnect();
  }
 
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));   // blink while connecting
    delay(250);
    Serial.print(".");
  }
  digitalWrite(STATUS_LED, LOW);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" connected, IP ");
    Serial.println(WiFi.localIP());
  } else {
    // 1 = network not found, 4 = connect failed (usually wrong password), 6 = disconnected
    Serial.printf(" FAILED (status %d)\n", WiFi.status());
    WiFi.disconnect();   // stop trying so the scan below can run
    delay(200);
    listNetworks();
  }
}
 
bool sendNotification(const String &title, const String &message) {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) return false;
 
  digitalWrite(STATUS_LED, HIGH);
  WiFiClientSecure client;
  client.setInsecure();               // skips certificate check; fine for a prototype
  HTTPClient http;
  http.begin(client, String("https://ntfy.sh/") + NTFY_TOPIC);
  http.addHeader("Title", title);
  http.addHeader("Priority", "high");  // stronger buzz on the watch
  http.addHeader("Tags", "point_up");  // shows an emoji on the notification
  // Buttons on the notification that send a command back to the ESP32.
  String cmdUrl = String("https://ntfy.sh/") + NTFY_CMD_TOPIC;
  http.addHeader("Actions", "http, LED on, " + cmdUrl + ", body=on, clear=true; "
                            "http, LED off, " + cmdUrl + ", body=off, clear=true");
  int code = http.POST(message);
  http.end();
  digitalWrite(STATUS_LED, LOW);
 
  Serial.printf("ntfy -> HTTP %d\n", code);
  return code == 200;
}
 
// Pulls the value of "key":"..." out of one line of ntfy's JSON.
String jsonField(const String &line, const String &key) {
  String tag = "\"" + key + "\":\"";
  int start = line.indexOf(tag);
  if (start < 0) return "";
  start += tag.length();
  int end = line.indexOf('"', start);
  return end < 0 ? "" : line.substring(start, end);
}
 
void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  Serial.printf("Command from phone: \"%s\"\n", cmd.c_str());
 
  if (cmd == "on")         digitalWrite(OK_LED, HIGH);
  else if (cmd == "off")   digitalWrite(OK_LED, LOW);
  else if (cmd == "blink") flash(OK_LED, 5, 150);
  else                     flash(STATUS_LED, 2, 150);   // unknown command: just acknowledge
 
  sendNotification("ESP32", "ESP32 got: " + cmd);
}
 
// Asks ntfy for any new messages on the command topic.
void pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return;
 
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String since = lastCmdId.length() ? lastCmdId : "10s";
  http.begin(client, String("https://ntfy.sh/") + NTFY_CMD_TOPIC + "/json?poll=1&since=" + since);
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  if (code != 200) return;
 
  // One JSON object per line, oldest first.
  int pos = 0;
  while (pos < (int)body.length()) {
    int nl = body.indexOf('\n', pos);
    if (nl < 0) nl = body.length();
    String line = body.substring(pos, nl);
    pos = nl + 1;
 
    if (jsonField(line, "event") != "message") continue;
    String id = jsonField(line, "id");
    if (id.length() == 0 || id == lastCmdId) continue;
    lastCmdId = id;
    handleCommand(jsonField(line, "message"));
  }
}
 
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);   // reads LOW when pressed
  pinMode(STATUS_LED, OUTPUT);
  pinMode(OK_LED, OUTPUT);
  pinMode(ERR_LED, OUTPUT);
 
  // Quick LED self-test so you know all three are wired right.
  flash(STATUS_LED, 1, 150); flash(OK_LED, 1, 150); flash(ERR_LED, 1, 150);
 
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) flash(OK_LED, 2, 100);
  else flash(ERR_LED, 3, 150);
}
 
void loop() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastReading) lastChange = millis();
  lastReading = reading;
 
  if (millis() - lastChange > DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
 
    if (buttonState == LOW && millis() - lastSent > COOLDOWN_MS) {   // just pressed
      pressCount++;
      lastSent = millis();
      Serial.printf("Button pressed (#%d), sending...\n", pressCount);
 
      if (sendNotification("Button pressed", "Press #" + String(pressCount))) {
        flash(OK_LED, 1, 400);
      } else {
        flash(ERR_LED, 3, 150);
      }
    }
  }
 
  // Check for commands from the phone every few seconds.
  // (Each check takes about half a second, so a very quick button tap during one may be missed.)
  if (millis() - lastPoll > POLL_MS) {
    lastPoll = millis();
    pollCommands();
  }
}