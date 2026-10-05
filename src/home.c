#include "home.h"
#include <stdio.h>
#include <string.h>

static void logf_(home_t *h, const char *msg) {
  home_event_t *e = &h->log[(h->log_head + h->log_count) % HOME_LOG_LEN];
  if (h->log_count == HOME_LOG_LEN) h->log_head = (h->log_head + 1) % HOME_LOG_LEN; else h->log_count++;
  e->t_s = (uint32_t)h->t;
  snprintf(e->msg, sizeof e->msg, "%s", msg);
}

const home_event_t *home_event(const home_t *h, int i) {
  if (i >= h->log_count) return 0;
  return &h->log[(h->log_head + h->log_count - 1 - i) % HOME_LOG_LEN];
}

const char *home_mode_str(home_mode_t m) { return m == MODE_HOME ? "home" : m == MODE_AWAY ? "away" : "night"; }
const char *home_alarm_str(alarm_state_t a) {
  switch (a) {
    case ALARM_DISARMED: return "disarmed";
    case ALARM_ARMING: return "arming";
    case ALARM_ARMED: return "armed";
    case ALARM_ENTRY: return "entry: code?";
    case ALARM_TRIGGERED: return "INTRUSION";
  }
  return "?";
}

void home_init(home_t *h) {
  memset(h, 0, sizeof *h);
  h->mode = MODE_HOME; h->setpoint_c = 21.0f; strcpy(h->code, "1234");
  h->light_timeout_s = 30.0f; h->manual_light = -1; h->rng = 12345;
  logf_(h, "System started");
}

void home_set_mode(home_t *h, home_mode_t m) {
  if (h->mode == m) return;
  h->mode = m;
  char b[48]; snprintf(b, sizeof b, "Mode %s", home_mode_str(m)); logf_(h, b);
}

void home_set_light(home_t *h, int pct) {
  h->manual_light = pct;
  logf_(h, pct < 0 ? "Lighting: automatic" : pct ? "Lighting: on (manual)" : "Lighting: off (manual)");
}

int home_arm(home_t *h) {
  if (h->alarm != ALARM_DISARMED) return 0;
  h->alarm = ALARM_ARMING; h->alarm_timer = HOME_EXIT_DELAY_S;
  logf_(h, "Alarm arming: leave within 10 s");
  return 1;
}

int home_disarm(home_t *h, const char *code) {
  if (h->alarm == ALARM_DISARMED) return 1;
  if (h->lock_timer > 0) { logf_(h, "Keypad locked: code ignored"); return 0; }
  if (strcmp(code, h->code) == 0) {
    h->alarm = ALARM_DISARMED; h->bad_codes = 0;
    home_set_mode(h, MODE_HOME);
    logf_(h, "Alarm disarmed");
    return 1;
  }
  h->bad_codes++;
  char b[48]; snprintf(b, sizeof b, "Wrong code (%d/3)", h->bad_codes); logf_(h, b);
  if (h->bad_codes >= 3) {                                /* brute-force protection */
    h->lock_timer = HOME_LOCKOUT_S; h->bad_codes = 0;
    logf_(h, "3 wrong codes: keypad locked for 30 s");
  }
  return 0;
}

static uint32_t rnd(home_t *h) { h->rng = h->rng * 1103515245u + 12345u; return (h->rng >> 16) & 0x7FFF; }

/* --- Keypad: digits, '#' confirm, '*' clear, 'A' arm --- */
static void handle_key(home_t *h, char k) {
  if (k >= '0' && k <= '9') { if (h->entry_len < 4) { h->entry[h->entry_len++] = k; h->entry[h->entry_len] = 0; } }
  else if (k == '*') { h->entry_len = 0; h->entry[0] = 0; }
  else if (k == 'A') home_arm(h);
  else if (k == '#') { home_disarm(h, h->entry); h->entry_len = 0; h->entry[0] = 0; }
}

