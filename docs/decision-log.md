# Decision log

One line per decision: date, choice, reason.

- 2026-10-20 Assistant name Nova, wake phrase 'Hey Nova', website push-to-talk fallback.
- 2026-10-20 Solo personal-use project, one Linux Mint PC (Xfce, X11); goal about one month, flexible hours.
- 2026-10-20 Local speech-to-text first (Whisper tiny/base class), local text-to-speech, cloud LLM (Grok first candidate, swappable).
- 2026-10-20 No interruption in v1; mic muted while Nova speaks.
- 2026-10-20 Safety tiers 0-3; delete, install and sudo forbidden in v1.
- 2026-10-20 Allowed folders: Downloads, Documents, Desktop, USB drive; missing USB means stop and report.
- 2026-10-20 No new hardware in v1; laptop pushes state to the ESP32 over HTTP.
- 2026-10-22 Full project report written; phases 0 to 8 defined.
- 2026-10-23 Phase 0 started: config files, config loader, environment and LLM check scripts.

Open (defaults in the report, section 16): confirmation rule, Grok access, wake word licence,
USB path and projects, Xfce commands, access token, voice control of to-dos and timer,
activity log storage, Python version.

- 2026-10-23: Confirmation rule = either voice or website approves (confirm_mode = "either").
- 2026-10-23: Voice control of to-dos and timer is in scope.

- 2026-10-23: Phase 1 done in code. Display commands go to POST /display on the ESP32 with a bearer token (DEVICE_TOKEN = NOVA_TOKEN).
- 2026-10-23: Pushed states expire by themselves (time-to-live) so a crashed laptop never leaves the face stuck.
- 2026-10-23: Disconnected = 15 s without any contact (to-do poll, heartbeat or push). The local timer keeps running.
- 2026-10-23: ESP32 gets a fixed IP in firmware (192.168.0.201) because the router reservation was not available.
- 2026-10-23: Wake word engine changed from Picovoice (free tier ended 2026-06-30) to openWakeWord. LLM provider changed from xAI to Groq (openai/gpt-oss-120b).
