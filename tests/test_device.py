import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

from nova.device import STATES, DeviceClient, DeviceError, Heartbeat, clean_text

TOKEN = "t" * 24


class Fake:
    """A fake ESP32: records requests, answers with a configurable status/body."""

    def __init__(self, status=200, body='{"ok":true}'):
        self.status, self.body, self.requests = status, body, []
        fake = self

        class H(BaseHTTPRequestHandler):
            def _handle(self):
                n = int(self.headers.get("Content-Length") or 0)
                fake.requests.append({
                    "method": self.command, "path": self.path,
                    "auth": self.headers.get("Authorization"),
                    "ctype": self.headers.get("Content-Type"),
                    "body": self.rfile.read(n).decode() if n else "",
                })
                data = fake.body.encode()
                self.send_response(fake.status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            do_GET = do_POST = _handle

            def log_message(self, *a):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def client(self, token=TOKEN):
        return DeviceClient("127.0.0.1", self.port, token, timeout=2)

    def close(self):
        self.server.shutdown()
        self.server.server_close()


@pytest.fixture()
def fake():
    f = Fake()
    yield f
    f.close()


def test_set_state_request_shape(fake):
    fake.client().set_state("working", "Running tests", ttl_ms=5000, step=2, steps=5)
    r = fake.requests[0]
    assert (r["method"], r["path"]) == ("POST", "/display")
    assert r["auth"] == f"Bearer {TOKEN}"
    assert r["ctype"] == "application/json"
    assert json.loads(r["body"]) == {"state": "working", "text": "Running tests",
                                     "ttl_ms": 5000, "steps": 5, "step": 2}


def test_minimal_state_has_no_optional_fields(fake):
    fake.client().set_state("listening")
    assert json.loads(fake.requests[0]["body"]) == {"state": "listening"}


@pytest.mark.parametrize("state", STATES)
def test_every_state_is_accepted(fake, state):
    fake.client().set_state(state)
    assert json.loads(fake.requests[0]["body"])["state"] == state


def test_clean_text():
    assert clean_text("café  au\tlait\n") == "cafe au lait"
    assert clean_text("x" * 100) == "x" * 40
    assert clean_text("") == ""
    assert clean_text("日本語") == ""
    assert clean_text("a\x00b\x07c") == "a b c"


@pytest.mark.parametrize("kwargs", [
    {"state": "dancing"},
    {"state": "idle", "ttl_ms": 0},
    {"state": "idle", "ttl_ms": 10**9},
    {"state": "working", "steps": 0},
    {"state": "working", "steps": 3, "step": 4},
    {"state": "working", "steps": 100},
])
def test_invalid_arguments_fail_before_any_network_call(fake, kwargs):
    with pytest.raises(ValueError):
        fake.client().set_state(**kwargs)
    assert fake.requests == []


def test_missing_token_never_reaches_network(fake):
    with pytest.raises(DeviceError, match="token is missing"):
        fake.client(token="").set_state("idle")
    with pytest.raises(DeviceError, match="token is missing"):
        fake.client(token="").heartbeat()
    assert fake.requests == []


def test_wrong_token_gives_helpful_error(fake):
    fake.status, fake.body = 401, '{"error":"unauthorized"}'
    with pytest.raises(DeviceError, match="DEVICE_TOKEN"):
        fake.client().set_state("idle")


def test_other_http_errors(fake):
    fake.status, fake.body = 400, '{"error":"unknown_state"}'
    with pytest.raises(DeviceError, match="HTTP 400"):
        fake.client().set_state("idle")


def test_non_json_reply(fake):
    fake.body = "<html>hi</html>"
    with pytest.raises(DeviceError, match="not JSON"):
        fake.client().heartbeat()


def test_unreachable_device_is_a_device_error():
    f = Fake()
    port = f.port
    f.close()                                   # nothing listens on this port any more
    with pytest.raises(DeviceError, match="cannot reach"):
        DeviceClient("127.0.0.1", port, TOKEN, timeout=0.5).heartbeat()


def test_health_sends_no_token(fake):
    fake.body = '{"ok":true,"state":"idle"}'
    assert fake.client(token="").health()["state"] == "idle"
    assert fake.requests[0]["auth"] is None
    assert (fake.requests[0]["method"], fake.requests[0]["path"]) == ("GET", "/health")


def test_token_is_not_in_error_text(fake):
    fake.status = 500
    with pytest.raises(DeviceError) as e:
        fake.client().heartbeat()
    assert TOKEN not in str(e.value)


def test_from_config_reads_token_from_environment(monkeypatch, tmp_path):
    import shutil
    from pathlib import Path
    from nova.config import load_config
    shutil.copytree(Path(__file__).resolve().parents[1] / "config", tmp_path / "config")
    monkeypatch.setenv("NOVA_TOKEN", "abc" * 8)
    c = DeviceClient.from_config(load_config(tmp_path, load_dotenv=False))
    assert c._token == "abc" * 8
    assert c.port == 80


class FlakyClient:
    def __init__(self):
        self.up = True
        self.calls = 0

    def heartbeat(self):
        self.calls += 1
        if not self.up:
            raise DeviceError("down")
        return {"ok": True}


def test_heartbeat_reports_changes():
    client, events = FlakyClient(), []
    hb = Heartbeat(client, interval_s=0.05, on_change=events.append)
    hb.start()
    time.sleep(0.2)
    client.up = False
    time.sleep(0.2)
    client.up = True
    time.sleep(0.2)
    hb.stop()
    assert events[0] is True
    assert False in events
    assert events[-1] is True
    assert all(a != b for a, b in zip(events, events[1:]))       # only changes are reported
    calls = client.calls
    time.sleep(0.15)
    assert client.calls == calls                                  # really stopped