static void step_alarm(home_t *h, const home_inputs_t *in, float dt) {
  if (h->lock_timer > 0) h->lock_timer -= dt;
  if (in->key) handle_key(h, in->key);
  switch (h->alarm) {
    case ALARM_ARMING:
      if ((h->alarm_timer -= dt) <= 0) { h->alarm = ALARM_ARMED; home_set_mode(h, MODE_AWAY); logf_(h, "Alarm armed"); }
      break;
    case ALARM_ARMED:
      if (in->door_open || in->motion) {
        h->alarm = ALARM_ENTRY; h->alarm_timer = HOME_ENTRY_DELAY_S;
        logf_(h, in->door_open ? "Door opened: enter code within 10 s" : "Motion: enter code within 10 s");
      }
      break;
    case ALARM_ENTRY:
      if ((h->alarm_timer -= dt) <= 0) { h->alarm = ALARM_TRIGGERED; logf_(h, "INTRUSION: siren triggered"); }
      break;
    default: break;
  }
}

static void step_gas(home_t *h, const home_inputs_t *in) {
  if (!h->gas_alarm && in->gas_ppm >= HOME_GAS_ALARM_PPM) { h->gas_alarm = 1; logf_(h, "GAS LEAK: fan on, heating off"); }
  else if (h->gas_alarm && in->gas_ppm < HOME_GAS_CLEAR_PPM) { h->gas_alarm = 0; logf_(h, "Gas level back to normal"); }
}

static void step_light(home_t *h, const home_inputs_t *in, float dt) {
  home_outputs_t *o = &h->out;
  if (h->alarm == ALARM_TRIGGERED || h->gas_alarm) { o->light_pct = ((int)(h->t * 2) % 2) ? 100 : 0; return; }  /* blinking */
  if (h->manual_light >= 0) { o->light_pct = h->manual_light; return; }
  int dark = in->lux < HOME_DARK_LUX;
  if (h->mode == MODE_AWAY) {                          /* presence simulation in the evening */
    if ((h->presence_sim_timer -= dt) <= 0) {
      h->presence_sim_on = dark && !h->presence_sim_on;
      h->presence_sim_timer = 20.0f + (float)(rnd(h) % 40);
    }
    o->light_pct = (dark && h->presence_sim_on) ? 100 : 0;
    return;
  }
  if (in->motion) h->light_timer = h->light_timeout_s;
  else if (h->light_timer > 0) h->light_timer -= dt;
  o->light_pct = (dark && h->light_timer > 0) ? (h->mode == MODE_NIGHT ? 30 : 100) : 0;
}

static void step_climate(home_t *h, const home_inputs_t *in) {
  home_outputs_t *o = &h->out;
  float sp = h->setpoint_c - (h->mode == MODE_NIGHT ? 2.0f : h->mode == MODE_AWAY ? 4.0f : 0.0f);
  /* thermostat with hysteresis (prevents relay chatter) */
  if (in->temp_c < sp - HOME_HYST_C) o->heater = 1;
  else if (in->temp_c > sp + HOME_HYST_C) o->heater = 0;
  if (in->door_open || h->gas_alarm) o->heater = 0;   /* door open or gas leak: no heating */
  /* ventilation: gas, humidity (70 / 60 % hysteresis), overheating */
  if (in->hum_pct > 70) h->hum_fan = 1; else if (in->hum_pct < 60) h->hum_fan = 0;
  o->fan = h->gas_alarm || h->hum_fan || in->temp_c > h->setpoint_c + 4;
  /* blinds: closed at night and when away; half-closed in full sun when too warm */
  if (h->mode != MODE_HOME) o->blinds_deg = 90;
  else if (in->lux > HOME_SUN_LUX && in->temp_c > h->setpoint_c + 2) o->blinds_deg = 60;
  else o->blinds_deg = 0;
}

float home_power_w(const home_t *h) {
  return h->out.light_pct / 100.0f * HOME_POWER_LAMP_W + h->out.heater * HOME_POWER_HEATER_W + h->out.fan * HOME_POWER_FAN_W;
}

void home_step(home_t *h, const home_inputs_t *in, float dt) {
  h->t += dt;
  step_alarm(h, in, dt);
  step_gas(h, in);
  step_light(h, in, dt);
  step_climate(h, in);
  h->out.siren = h->gas_alarm || h->alarm == ALARM_TRIGGERED;
  h->energy_wh += home_power_w(h) * dt / 3600.0f;
}
