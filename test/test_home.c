/*
 * Automation tests on PC (same code as on the ESP32)
 *   gcc -O2 -Wall -Wextra -Isrc -o t test/test_home.c src/home.c && ./t
 */
#include <stdio.h>
#include <string.h>
#include "home.h"

static int fails = 0;
#define CHECK(c, ...) do { int ok_ = (c); printf("%s ", ok_ ? "[OK]   " : "[FAIL] "); printf(__VA_ARGS__); \
  printf("\n"); if (!ok_) fails++; } while (0)

static home_inputs_t base(void) { home_inputs_t in = {0, 50, 21, 50, 300, 0, 0}; return in; }
static void run(home_t *h, home_inputs_t *in, float seconds) {
  for (float t = 0; t < seconds; t += 0.1f) { home_step(h, in, 0.1f); in->key = 0; }
}
static void type(home_t *h, home_inputs_t *in, const char *keys) {
  for (const char *k = keys; *k; k++) { in->key = *k; home_step(h, in, 0.1f); }
  in->key = 0;
}

int main(void) {
  home_t h; home_inputs_t in;

  /* Automatic lighting */
  home_init(&h); in = base(); in.lux = 50;
  in.motion = 1; run(&h, &in, 1); in.motion = 0;
  CHECK(h.out.light_pct == 100, "dark + motion: lamp on at 100 %%");
  run(&h, &in, 25);
  CHECK(h.out.light_pct == 100, "still on 25 s after the last motion");
  run(&h, &in, 10);
  CHECK(h.out.light_pct == 0, "switched off automatically after 30 s without motion (energy saving)");
  in.lux = 5000; in.motion = 1; run(&h, &in, 1);
  CHECK(h.out.light_pct == 0, "in daylight, motion does not turn the lamp on");
  home_set_mode(&h, MODE_NIGHT); in.lux = 20; run(&h, &in, 1);
  CHECK(h.out.light_pct == 30, "night mode: 30 %% night light to avoid glare");

  /* Thermostat */
  home_init(&h); in = base(); in.temp_c = 20.0f; run(&h, &in, 1);
  CHECK(h.out.heater == 1, "20 °C with a 21 °C setpoint: heating on");
  in.temp_c = 21.3f; run(&h, &in, 1);
  CHECK(h.out.heater == 1, "21.3 °C: stays on (±0.5 °C hysteresis, no relay chatter)");
  in.temp_c = 21.6f; run(&h, &in, 1);
  CHECK(h.out.heater == 0, "21.6 °C: heating off");
  in.temp_c = 19.0f; in.door_open = 1; run(&h, &in, 1);
  CHECK(h.out.heater == 0, "door open: no heating the outdoors");
  in.door_open = 0; home_set_mode(&h, MODE_AWAY); in.temp_c = 18.0f; run(&h, &in, 1);
  CHECK(h.out.heater == 0, "away mode: setpoint lowered to 17 °C, 18 °C is enough");

  /* Gas safety takes priority */
  home_init(&h); in = base(); in.temp_c = 18; run(&h, &in, 1);
  in.gas_ppm = 1500; run(&h, &in, 1);
  CHECK(h.out.siren && h.out.fan && !h.out.heater, "gas leak: siren, forced ventilation, heating off");
  in.gas_ppm = 700; run(&h, &in, 1);
  CHECK(h.out.siren, "700 ppm: alert stays active (1000 / 500 ppm hysteresis)");
  in.gas_ppm = 300; run(&h, &in, 1);
  CHECK(!h.out.siren && h.out.heater, "300 ppm: alert cleared, heating resumes");

  /* Alarm: arming, entry, disarming */
  home_init(&h); in = base();
  type(&h, &in, "A");
  in.door_open = 1; run(&h, &in, 5); in.door_open = 0;
  CHECK(h.alarm == ALARM_ARMING, "during the 10 s exit delay, opening the door triggers nothing");
  run(&h, &in, 6);
  CHECK(h.alarm == ALARM_ARMED && h.mode == MODE_AWAY, "alarm armed, the home switches to away mode");
  in.door_open = 1; run(&h, &in, 1); in.door_open = 0;
  CHECK(h.alarm == ALARM_ENTRY && !h.out.siren, "door opened: 10 s to enter the code, no siren yet");
  type(&h, &in, "1234#");
  CHECK(h.alarm == ALARM_DISARMED && h.mode == MODE_HOME, "correct code 1234: alarm disarmed, home mode");

  /* Intrusion */
  home_init(&h); in = base(); home_arm(&h); run(&h, &in, 11);
  in.motion = 1; run(&h, &in, 1); in.motion = 0; run(&h, &in, 10);
  CHECK(h.alarm == ALARM_TRIGGERED && h.out.siren, "motion without code within 10 s: INTRUSION, siren");

  /* Keypad brute force */
  type(&h, &in, "0000#"); type(&h, &in, "1111#"); type(&h, &in, "2222#");
  CHECK(h.lock_timer > 0, "3 wrong codes: keypad locked for 30 s");
  type(&h, &in, "1234#");
  CHECK(h.alarm == ALARM_TRIGGERED, "even the correct code is ignored during the lockout");
  run(&h, &in, 31); type(&h, &in, "1234#");
  CHECK(h.alarm == ALARM_DISARMED && !h.out.siren, "after 30 s, the correct code disarms");

  /* Presence simulation */
  home_init(&h); in = base(); in.lux = 20; home_set_mode(&h, MODE_AWAY);
  int ons = 0, prev = 0;
  for (int k = 0; k < 6000; k++) { home_step(&h, &in, 0.1f); if (h.out.light_pct && !prev) ons++; prev = h.out.light_pct; }
  CHECK(ons >= 3, "away in the evening: lamp turned on %d times in 10 min (burglar deterrent)", ons);

  /* Blinds */
  home_init(&h); in = base(); in.lux = 30000; in.temp_c = 25; run(&h, &in, 1);
  CHECK(h.out.blinds_deg == 60, "full sun and 25 °C: blinds half-closed (sun protection)");

  /* Energy */
  home_init(&h); in = base(); in.temp_c = 15; run(&h, &in, 3600);
  CHECK(h.energy_wh > 1490 && h.energy_wh < 1510, "1 h of heating: %.0f Wh consumed (1500 W heater)", h.energy_wh);

  printf("\n%s: %d failure(s)\n", fails ? "FAILED" : "ALL TESTS PASSED", fails);
  return fails != 0;
}
