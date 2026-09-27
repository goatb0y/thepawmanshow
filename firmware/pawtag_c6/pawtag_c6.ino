/*
  PAWTAG — Glyph C6 (ESP32-C6) + MPU6050 + button
  Samples the IMU at 50 Hz, computes 1-second features and POSTs them to the
  Pawman server. The SERVER does the activity classification, so thresholds
  can be tuned live without reflashing.

  Arduino IDE: install "esp32" by Espressif (v3.x), Board = "ESP32C6 Dev Module",
  Tools > USB CDC On Boot = Enabled (so Serial works over USB-C).

  Wiring (Glyph C6 -> MPU6050):
    3V3 -> VCC,  GND -> GND,  GPIO4 -> SDA,  GPIO5 -> SCL
  Button: one leg -> GPIO3, other leg -> GND  (internal pull-up used)
          The onboard BOOT button (GPIO9) also works as the same button.
    short press  = "mark this moment"
    hold 2 s     = "end of day" -> server generates the Pawman episode
  Battery: Glyph C6 has LiPo pads on the back; or feed 5V from the 134N3P.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>

// ======== EDIT THESE ========
const char* WIFI_SSID = "PAWMAN";
const char* WIFI_PASS = "pawman123";
const char* SERVER    = "http://192.168.1.50:8000";
const char* DEVICE_ID = "pawtag-1";
// ============================

#define SDA_PIN   4
#define SCL_PIN   5
#define BTN_PIN   3
#define BOOT_PIN  9
#define MPU_ADDR  0x68   // 0x69 if AD0 is tied high

const int   HZ = 50;
const int   N  = HZ;     // 1-second window
float mag[N], gyr[N];
int   idx = 0;
uint32_t lastSample = 0;
String pendingButton = "";

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}

bool mpuInit() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) return false;
  mpuWrite(0x6B, 0x00);  // wake up
  mpuWrite(0x1C, 0x10);  // accel ±8 g  (4096 LSB/g) — dogs jump hard
  mpuWrite(0x1B, 0x10);  // gyro ±1000 dps (32.8 LSB/dps)
  mpuWrite(0x1A, 0x03);  // DLPF ~44 Hz
  return true;
}

bool mpuRead(float& a, float& g) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, 14) != 14) return false;
  int16_t v[7];
  for (int i = 0; i < 7; i++) v[i] = (Wire.read() << 8) | Wire.read();
  float ax = v[0] / 4096.0, ay = v[1] / 4096.0, az = v[2] / 4096.0;
  float gx = v[4] / 32.8,  gy = v[5] / 32.8,  gz = v[6] / 32.8;
  a = sqrtf(ax*ax + ay*ay + az*az);
  g = sqrtf(gx*gx + gy*gy + gz*gz);
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); Serial.print("."); }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK " + WiFi.localIP().toString() : " FAILED");
}

void checkButton() {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  bool down = digitalRead(BTN_PIN) == LOW || digitalRead(BOOT_PIN) == LOW;
  if (down && !wasDown) downAt = millis();
  if (!down && wasDown) {
    uint32_t held = millis() - downAt;
    if (held > 2000)      pendingButton = "end_of_day";
    else if (held > 40)   pendingButton = "mark";
  }
  wasDown = down;
}

void sendWindow() {
  float sum = 0, sq = 0, gsum = 0, peak = 0, gpeak = 0;
  for (int i = 0; i < N; i++) {
    sum += mag[i]; sq += mag[i] * mag[i]; gsum += gyr[i];
    if (mag[i] > peak) peak = mag[i];
    if (gyr[i] > gpeak) gpeak = gyr[i];
  }
  float mean = sum / N;
  float stdv = sqrtf(fmaxf(sq / N - mean * mean, 0));
  float gmean = gsum / N;

  char body[256];
  snprintf(body, sizeof(body),
    "{\"device\":\"%s\",\"accel_mean\":%.3f,\"accel_std\":%.3f,\"accel_peak\":%.2f,"
    "\"gyro_mean\":%.1f,\"gyro_peak\":%.1f,\"button\":\"%s\"}",
    DEVICE_ID, mean, stdv, peak, gmean, gpeak, pendingButton.c_str());
  Serial.println(body);

  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); return; }
  HTTPClient http;
  http.setTimeout(2000);
  http.begin(String(SERVER) + "/imu");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body, strlen(body));
  if (code == 200) pendingButton = "";   // only clear once delivered
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  while (!mpuInit()) { Serial.println("MPU6050 not found — check SDA=4 SCL=5, 3V3, GND"); delay(1000); }
  Serial.println("MPU6050 OK");
  connectWiFi();
}

void loop() {
  checkButton();
  if (millis() - lastSample >= 1000 / HZ) {
    lastSample = millis();
    float a, g;
    if (mpuRead(a, g)) {
      mag[idx] = a; gyr[idx] = g; idx++;
      if (idx >= N) { idx = 0; sendWindow(); }
    }
  }
}
