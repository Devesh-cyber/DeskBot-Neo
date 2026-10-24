# nova_device (Phase 1)

The face and the display endpoint. Built from `phase2_connected`: eyes, blinking, timer countdown and the
to-do peek all still work. New: eight Nova states and `POST /display`.

## Files
| File | Job |
|---|---|
| `nova_device.ino` | main loop, eye animation, drawing of each state |
| `nova_logic.h` | state names, time-to-live, text cleaning, "which state to show" (no Arduino code, unit-tested on a PC) |
| `nova_state.h` | state shared between the HTTP task and the display loop |
| `nova_server.h` | HTTP server: `/display`, `/heartbeat`, `/health` |
| `net_task.h` | Wi-Fi, fixed IP, polling of the to-do/timer server |
| `faces.h` | drawing helpers (X eyes, check mark, dots, mouth, bars) |
| `timer_state.h`, `views.h` | unchanged from phase 2 |
| `secrets.example.h` | copy to `secrets.h` (git-ignored) |

## Libraries
Adafruit SSD1306, Adafruit GFX, ArduinoJson v7. Board: ESP32 Dev Module.

## States
idle, listening, thinking, speaking, working, saved, error, disconnected. See `docs/api-contracts.md`.
