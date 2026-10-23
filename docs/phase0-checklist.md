# Phase 0 checklist (manual items)

Code is done when `pytest` passes and `scripts/check_env.py` shows no FAIL. The items below need you.

## Repo
- [ ] `git mv simulator/wokwoi simulator/wokwi`
- [ ] Rename `liabaries` to `libraries.txt` (`git mv`)
- [ ] `git rm -r --cached server/__pycache__`
- [ ] Append `gitignore_additions.txt` to `.gitignore`
- [ ] Copy this package's files into the repo root (do not overwrite `server/main.py`)
- [ ] Commit: "Phase 0: config, checks, contracts"

## Local setup
- [ ] `python3 -m venv .venv && source .venv/bin/activate`
- [ ] `pip install -r server/requirements.txt`
- [ ] `cp .env.example .env`, then fill the keys (never commit `.env`)
- [ ] `sudo apt install xdotool wmctrl pulseaudio-utils`
- [ ] `python3 -m pytest -q`
- [ ] `python3 scripts/check_env.py`

## Decisions and accounts
- [ ] Router: reserve a fixed IP for the laptop and for the ESP32
- [ ] Picovoice: create an account and AccessKey (free tier), train "Hey Nova" later in Phase 2
- [ ] xAI: check console for credits, create a key, run `python3 scripts/check_llm.py --list-only`, pick a model, set `[llm] model`
- [ ] Generate `NOVA_TOKEN`: `python3 -c "import secrets; print(secrets.token_urlsafe(32))"`
- [ ] Plug in the USB drive, set `[usb] mount_path`
- [ ] Register 1-3 projects in `config/projects.toml` with test commands
- [ ] Confirm the four Xfce app commands (`which firefox thunar code mousepad`)
- [ ] Decide: confirmation rule (either / both) and voice control of to-dos/timer

## Exit
- [ ] Pin versions: copy the "Pin report" lines into `server/requirements.txt`
- [ ] Update `docs/decision-log.md`
