"""Client for the Nova display (ESP32). Pushes states to POST /display on the device.

Standard library only. The access token comes from the environment (NOVA_TOKEN by default)
and is never logged or printed.
"""
from __future__ import annotations

import http.client
import json
import threading
import unicodedata
from typing import Callable

from .config import NovaConfig, get_secret

STATES = ("idle", "listening", "thinking", "speaking", "working", "saved", "error", "disconnected")
TEXT_MAX = 40
TTL_MAX_MS = 600_000


class DeviceError(Exception):
    """The display could not be reached, or it refused the request."""


def clean_text(text: str, limit: int = TEXT_MAX) -> str:
    """Printable ASCII only (accents are dropped: 'cafe' not 'café'), single spaces, max `limit` chars."""
    ascii_text = unicodedata.normalize("NFKD", text or "").encode("ascii", "ignore").decode("ascii")
    ascii_text = "".join(c if 32 <= ord(c) < 127 else " " for c in ascii_text)
    return " ".join(ascii_text.split())[:limit]


class DeviceClient:
    def __init__(self, host: str, port: int = 80, token: str = "", timeout: float = 1.5):
        self.host = host
        self.port = port
        self._token = token
        self.timeout = timeout

    @classmethod
    def from_config(cls, cfg: NovaConfig, timeout: float = 1.5) -> "DeviceClient":
        token = get_secret(cfg.safety.access_token_env) or ""
        return cls(cfg.device.address, cfg.device.port, token, timeout)

    # ---- public API ----
    def set_state(self, state: str, text: str = "", ttl_ms: int | None = None,
                  step: int | None = None, steps: int | None = None) -> dict:
        if state not in STATES:
            raise ValueError(f"unknown state {state!r}; use one of {', '.join(STATES)}")
        body: dict = {"state": state}
        text = clean_text(text)
        if text:
            body["text"] = text
        if ttl_ms is not None:
            if not 0 < ttl_ms <= TTL_MAX_MS:
                raise ValueError(f"ttl_ms must be between 1 and {TTL_MAX_MS}")
            body["ttl_ms"] = int(ttl_ms)
        if steps is not None:
            if not (0 < steps <= 99 and 0 <= (step or 0) <= steps):
                raise ValueError("steps must be 1..99 and step must be between 0 and steps")
            body["steps"] = int(steps)
            body["step"] = int(step or 0)
        return self._request("POST", "/display", body, auth=True)

    def heartbeat(self) -> dict:
        return self._request("POST", "/heartbeat", None, auth=True)

    def health(self) -> dict:
        return self._request("GET", "/health", None, auth=False)

    # ---- plumbing ----
    def _request(self, method: str, path: str, body: dict | None, auth: bool) -> dict:
        headers = {"Content-Type": "application/json"}
        if auth:
            if not self._token:
                raise DeviceError("access token is missing: set NOVA_TOKEN in .env")
            headers["Authorization"] = f"Bearer {self._token}"
        payload = json.dumps(body) if body is not None else ""
        conn = http.client.HTTPConnection(self.host, self.port, timeout=self.timeout)
        try:
            conn.request(method, path, body=payload if method == "POST" else None, headers=headers)
            resp = conn.getresponse()
            raw = resp.read().decode("utf-8", "replace")
        except (OSError, http.client.HTTPException) as exc:
            raise DeviceError(f"cannot reach the display at {self.host}:{self.port} ({exc})") from None
        finally:
            conn.close()
        if resp.status == 401:
            raise DeviceError("the display rejected the token: DEVICE_TOKEN in secrets.h must match NOVA_TOKEN in .env")
        if not 200 <= resp.status < 300:
            raise DeviceError(f"the display answered HTTP {resp.status}: {raw[:200]}")
        try:
            return json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            raise DeviceError(f"the display sent something that is not JSON: {raw[:100]!r}") from None


class Heartbeat:
    """Background thread: pings the display every `interval_s` so it knows the laptop is alive.

    `connected` tells whether the last ping worked. `on_change(connected)` fires on every change.
    """

    def __init__(self, client: DeviceClient, interval_s: float = 5.0,
                 on_change: Callable[[bool], None] | None = None):
        self.client = client
        self.interval_s = interval_s
        self.on_change = on_change
        self.connected = False
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="nova-heartbeat", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=self.interval_s + 2)

    def _run(self) -> None:
        first = True
        while not self._stop.is_set():
            try:
                self.client.heartbeat()
                ok = True
            except DeviceError:
                ok = False
            if ok != self.connected or first:
                self.connected = ok
                first = False
                if self.on_change:
                    self.on_change(ok)
            self._stop.wait(self.interval_s)
