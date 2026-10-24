# Nova API contracts (draft v0.1)

Change this file first, then the code. Endpoints that act, confirm, or push to the
device require the access token (`Authorization: Bearer <NOVA_TOKEN>`).

## Nova core (laptop, port 8000)

Existing, unchanged:
- `GET/POST /api/todos`, `POST /api/todos/{id}/toggle`, `DELETE /api/todos/{id}`
- `POST /api/timer/start`, `POST /api/timer/stop`
- `GET /api/state` (compact state for the ESP32)

New:
- `POST /api/chat` body `{text}` -> `{reply, activity_id}`
- `POST /api/voice/ptt/start`, `POST /api/voice/ptt/stop`
- `GET /api/activity?since=<iso time>` -> list of entries:
  `{id, ts, kind: state|tool_call|result|error, tier, summary, details}`
- `GET /api/confirmations` -> pending list `{id, tool, summary, tier, expires_at}`
- `POST /api/confirm/{id}` body `{approve: true|false}`
- `GET /api/status` -> `{state, device_connected, mic_enabled}`
- `POST /api/mic` body `{enabled: true|false}`

Errors: JSON `{error: <code>, message: <text>}` with a matching HTTP status.

## ESP32 (port 80)

Auth: `Authorization: Bearer <DEVICE_TOKEN>` where `DEVICE_TOKEN` (in `secrets.h`) equals `NOVA_TOKEN` (in `.env`).
Missing or wrong token -> HTTP 401. Errors are JSON `{error, message}`.

- `POST /display` (token) body `{state, text?, ttl_ms?, step?, steps?}` -> `{ok: true, state}`
  - `state`: idle, listening, thinking, speaking, working, saved, error, disconnected (case-insensitive)
  - `text`: up to 40 characters, printable ASCII (anything else becomes `?`). Shown by working, error, saved.
  - `ttl_ms`: how long the state stays if not refreshed, 1..600000. Default per state:
    listening 15 s, thinking 30 s, speaking 30 s, working 60 s, saved 2.5 s, error 8 s, disconnected 30 s.
    Idle never expires. After expiry the screen returns to idle (or disconnected if the laptop is silent).
  - `step`, `steps`: working shows a progress bar and `step/steps`. `steps` 1..99, `step` 0..steps.
    Without them, working shows a sweeping bar.
  - Errors: 400 `invalid_json` / `unknown_state` / `bad_request`, 401 `unauthorized`, 413 `too_large` (body over 600 bytes).
- `POST /heartbeat` (token) -> `{ok: true}`. Tells the device the laptop is alive.
- `GET /health` (open) -> `{ok, uptime_s, state, fw}`.

Disconnected: the device counts a successful to-do poll, a heartbeat, or a display push as contact.
No contact for 15 s -> disconnected face. The local timer keeps running and its countdown stays visible.
At boot the device waits the same 15 s before declaring "disconnected".
The laptop should send `POST /heartbeat` every 5 s (`nova.device.Heartbeat`).
