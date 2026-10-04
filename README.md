# 🏠 Maison connectée : automatismes locaux, alarme et sécurité gaz (ESP32 + FreeRTOS)

[![Tests](https://github.com/bouabdellah-yacine/smart-home-esp32/actions/workflows/ci.yml/badge.svg)](https://github.com/bouabdellah-yacine/smart-home-esp32/actions/workflows/ci.yml)

Une maison qui réfléchit **toute seule, sans Internet** : elle allume la lumière quand quelqu'un entre
dans le noir, chauffe juste ce qu'il faut, ferme les volets au soleil, surveille le gaz et protège la
maison avec une alarme à code. On la pilote depuis une **page web servie directement par l'ESP32**,
sur PC ou sur téléphone.

> ✅ Simulée sur **Wokwi** (VS Code). Toutes les règles sont dans un module C portable, **24 tests** sur PC.

## Les automatismes

| Fonction | Règle |
|---|---|
| 💡 Éclairage | sombre (< 200 lux) + mouvement → 100 % ; extinction 30 s après le dernier mouvement ; mode nuit : veilleuse 30 % |
| 🔥 Thermostat | consigne 21 °C, hystérésis ±0,5 °C ; nuit −2 °C, absent −4 °C ; **porte ouverte → chauffage coupé** |
| 🌀 Ventilation | humidité > 70 % (arrêt sous 60 %), surchauffe, ou fuite de gaz |
| 🪟 Volets | fermés la nuit et en absence ; mi-clos en plein soleil quand il fait trop chaud |
| ⚠️ Gaz | ≥ 1000 ppm → sirène, ventilation forcée, chauffage coupé (priorité absolue) ; fin d'alerte sous 500 ppm |
| 🚨 Alarme | `A` pour armer, 10 s pour sortir ; intrusion → 10 s pour taper le code ; sinon sirène et lampe qui clignote |
| 🔐 Anti force brute | 3 codes faux → clavier bloqué 30 s |
| 🎭 Simulation de présence | en mode absent, la lampe s'allume et s'éteint au hasard le soir |
| ⚡ Énergie | puissance instantanée et consommation (lampe 12 W, chauffage 1500 W, ventilation 40 W) |

**Priorités :** sécurité gaz > alarme > confort > économie.

## Architecture

```
Capteurs ──► taskSensors (10 Hz) ─┐
Clavier  ──► taskKeypad  (50 Hz) ─┼─► taskLogic (10 Hz) : home.c ──► lampe PWM, relais, servo, sirène
                                  │                         │
Page web / série ◄── loop() ──────┘           taskDisplay ──► OLED
```

| Fichier | Rôle |
|---|---|
| `src/home.c` | toutes les règles (C portable, sans dépendance au matériel) |
| `src/main.cpp` | tâches FreeRTOS, capteurs, clavier matriciel, PWM (lampe, servo, sirène), serveur web |
| `src/web_page.h` | page web embarquée dans le firmware |
| `test/test_home.c` | 24 tests : éclairage, thermostat, gaz, alarme, force brute, présence, volets, énergie |

## Lancer la démo (Wokwi dans VS Code)

1. Ouvre ce dossier dans VS Code → PlatformIO **Build** → **F1 › Wokwi: Start Simulator**.
2. Ouvre **http://localhost:8180** dans ton navigateur : la page de la maison (mise à jour chaque seconde).
3. Essaie :
   - capteur de **luminosité** au minimum, puis clic sur le **PIR** : la lampe s'allume, puis s'éteint 30 s après ;
   - **DHT22** à 18 °C : le relais du chauffage colle ; ouvre la **porte** (interrupteur) : il se coupe ;
   - **glissière gaz** à fond : alerte, sirène, ventilation, bandeau rouge sur la page web ;
   - clavier : `A` arme l'alarme, attends 10 s, ouvre la porte, puis tape `1234#` avant 10 s.
     Tape trois codes faux pour voir le blocage du clavier.

Commandes dans le moniteur série : `mode away`, `mode night`, `light on`, `light auto`, `sp 1`, `arm`, `disarm 1234`.

> Si la redirection de port ne fonctionne pas dans ta version de Wokwi, la maison marche quand même :
> écran OLED, clavier et commandes série.

## Tests

```bash
gcc -O2 -Wall -Wextra -Isrc -o t test/test_home.c src/home.c && ./t
```

## Licence

© 2026 Yacine — tous droits réservés. Code publié pour consultation uniquement (voir [`LICENSE`](LICENSE)).
