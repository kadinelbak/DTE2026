const int N = 20;          // 1200 Hz / 60 Hz = 20 samples per cycle
int buf[N];
int idx = 0, cnt = 0;
float env = 0;
unsigned long t;

void setup() {
  Serial.begin(115200);
  t = micros();
}

void loop() {
  if (micros() - t >= 833) {      // 1200 Hz sampling
    t += 833;
    int x = analogRead(A0);
    int y = x - buf[idx];         // cancel 60 Hz
    buf[idx] = x;
    idx = (idx + 1) % N;
    env = 0.98 * env + 0.02 * abs(y);  // envelope
    if (++cnt >= 10) {            // print every 10th sample
      cnt = 0;
      Serial.println(env);
    }
  }
}