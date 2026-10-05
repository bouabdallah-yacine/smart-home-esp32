/*
 * ============================================================================
 *  Smart home — ESP32 + FreeRTOS (simulated on Wokwi)
 * ============================================================================
 *  Sensors  : PIR (presence), photoresistor (light level), DHT22 (temperature,
 *             humidity), slide potentiometer (simulates an MQ-2 gas sensor),
 *             switch (door), 4x4 keypad (alarm)
 *  Outputs  : dimmable lamp (PWM), heating relay, ventilation (blue LED),
 *             blinds (servo motor), siren (buzzer), OLED display
 *  Control  : web page served BY THE ESP32 (http://localhost:8180 with Wokwi)
 *             + commands in the serial monitor
 *
 *  All rules live in home.c (portable C, 24 tests on PC).
 *
 *  FreeRTOS tasks:
 *    taskSensors  10 Hz   sensor reading
 *    taskKeypad   50 Hz   matrix keypad scanning (debounced)
 *    taskLogic    10 Hz   automation rules → actuators
 *    taskDisplay   4 Hz   OLED display
 *    loop()               web server + serial commands
 * ============================================================================
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include "esp_arduino_version.h"
#include "home.h"
#include "web_page.h"

// --- Pin map ----------------------------------------------------------------
#define PIN_DHT     15
#define PIN_PIR     35
#define PIN_LDR     34
#define PIN_GAS     39
#define PIN_DOOR    36
#define PIN_LAMP    2
#define PIN_HEATER  4
#define PIN_FAN     18
#define PIN_SERVO   19
#define PIN_SIREN   23
static const uint8_t ROWS[4] = {13, 12, 14, 27};
static const uint8_t COLS[4] = {26, 25, 33, 32};
static const char KEYS[4][4] = {{'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}};

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
DHTesp dht;
WebServer server(80);

home_t home;
home_inputs_t inputs;                     // updated by taskSensors
SemaphoreHandle_t homeMutex;
QueueHandle_t keyQueue;

// ---------------------------------------------------------------------------
//  PWM: lamp (5 kHz), servo (50 Hz), siren (2 kHz) on 3 LEDC timers
// ---------------------------------------------------------------------------
static void pwmSetup(uint8_t pin, uint32_t freq, uint8_t bits, uint8_t ch) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(pin, freq, bits, ch);
#else
  ledcSetup(ch, freq, bits); ledcAttachPin(pin, ch);
#endif
}
static void pwmOut(uint8_t pin, uint8_t ch, uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)ch; ledcWrite(pin, duty);
#else
  (void)pin; ledcWrite(ch, duty);
#endif
}
static void servoWrite(int deg) {                    // 0.5 to 2.5 ms pulse every 20 ms
  uint32_t us = 500 + (uint32_t)deg * 2000 / 180;
  pwmOut(PIN_SERVO, 2, us * 65535 / 20000);
}

// ---------------------------------------------------------------------------
//  Sensors
// ---------------------------------------------------------------------------
static float readLux() {
  // Wokwi photoresistor formula (GAMMA 0.7, RL10 50 kΩ, 10 kΩ divider... at 3.3 V)
  float v = analogRead(PIN_LDR) / 4095.0f * 3.3f;
  if (v >= 3.29f) v = 3.29f;
  if (v < 0.001f) return 100000;
  float r = 2000.0f * v / (1.0f - v / 3.3f);
  return powf(50e3f * powf(10, 0.7f) / r, 1.0f / 0.7f);
}

void taskSensors(void *) {
  float temp = 21, hum = 50;
  uint32_t k = 0;
  for (;;) {
    if (k++ % 20 == 0) {                             // the DHT22 can be read at most every 2 s
      TempAndHumidity th = dht.getTempAndHumidity();
      if (dht.getStatus() == DHTesp::ERROR_NONE) { temp = th.temperature; hum = th.humidity; }
    }
    home_inputs_t in = {};
    in.motion = digitalRead(PIN_PIR);
    in.lux = readLux();
    in.temp_c = temp; in.hum_pct = hum;
    in.gas_ppm = analogRead(PIN_GAS) / 4095.0f * 5000.0f;   // slider: 0 to 5000 ppm
    in.door_open = digitalRead(PIN_DOOR);
    xSemaphoreTake(homeMutex, portMAX_DELAY);
    inputs = in;
    xSemaphoreGive(homeMutex);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ---------------------------------------------------------------------------
//  4x4 matrix keypad: drive one row low and read the columns
// ---------------------------------------------------------------------------
void taskKeypad(void *) {
  char last = 0; int stable = 0;
  for (;;) {
    char now = 0;
    for (int r = 0; r < 4; r++) {
      digitalWrite(ROWS[r], LOW);
      delayMicroseconds(5);
      for (int c = 0; c < 4; c++) if (digitalRead(COLS[c]) == LOW) now = KEYS[r][c];
      digitalWrite(ROWS[r], HIGH);
    }
    if (now == last) { if (++stable == 2 && now) xQueueSend(keyQueue, &now, 0); }   // debounce: 2 identical reads
    else { stable = 0; last = now; }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---------------------------------------------------------------------------
//  Automation rules → actuators
// ---------------------------------------------------------------------------
void taskLogic(void *) {
  TickType_t lastWake = xTaskGetTickCount();
  home_outputs_t prev = {-1, -1, -1, -1, -1};
  for (;;) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
    xSemaphoreTake(homeMutex, portMAX_DELAY);
    home_inputs_t in = inputs;
    char key;
    in.key = xQueueReceive(keyQueue, &key, 0) == pdTRUE ? key : 0;
    int nlog = home.log_count; uint32_t lastT = nlog ? home_event(&home, 0)->t_s : 0;
    home_step(&home, &in, 0.1f);
    home_outputs_t o = home.out;
    const home_event_t *ev = home_event(&home, 0);
    bool newEvent = ev && (home.log_count != nlog || ev->t_s != lastT);
    char msg[48] = ""; if (newEvent) strncpy(msg, ev->msg, sizeof msg);
    xSemaphoreGive(homeMutex);

    if (newEvent) Serial.printf("[%5lu s] %s\n", (unsigned long)ev->t_s, msg);
    if (o.light_pct != prev.light_pct) pwmOut(PIN_LAMP, 0, (uint32_t)o.light_pct * 1023 / 100);
    if (o.heater != prev.heater) digitalWrite(PIN_HEATER, o.heater);
    if (o.fan != prev.fan) digitalWrite(PIN_FAN, o.fan);
    if (o.blinds_deg != prev.blinds_deg) servoWrite(o.blinds_deg);
    if (o.siren != prev.siren) pwmOut(PIN_SIREN, 4, o.siren ? 512 : 0);
    prev = o;
  }
}

// ---------------------------------------------------------------------------
//  OLED display
// ---------------------------------------------------------------------------
void taskDisplay(void *) {
  for (;;) {
    xSemaphoreTake(homeMutex, portMAX_DELAY);
    home_t h = home; home_inputs_t in = inputs;
    xSemaphoreGive(homeMutex);
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
    oled.setCursor(0, 0);  oled.printf("%-6s alarm:%s", home_mode_str(h.mode), home_alarm_str(h.alarm));
    oled.setCursor(0, 12); oled.printf("%4.1fC %3.0f%%  set %4.1f", in.temp_c, in.hum_pct, h.setpoint_c);
    oled.setCursor(0, 22); oled.printf("lux %5.0f gas %4.0fppm", in.lux, in.gas_ppm);
    oled.setCursor(0, 32); oled.printf("L%3d%% H:%s F:%s B:%s", h.out.light_pct, h.out.heater ? "on" : "--", h.out.fan ? "on" : "--", h.out.blinds_deg ? "C" : "O");
    oled.setCursor(0, 42); oled.printf("%5.0f W  %7.1f Wh", home_power_w(&h), h.energy_wh);
    oled.setCursor(0, 54);
    if (h.gas_alarm) oled.print("!! GAS LEAK !!");
    else if (h.alarm == ALARM_TRIGGERED) oled.print("!! INTRUSION !!");
    else if (h.lock_timer > 0) oled.printf("keypad locked %2.0fs", h.lock_timer);
    else if (h.alarm == ALARM_ENTRY) oled.printf("code: %-4.*s  %2.0fs", h.entry_len, "****", h.alarm_timer);
    else if (h.entry_len) oled.printf("code: %.*s", h.entry_len, "****");
    oled.display();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// ---------------------------------------------------------------------------
//  Commands (web page and serial monitor)
// ---------------------------------------------------------------------------
static String command(const String &c, const String &v) {
  String r = "ok";
  xSemaphoreTake(homeMutex, portMAX_DELAY);
  if (c == "mode") home_set_mode(&home, v == "away" ? MODE_AWAY : v == "night" ? MODE_NIGHT : MODE_HOME);
  else if (c == "light") home_set_light(&home, v == "on" ? 100 : v == "off" ? 0 : -1);
  else if (c == "sp") home.setpoint_c = constrain(home.setpoint_c + v.toFloat(), 10.0f, 28.0f);
  else if (c == "arm") r = home_arm(&home) ? "ok" : "already armed";
  else if (c == "disarm") r = home_disarm(&home, v.c_str()) ? "ok" : "code rejected";
  else r = "unknown command";
  xSemaphoreGive(homeMutex);
  return r;
}

static void sendState() {
  xSemaphoreTake(homeMutex, portMAX_DELAY);
  home_t h = home; home_inputs_t in = inputs;
  xSemaphoreGive(homeMutex);
  static char buf[2048];
  int n = snprintf(buf, sizeof buf,
    "{\"temp\":%.1f,\"hum\":%.0f,\"lux\":%.0f,\"gas\":%.0f,\"door\":%d,\"mode\":\"%s\",\"sp\":%.1f,"
    "\"light\":%d,\"heater\":%d,\"fan\":%d,\"blinds\":%d,\"siren\":%d,\"gasAlarm\":%d,"
    "\"alarm\":\"%s\",\"locked\":%d,\"power\":%.0f,\"energy\":%.2f,\"log\":[",
    in.temp_c, in.hum_pct, in.lux, in.gas_ppm, in.door_open,
    h.mode == MODE_AWAY ? "away" : h.mode == MODE_NIGHT ? "night" : "home", h.setpoint_c,
    h.out.light_pct, h.out.heater, h.out.fan, h.out.blinds_deg, h.out.siren, h.gas_alarm,
    home_alarm_str(h.alarm), h.lock_timer > 0, home_power_w(&h), h.energy_wh);
  for (int i = 0; i < 8; i++) {
    const home_event_t *e = home_event(&h, i);
    if (!e) break;
    n += snprintf(buf + n, sizeof buf - n, "%s{\"t\":%lu,\"m\":\"%s\"}", i ? "," : "", (unsigned long)e->t_s, e->msg);
  }
  snprintf(buf + n, sizeof buf - n, "]}");
  server.send(200, "application/json", buf);
}

void setup() {
  Serial.begin(115200);
  homeMutex = xSemaphoreCreateMutex();
  keyQueue = xQueueCreate(8, sizeof(char));
  home_init(&home);

  pinMode(PIN_PIR, INPUT); pinMode(PIN_DOOR, INPUT);
  pinMode(PIN_HEATER, OUTPUT); pinMode(PIN_FAN, OUTPUT);
  for (int i = 0; i < 4; i++) { pinMode(ROWS[i], OUTPUT); digitalWrite(ROWS[i], HIGH); pinMode(COLS[i], INPUT_PULLUP); }
  pwmSetup(PIN_LAMP, 5000, 10, 0);
  pwmSetup(PIN_SERVO, 50, 16, 2);
  pwmSetup(PIN_SIREN, 2000, 10, 4);
  servoWrite(0);
  dht.setup(PIN_DHT, DHTesp::DHT22);
  Wire.begin(21, 22);
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  xTaskCreatePinnedToCore(taskSensors, "sensors", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(taskKeypad,  "keypad",  2048, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(taskLogic,   "logic",   4096, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(taskDisplay, "display", 4096, nullptr, 1, nullptr, 0);

  Serial.println("\n=== Smart home ===");
  Serial.println("Commands: mode home|night|away, light on|off|auto, sp +1|-1, arm, disarm 1234");
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin("Wokwi-GUEST", "", 6);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print("."); }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " failed (the home still works, without the web page)");

  server.on("/", []() { server.send(200, "text/html; charset=utf-8", WEB_PAGE); });
  server.on("/api/state", sendState);
  server.on("/api/cmd", []() { server.send(200, "text/plain", command(server.arg("c"), server.arg("v"))); });
  server.begin();
  Serial.println("Web page: http://localhost:8180 (Wokwi forwards to port 80 of the ESP32)");
}

void loop() {
  server.handleClient();
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      line.trim();
      if (line.length()) {
        int sp = line.indexOf(' ');
        String c = sp < 0 ? line : line.substring(0, sp), v = sp < 0 ? "" : line.substring(sp + 1);
        Serial.printf("> %s : %s\n", line.c_str(), command(c, v).c_str());
      }
      line = "";
    } else if (line.length() < 32) line += ch;
  }
  delay(2);
}
