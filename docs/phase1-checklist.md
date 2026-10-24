# Phase 1 checklist (device states)

Done when every box is ticked.

- [ ] `secrets.h` created in `firmware/nova_device/` with Wi-Fi, `DEVICE_TOKEN` (same as `NOVA_TOKEN`) and the new server IP
- [ ] Sketch compiles and uploads
- [ ] Serial Monitor shows `Wi-Fi connected, ESP32 IP: 192.168.0.201` and `HTTP server ready on port 80`
- [ ] `python3 scripts/device_demo.py status` prints `ok: True`
- [ ] `python3 scripts/device_demo.py cycle` shows all 8 states look right (idle, listening, thinking, speaking, working, saved, error)
- [ ] saved returns to idle by itself after ~2.5 s, error after ~8 s
- [ ] wrong token is rejected (device_demo prints "rejected the token")
- [ ] stop sending anything for 15 s -> sleepy "Nova offline" face (stop uvicorn first, it also counts as contact)
- [ ] start a timer from the phone page: countdown still works, with and without the laptop
- [ ] `python3 -m pytest -q` passes
- [ ] committed and pushed
