# 🏠 Smart home: local automation, alarm and gas safety (ESP32 + FreeRTOS)

[![Tests](https://github.com/bouabdallah-yacine/smart-home-esp32/actions/workflows/ci.yml/badge.svg)](https://github.com/bouabdallah-yacine/smart-home-esp32/actions/workflows/ci.yml)

A home that thinks **on its own, without Internet**: it turns the light on when someone walks in
the dark, heats just as much as needed, closes the blinds in bright sun, monitors gas and protects
the house with a code-based alarm. It is controlled from a **web page served directly by the ESP32**,
on a computer or a phone.

> ✅ Simulated on **Wokwi** (VS Code). All rules live in a portable C module, with **24 tests** on PC.

## Automation rules

| Feature | Rule |
|---|---|
| 💡 Lighting | dark (< 200 lux) + motion → 100 %; switches off 30 s after the last motion; night mode: 30 % night light |
| 🔥 Thermostat | 21 °C setpoint, ±0.5 °C hysteresis; night −2 °C, away −4 °C; **door open → heating off** |
| 🌀 Ventilation | humidity > 70 % (stops below 60 %), overheating, or gas leak |
| 🪟 Blinds | closed at night and when away; half-closed in full sun when it is too warm |
| ⚠️ Gas | ≥ 1000 ppm → siren, forced ventilation, heating off (top priority); alert clears below 500 ppm |
| 🚨 Alarm | `A` to arm, 10 s to leave; intrusion → 10 s to enter the code; otherwise siren and flashing lamp |
| 🔐 Brute-force protection | 3 wrong codes → keypad locked for 30 s |
| 🎭 Presence simulation | in away mode, the lamp randomly turns on and off in the evening |
| ⚡ Energy | instantaneous power and consumption (lamp 12 W, heater 1500 W, fan 40 W) |

**Priorities:** gas safety > alarm > comfort > energy saving.

## Architecture

```
Sensors  ──► taskSensors (10 Hz) ─┐
Keypad   ──► taskKeypad  (50 Hz) ─┼─► taskLogic (10 Hz) : home.c ──► PWM lamp, relay, servo, siren
                                  │                         │
Web page / serial ◄── loop() ─────┘           taskDisplay ──► OLED
```

| File | Role |
|---|---|
| `src/home.c` | all the rules (portable C, no hardware dependency) |
| `src/main.cpp` | FreeRTOS tasks, sensors, matrix keypad, PWM (lamp, servo, siren), web server |
| `src/web_page.h` | web page embedded in the firmware |
| `test/test_home.c` | 24 tests: lighting, thermostat, gas, alarm, brute force, presence, blinds, energy |

## Running the demo (Wokwi in VS Code)

1. Open this folder in VS Code → PlatformIO **Build** → **F1 › Wokwi: Start Simulator**.
2. Open **http://localhost:8180** in your browser: the home dashboard (refreshed every second).
3. Try it:
   - set the **light sensor** to its minimum, then click the **PIR**: the lamp turns on, then off 30 s later;
   - **DHT22** at 18 °C: the heating relay clicks on; open the **door** (switch): it turns off;
   - **gas slider** all the way up: alert, siren, ventilation, red banner on the web page;
   - keypad: `A` arms the alarm, wait 10 s, open the door, then type `1234#` within 10 s.
     Type three wrong codes to see the keypad lockout.

Serial monitor commands: `mode away`, `mode night`, `light on`, `light auto`, `sp 1`, `arm`, `disarm 1234`.

> If port forwarding does not work in your Wokwi version, the home still works:
> OLED display, keypad and serial commands.

## Tests

```bash
gcc -O2 -Wall -Wextra -Isrc -o t test/test_home.c src/home.c && ./t
```

## License

© 2026 Yacine — all rights reserved. Code published for viewing only (see [`LICENSE`](LICENSE)).
