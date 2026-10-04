#include <EEPROM.h>

// ---------- pins ----------
const int SENSOR_PIN = A1;     // outside stretch sensor
const int LED_BLUE   = 8;      // warning
const int LED_RED    = 9;      // too far

// ---------- circuit ----------
const float VCC     = 5.0;
const float R_FIXED = 4000000.0;   // fixed resistor on A1, in ohms: set to yours
const unsigned long SAMPLE_MS = 500;

// ---------- warning limits ----------
// Used once you've calibrated to degrees:
const float WARN_DEG   = 15.0;     // blue on above this
const float DANGER_DEG = 25.0;     // red on above this
// Used before calibration (percent change from neutral):
const float WARN_PCT   = 10.0;
const float DANGER_PCT = 20.0;
const float HYSTERESIS = 0.1;      // must drop 10% below a limit to switch off (stops flicker)

float baseV = 0, degPerPct = 0, smoothV = 0;
int level = 0;                      // 0 ok, 1 warning, 2 danger

float readVolts() {
  analogRead(SENSOR_PIN);
  delayMicroseconds(200);
  long sum = 0;
  for (int i = 0; i < 20; i++) sum += analogRead(SENSOR_PIN);
  return (sum / 20.0) * VCC / 1023.0;
}

float pctChange(float v) { return (v - baseV) / baseV * 100.0; }

// Stretch sensor resistance from the A1 voltage (sensor is the bottom half of the divider)
float sensorR(float v) {
  if (v < 0.005 || v > VCC - 0.005) return -1;   // -1 = out of range (shorted or open)
  return R_FIXED * v / (VCC - v);
}

// Picks the warning level from a value and its two limits, with hysteresis
int checkLevel(float value, float warn, float danger) {
  value = fabs(value);                         // too far in either direction counts
  if (value > danger) return 2;
  if (level == 2 && value > danger * (1 - HYSTERESIS)) return 2;
  if (value > warn) return 1;
  if (level >= 1 && value > warn * (1 - HYSTERESIS)) return 1;
  return 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  // LED test: each lights briefly so you know they're wired right
  digitalWrite(LED_BLUE, HIGH); delay(300); digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_RED,  HIGH); delay(300); digitalWrite(LED_RED,  LOW);

  EEPROM.get(0, degPerPct);
  if (isnan(degPerPct) || degPerPct == 0 || fabs(degPerPct) > 1000) degPerPct = 0;

  Serial.println("Hold ankle in neutral, zeroing in 3 s...");
  digitalWrite(LED_BLUE, HIGH);                // blue on while zeroing
  delay(3000);
  baseV = readVolts();
  smoothV = baseV;
  digitalWrite(LED_BLUE, LOW);

  Serial.print("Resting sensor resistance: ");
  Serial.print(sensorR(baseV), 0);
  Serial.println(" ohms");

  Serial.println("Commands: z = re-zero (ankle neutral), or type an angle you're holding, e.g. 20");
  if (degPerPct == 0) Serial.println("Not calibrated: warnings use % change until you calibrate.");
}

void loop() {
  // --- commands ---
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "z") {
      baseV = readVolts(); smoothV = baseV;
      Serial.print(">> Zeroed. Resting resistance: ");
      Serial.print(sensorR(baseV), 0);
      Serial.println(" ohms");
    } else if (cmd.length() > 0) {
      float deg = cmd.toFloat();
      float p = pctChange(smoothV);
      if (deg != 0 && fabs(p) > 0.2) {
        degPerPct = deg / p;
        EEPROM.put(0, degPerPct);
        Serial.print(">> Calibrated: "); Serial.print(degPerPct, 3); Serial.println(" deg per 1% (saved).");
      } else {
        Serial.println(">> Not enough change from neutral; roll further and try again.");
      }
    }
  }

  // --- reading ---
  smoothV = 0.7 * smoothV + 0.3 * readVolts();
  float p = pctChange(smoothV);
  float angle = p * degPerPct;

  // --- warning level ---
  if (degPerPct != 0) level = checkLevel(angle, WARN_DEG, DANGER_DEG);
  else                level = checkLevel(p, WARN_PCT, DANGER_PCT);

  static bool blink = false;
  blink = !blink;
  digitalWrite(LED_BLUE, level == 1);
  digitalWrite(LED_RED,  level == 2 && blink);   // red blinks when too far

  // --- print ---
  float r = sensorR(smoothV);
  float r0 = sensorR(baseV);
  Serial.print("V: ");        Serial.print(smoothV, 3);
  Serial.print("  R: ");
  if (r < 0) Serial.print("out of range");
  else {
    Serial.print(r, 0); Serial.print(" ohms");
    if (r0 > 0) {                                  // resistance change from rest
      Serial.print(" (");
      Serial.print((r - r0) / r0 * 100.0, 1);
      Serial.print("%)");
    }
  }
  Serial.print("  change: "); Serial.print(p, 2); Serial.print("%");
  if (degPerPct != 0) { Serial.print("  angle: "); Serial.print(angle, 1); Serial.print(" deg"); }
  Serial.print("  ");
  Serial.println(level == 2 ? "DANGER" : level == 1 ? "warning" : "ok");

  delay(SAMPLE_MS);
}