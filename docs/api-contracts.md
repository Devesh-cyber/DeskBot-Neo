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

- `POST /display` body `{state, text?, step?, steps?, ttl_ms?}`
  - `state`: idle, listening, thinking, speaking, working, saved, error, disconnected
  - `text`: plain ASCII, up to 40 characters
  - `ttl_ms`: return to idle after this time (optional)
- `GET /health` -> `{ok: true, uptime_s, state}`
- The laptop sends a heartbeat every 5 s. No heartbeat for 15 s -> disconnected.
