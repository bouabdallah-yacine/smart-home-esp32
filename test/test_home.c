/*
 * Tests des automatismes sur PC (même code que sur l'ESP32)
 *   gcc -O2 -Wall -Wextra -Isrc -o t test/test_home.c src/home.c && ./t
 */
#include <stdio.h>
#include <string.h>
#include "home.h"

static int fails = 0;
#define CHECK(c, ...) do { int ok_ = (c); printf("%s ", ok_ ? "[OK]   " : "[ECHEC]"); printf(__VA_ARGS__); \
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

  /* Éclairage automatique */
  home_init(&h); in = base(); in.lux = 50;
  in.motion = 1; run(&h, &in, 1); in.motion = 0;
  CHECK(h.out.light_pct == 100, "il fait sombre + mouvement : lampe allumée à 100 %%");
  run(&h, &in, 25);
  CHECK(h.out.light_pct == 100, "toujours allumée 25 s après le dernier mouvement");
  run(&h, &in, 10);
  CHECK(h.out.light_pct == 0, "éteinte automatiquement après 30 s sans mouvement (économie)");
  in.lux = 5000; in.motion = 1; run(&h, &in, 1);
  CHECK(h.out.light_pct == 0, "en plein jour, le mouvement n'allume pas la lampe");
  home_set_mode(&h, MODE_NIGHT); in.lux = 20; run(&h, &in, 1);
  CHECK(h.out.light_pct == 30, "mode nuit : veilleuse à 30 %% pour ne pas éblouir");

  /* Thermostat */
  home_init(&h); in = base(); in.temp_c = 20.0f; run(&h, &in, 1);
  CHECK(h.out.heater == 1, "20 °C pour une consigne de 21 °C : chauffage allumé");
  in.temp_c = 21.3f; run(&h, &in, 1);
  CHECK(h.out.heater == 1, "21,3 °C : reste allumé (hystérésis ±0,5 °C, le relais ne claque pas)");
  in.temp_c = 21.6f; run(&h, &in, 1);
  CHECK(h.out.heater == 0, "21,6 °C : chauffage coupé");
  in.temp_c = 19.0f; in.door_open = 1; run(&h, &in, 1);
  CHECK(h.out.heater == 0, "porte ouverte : on ne chauffe pas l'extérieur");
  in.door_open = 0; home_set_mode(&h, MODE_AWAY); in.temp_c = 18.0f; run(&h, &in, 1);
  CHECK(h.out.heater == 0, "mode absent : consigne abaissée à 17 °C, 18 °C suffit");

  /* Sécurité gaz prioritaire */
  home_init(&h); in = base(); in.temp_c = 18; run(&h, &in, 1);
  in.gas_ppm = 1500; run(&h, &in, 1);
  CHECK(h.out.siren && h.out.fan && !h.out.heater, "fuite de gaz : sirène, ventilation forcée, chauffage coupé");
  in.gas_ppm = 700; run(&h, &in, 1);
  CHECK(h.out.siren, "700 ppm : l'alerte reste active (hystérésis 1000 / 500 ppm)");
  in.gas_ppm = 300; run(&h, &in, 1);
  CHECK(!h.out.siren && h.out.heater, "300 ppm : fin d'alerte, le chauffage reprend");

  /* Alarme : armement, entrée, désarmement */
  home_init(&h); in = base();
  type(&h, &in, "A");
  in.door_open = 1; run(&h, &in, 5); in.door_open = 0;
  CHECK(h.alarm == ALARM_ARMING, "pendant les 10 s pour sortir, ouvrir la porte ne déclenche rien");
  run(&h, &in, 6);
  CHECK(h.alarm == ALARM_ARMED && h.mode == MODE_AWAY, "alarme armée, la maison passe en mode absent");
  in.door_open = 1; run(&h, &in, 1); in.door_open = 0;
  CHECK(h.alarm == ALARM_ENTRY && !h.out.siren, "porte ouverte : 10 s pour taper le code, pas encore de sirène");
  type(&h, &in, "1234#");
  CHECK(h.alarm == ALARM_DISARMED && h.mode == MODE_HOME, "bon code 1234 : alarme désarmée, mode maison");

  /* Intrusion */
  home_init(&h); in = base(); home_arm(&h); run(&h, &in, 11);
  in.motion = 1; run(&h, &in, 1); in.motion = 0; run(&h, &in, 10);
  CHECK(h.alarm == ALARM_TRIGGERED && h.out.siren, "mouvement sans code dans les 10 s : INTRUSION, sirène");

  /* Force brute sur le clavier */
  type(&h, &in, "0000#"); type(&h, &in, "1111#"); type(&h, &in, "2222#");
  CHECK(h.lock_timer > 0, "3 codes faux : clavier bloqué 30 s");
  type(&h, &in, "1234#");
  CHECK(h.alarm == ALARM_TRIGGERED, "même le bon code est ignoré pendant le blocage");
  run(&h, &in, 31); type(&h, &in, "1234#");
  CHECK(h.alarm == ALARM_DISARMED && !h.out.siren, "après 30 s, le bon code désarme");

  /* Simulation de présence */
  home_init(&h); in = base(); in.lux = 20; home_set_mode(&h, MODE_AWAY);
  int ons = 0, prev = 0;
  for (int k = 0; k < 6000; k++) { home_step(&h, &in, 0.1f); if (h.out.light_pct && !prev) ons++; prev = h.out.light_pct; }
  CHECK(ons >= 3, "absent le soir : la lampe s'allume %d fois en 10 min (dissuasion des cambrioleurs)", ons);

  /* Volets */
  home_init(&h); in = base(); in.lux = 30000; in.temp_c = 25; run(&h, &in, 1);
  CHECK(h.out.blinds_deg == 60, "plein soleil et 25 °C : volets à moitié fermés (protection solaire)");

  /* Énergie */
  home_init(&h); in = base(); in.temp_c = 15; run(&h, &in, 3600);
  CHECK(h.energy_wh > 1490 && h.energy_wh < 1510, "1 h de chauffage : %.0f Wh consommés (radiateur 1500 W)", h.energy_wh);

  printf("\n%s : %d échec(s)\n", fails ? "ÉCHEC" : "TOUS LES TESTS PASSENT", fails);
  return fails != 0;
}
