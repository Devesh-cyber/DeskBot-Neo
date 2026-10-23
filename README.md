# Nova (DesktopBot)

A voice-controlled desk assistant. An ESP32 + 0.96" OLED is its face; an agent on the Linux Mint laptop does the work.

## Layout
```
config/        nova.toml (settings), projects.toml (project registry)
server/        main.py (existing to-do/timer server), nova/ (assistant core), voice/ (voice worker)
firmware/      phase1_eyes, phase2_connected (working), nova_device (Phase 1 target)
scripts/       check_env.py, check_llm.py
tests/         pytest tests
docs/          api-contracts.md, decision-log.md, phase0-checklist.md, project report
simulator/     Wokwi files
```

## Setup
```
python3 -m venv .venv && source .venv/bin/activate
pip install -r server/requirements.txt
cp .env.example .env        # fill in keys
python3 -m pytest -q
python3 scripts/check_env.py
python3 scripts/check_llm.py --list-only
```

## Run the existing server (Phase 2 demo)
```
cd server && uvicorn main:app --host 0.0.0.0 --port 8000
```
Phone page: `http://<laptop-ip>:8000`. Firmware: copy `firmware/phase2_connected/secrets.example.h` to `secrets.h`, fill Wi-Fi and server IP, upload with Arduino IDE.

## Status
Phase 0 (foundations) in progress. See `docs/phase0-checklist.md`.
