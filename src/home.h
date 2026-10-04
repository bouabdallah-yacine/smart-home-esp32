/*
 * ============================================================================
 *  Cerveau de la maison : toutes les règles d'automatisme, en C portable
 * ============================================================================
 *  Entrées  : présence (PIR), luminosité, température, humidité, gaz, porte,
 *             touches du clavier, mode choisi par l'habitant
 *  Sorties  : lampe (0..100 %), chauffage, ventilation, volets (0..90°), sirène
 *
 *  Priorités (de la plus forte à la plus faible) :
 *    1. SÉCURITÉ GAZ   : sirène, ventilation forcée, chauffage coupé
 *    2. ALARME         : intrusion détectée en mode absent
 *    3. CONFORT        : éclairage automatique, thermostat, volets
 *    4. ÉCONOMIE       : extinction automatique, mode absent, simulation de présence
 *
 *  Le même fichier tourne sur l'ESP32 et dans les tests sur PC (test/test_home.c).
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
  /* capteurs */
  int   motion;          /* 1 = présence détectée */
  float lux;             /* luminosité */
  float temp_c, hum_pct;
  float gas_ppm;
  int   door_open;
  /* clavier : touche appuyée ce cycle (0 si aucune) */
  char  key;
} home_inputs_t;

typedef struct {
  int   light_pct;       /* lampe du salon */
  int   heater;          /* relais du chauffage */
  int   fan;             /* ventilation */
  int   blinds_deg;      /* 0 = volets ouverts, 90 = fermés */
  int   siren;
} home_outputs_t;

#define HOME_LOG_LEN 12
typedef struct { uint32_t t_s; char msg[48]; } home_event_t;

typedef struct {
  /* réglages */
  home_mode_t mode;
  float setpoint_c;      /* consigne du thermostat */
  char  code[5];         /* code de l'alarme */
  float light_timeout_s; /* extinction après absence de mouvement */
  /* état */
  float t;               /* temps écoulé (s) */
  float light_timer;
  int   manual_light;    /* -1 = auto, sinon valeur forcée par l'habitant */
  alarm_state_t alarm;
  float alarm_timer;
  char  entry[5]; int entry_len;
  int   bad_codes;
  float lock_timer;      /* clavier bloqué après 3 codes faux */
  int   hum_fan;         /* mémoire de l'hystérésis humidité */
  int   gas_alarm;
  float presence_sim_timer; int presence_sim_on;
  uint32_t rng;
  /* énergie */
  float energy_wh;
  /* journal des événements */
  home_event_t log[HOME_LOG_LEN]; int log_head, log_count;
  home_outputs_t out;
} home_t;

/* Constantes des règles (documentées dans le README) */
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

/* Commandes de l'habitant (page web, moniteur série) */
void home_set_mode(home_t *h, home_mode_t m);
void home_set_light(home_t *h, int pct);             /* -1 = retour en automatique */
int  home_arm(home_t *h);                            /* 1 si l'armement démarre */
int  home_disarm(home_t *h, const char *code);       /* 1 si le code est bon */

const char *home_mode_str(home_mode_t m);
const char *home_alarm_str(alarm_state_t a);
/* i-ème événement le plus récent (0 = dernier) ; NULL s'il n'existe pas */
const home_event_t *home_event(const home_t *h, int i);

#ifdef __cplusplus
}
#endif
