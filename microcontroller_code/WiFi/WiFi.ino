/*
  ESP32 button -> Apple Watch "danger mode" alert with stiffness control
  ----------------------------------------------------------------------
  Press the button and your Apple Watch buzzes with "Danger mode approached"
  and asks whether to increase stiffness. The notification has two buttons,
  "Increase stiffness" and "Decrease stiffness", which send a command back to
  the ESP32 and change the stiffness level (0 to MAX_STIFFNESS).
  Uses ntfy.sh (free, no account) over WiFi: install the "ntfy" app on your
  iPhone and subscribe to NTFY_TOPIC. iPhone notifications mirror to the Watch.

  LEDs show the stiffness level (one LED stays on):
    Green  (OK_LED)      = low stiffness     (level 0-1)
    Yellow (STATUS_LED)  = medium stiffness  (level 2-3)
    Red    (ERR_LED)     = high stiffness    (level 4-5)
  Other LED signals:
    Yellow blinking      = connecting to WiFi, solid while sending
    New level's LED      flashes 3x when the stiffness changes
    Red flashes 3x       = send failed (check WiFi / topic)
    Current LED flashes 1x when stiffness is already at max / min

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
#define MAX_STIFFNESS 5    // stiffness levels go from 0 (softest) to this
#define START_STIFFNESS 2
 
int pressCount = 0;
int stiffness = START_STIFFNESS;
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
 
// Which LED shows the current stiffness: green = low, yellow = medium, red = high.
int stiffnessLed() {
  if (stiffness <= 1) return OK_LED;
  if (stiffness <= 3) return STATUS_LED;
  return ERR_LED;
}

// Turns on only the LED for the current stiffness.
void showStiffness() {
  digitalWrite(OK_LED, LOW);
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(ERR_LED, LOW);
  digitalWrite(stiffnessLed(), HIGH);
}

// Sets a new stiffness level and shows it on the LEDs.
// This is where the damper / actuator output goes once it is wired up
// (e.g. set the MR coil PWM duty from the level).
void setStiffness(int level) {
  stiffness = level;
  Serial.printf("Stiffness now %d/%d\n", stiffness, MAX_STIFFNESS);
  showStiffness();
  flash(stiffnessLed(), 3, 150);   // flash the new colour so the change is obvious
}

// Sends a notification. withStiffnessButtons adds the increase / decrease buttons.
bool sendNotification(const String &title, const String &message,
                      const char *priority, const char *tags, bool withStiffnessButtons) {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) return false;
 
  digitalWrite(STATUS_LED, HIGH);
  WiFiClientSecure client;
  client.setInsecure();               // skips certificate check; fine for a prototype
  HTTPClient http;
  http.begin(client, String("https://ntfy.sh/") + NTFY_TOPIC);
  http.addHeader("Title", title);
  http.addHeader("Priority", priority);  // "urgent"/"high" give a stronger buzz on the watch
  http.addHeader("Tags", tags);          // shows an emoji on the notification
  if (withStiffnessButtons) {
    // Buttons on the notification that send a command back to the ESP32.
    String cmdUrl = String("https://ntfy.sh/") + NTFY_CMD_TOPIC;
    http.addHeader("Actions", "http, Increase stiffness, " + cmdUrl + ", body=up, clear=true; "
                              "http, Decrease stiffness, " + cmdUrl + ", body=down, clear=true");
  }
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
 
  if (cmd == "up" || cmd == "down") {
    int target = stiffness + (cmd == "up" ? 1 : -1);
    if (target < 0 || target > MAX_STIFFNESS) {
      flash(stiffnessLed(), 1, 300);
      sendNotification("Stiffness unchanged",
                       String("Already at ") + (target < 0 ? "minimum" : "maximum") +
                       " (" + stiffness + "/" + MAX_STIFFNESS + ")",
                       "default", "warning", true);
      showStiffness();
      return;
    }
    setStiffness(target);
    sendNotification(cmd == "up" ? "Stiffness increased" : "Stiffness decreased",
                     String("Stiffness now ") + stiffness + "/" + MAX_STIFFNESS,
                     "default", cmd == "up" ? "arrow_up" : "arrow_down", true);
    showStiffness();   // sending blinks the yellow LED, so put the stiffness colour back
  }
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
  showStiffness();
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
 
      String msg = String("Danger mode approached. Increase stiffness? (now ") +
                   stiffness + "/" + MAX_STIFFNESS + ")";
      if (sendNotification("Danger mode approached", msg, "urgent", "warning", true)) {
        flash(stiffnessLed(), 1, 400);
      } else {
        flash(ERR_LED, 3, 150);
      }
      showStiffness();
    }
  }
 
  // Check for commands from the phone every few seconds.
  // (Each check takes about half a second, so a very quick button tap during one may be missed.)
  if (millis() - lastPoll > POLL_MS) {
    lastPoll = millis();
    pollCommands();
  }
}