/*
 * ============================================================================
 *  Home brain: all automation rules, in portable C
 * ============================================================================
 *  Inputs   : presence (PIR), light level, temperature, humidity, gas, door,
 *             keypad keys, mode selected by the occupant
 *  Outputs  : lamp (0..100 %), heating, ventilation, blinds (0..90°), siren
 *
 *  Priorities (highest to lowest):
 *    1. GAS SAFETY     : siren, forced ventilation, heating off
 *    2. ALARM          : intrusion detected in away mode
 *    3. COMFORT        : automatic lighting, thermostat, blinds
 *    4. ENERGY SAVING  : automatic switch-off, away mode, presence simulation
 *
 *  The same file runs on the ESP32 and in the PC tests (test/test_home.c).
 * ============================================================================
 */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MODE_HOME = 0, MODE_AWAY, MODE_NIGHT } home_mode_t;
typedef enum { ALARM_DISARMED = 0, ALARM_ARMING, ALARM_ARMED, ALARM_ENTRY, ALARM_TRIGGERED } alarm_state_t;

typedef struct {
  /* sensors */
  int   motion;          /* 1 = presence detected */
  float lux;             /* light level */
  float temp_c, hum_pct;
  float gas_ppm;
  int   door_open;
  /* keypad: key pressed this cycle (0 if none) */
  char  key;
} home_inputs_t;

typedef struct {
  int   light_pct;       /* living-room lamp */
  int   heater;          /* heating relay */
  int   fan;             /* ventilation fan */
  int   blinds_deg;      /* 0 = blinds open, 90 = closed */
  int   siren;
} home_outputs_t;

#define HOME_LOG_LEN 12
typedef struct { uint32_t t_s; char msg[48]; } home_event_t;

typedef struct {
  /* settings */
  home_mode_t mode;
  float setpoint_c;      /* thermostat setpoint */
  char  code[5];         /* alarm code */
  float light_timeout_s; /* switch-off delay after the last motion */
  /* state */
  float t;               /* elapsed time (s) */
  float light_timer;
  int   manual_light;    /* -1 = auto, otherwise value forced by the occupant */
  alarm_state_t alarm;
  float alarm_timer;
  char  entry[5]; int entry_len;
  int   bad_codes;
  float lock_timer;      /* keypad locked after 3 wrong codes */
  int   hum_fan;         /* humidity hysteresis memory */
  int   gas_alarm;
  float presence_sim_timer; int presence_sim_on;
  uint32_t rng;
  /* energy */
  float energy_wh;
  /* event log */
  home_event_t log[HOME_LOG_LEN]; int log_head, log_count;
  home_outputs_t out;
} home_t;

/* Rule constants (documented in the README) */
#define HOME_DARK_LUX        200.0f
#define HOME_SUN_LUX         10000.0f
#define HOME_GAS_ALARM_PPM   1000.0f
#define HOME_GAS_CLEAR_PPM   500.0f
#define HOME_HYST_C          0.5f
#define HOME_EXIT_DELAY_S    10.0f
#define HOME_ENTRY_DELAY_S   10.0f
#define HOME_LOCKOUT_S       30.0f
#define HOME_POWER_LAMP_W    12.0f
#define HOME_POWER_HEATER_W  1500.0f
#define HOME_POWER_FAN_W     40.0f

void home_init(home_t *h);
void home_step(home_t *h, const home_inputs_t *in, float dt);
float home_power_w(const home_t *h);

/* Occupant commands (web page, serial monitor) */
void home_set_mode(home_t *h, home_mode_t m);
void home_set_light(home_t *h, int pct);             /* -1 = back to automatic */
int  home_arm(home_t *h);                            /* 1 if arming starts */
int  home_disarm(home_t *h, const char *code);       /* 1 if the code is correct */

const char *home_mode_str(home_mode_t m);
const char *home_alarm_str(alarm_state_t a);
/* i-th most recent event (0 = latest); NULL if it does not exist */
const home_event_t *home_event(const home_t *h, int i);

#ifdef __cplusplus
}
#endif
