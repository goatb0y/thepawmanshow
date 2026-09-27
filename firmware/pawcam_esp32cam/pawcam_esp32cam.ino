/*
  PAWCAM — ESP32-CAM (AI-Thinker, OV2640)
  Takes a JPEG and POSTs it to the Pawman server. The server replies with
  how many ms to wait before the next shot (faster when the dog is active).

  Arduino IDE: Board = "AI Thinker ESP32-CAM", PSRAM enabled (default).
  Flashing with CP2102:  CP2102 TX -> U0R, RX -> U0T, GND -> GND,
                         IO0 -> GND while flashing, press RST, upload,
                         then remove IO0-GND and press RST to run.
  Power the cam from a solid 5V (the 134N3P output) on the 5V pin,
  NOT from the CP2102's 3.3V pin.

  NO CAPACITOR? This sketch already does the software mitigations:
   - brownout detector disabled
   - lower WiFi TX power, lower camera clock, single frame buffer
  Also keep 5V/GND wires short and thick (5–8 cm), and twist them together.
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ======== EDIT THESE ========
const char* WIFI_SSID = "PAWMAN";          // your own hotspot/router
const char* WIFI_PASS = "pawman123";
const char* SERVER    = "http://192.168.1.50:8000";  // laptop IP running server
const char* DEVICE_ID = "pawcam-1";
// ============================

// AI-Thinker pin map
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_LED_PIN      4
#define RED_LED_PIN       33   // small red LED on the back, active LOW

uint32_t nextDelayMs = 4000;

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 10000000;          // 10 MHz: less current than 20 MHz
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = FRAMESIZE_VGA;     // 640x480 — enough for the story
  c.jpeg_quality = 12;                // 10-15 good; higher = smaller file
  c.fb_count     = 1;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&c) != ESP_OK) return false;

  sensor_t* s = esp_camera_sensor_get();
  // If the image is upside down on the harness, flip these:
  s->set_vflip(s, 0);
  s->set_hmirror(s, 0);
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setTxPower(WIFI_POWER_11dBm);  // lower TX power = smaller current spikes
  Serial.print("WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(300); Serial.print(".");
    digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN));
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK " + WiFi.localIP().toString() : " FAILED");
  digitalWrite(RED_LED_PIN, WiFi.status() == WL_CONNECTED ? LOW : HIGH);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // no capacitor: stop brownout resets
  Serial.begin(115200);
  pinMode(FLASH_LED_PIN, OUTPUT); digitalWrite(FLASH_LED_PIN, LOW);
  pinMode(RED_LED_PIN, OUTPUT);   digitalWrite(RED_LED_PIN, HIGH);

  if (!initCamera()) {
    Serial.println("Camera init FAILED — check ribbon cable / power. Rebooting.");
    delay(2000); ESP.restart();
  }
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); delay(1000); return; }

  // Throw away one stale frame so the picture is current
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) { Serial.println("capture failed"); delay(1000); return; }

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(String(SERVER) + "/frame");
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("X-Device", DEVICE_ID);
  int code = http.POST(fb->buf, fb->len);
  size_t sent = fb->len;
  esp_camera_fb_return(fb);

  if (code == 200) {
    long d = http.getString().toInt();          // server says when to shoot next
    if (d >= 500 && d <= 60000) nextDelayMs = d;
    Serial.printf("frame %u bytes -> next in %lu ms\n", sent, nextDelayMs);
  } else {
    Serial.printf("POST failed: %d\n", code);
    nextDelayMs = 3000;
  }
  http.end();
  delay(nextDelayMs);
}
