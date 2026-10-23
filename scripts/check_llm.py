#!/usr/bin/env python3
"""Check xAI (or any OpenAI-compatible) access.  python3 scripts/check_llm.py [--list-only]

1. GET {base_url}/models  -> lists model ids you can use.
2. If [llm] model is set: one small chat call with a dummy tool, to test tool calling and latency.
Never prints the API key.
"""
from __future__ import annotations

import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))
from nova.config import ConfigError, get_secret, load_config  # noqa: E402


def call(url: str, key: str, timeout: int, body: dict | None = None) -> dict:
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method="POST" if body is not None else "GET")
    req.add_header("Authorization", f"Bearer {key}")
    req.add_header("Content-Type", "application/json")
    req.add_header("User-Agent", "nova-check/0.1")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.load(resp)


def explain(exc: Exception) -> str:
    if isinstance(exc, urllib.error.HTTPError):
        hint = {401: "key rejected, check XAI_API_KEY", 403: "no access / no credits on this key",
                404: "wrong URL or model name", 429: "rate limited or out of credits"}.get(exc.code, "")
        try:
            detail = exc.read().decode()[:300]
        except Exception:
            detail = ""
        return f"HTTP {exc.code} {hint} {detail}".strip()
    return f"{type(exc).__name__}: {exc}"


def main() -> int:
    list_only = "--list-only" in sys.argv
    try:
        cfg = load_config()
    except ConfigError as exc:
        print(f"[FAIL] config: {exc}")
        return 1
    key = get_secret(cfg.llm.api_key_env)
    if not key:
        print(f"[FAIL] {cfg.llm.api_key_env} is not set. Put it in .env (see .env.example).")
        return 1

    try:
        models = call(f"{cfg.llm.base_url}/models", key, cfg.llm.timeout_s)
    except Exception as exc:
        print(f"[FAIL] listing models: {explain(exc)}")
        return 1
    ids = sorted(m.get("id", "?") for m in models.get("data", []))
    print(f"[OK  ] key works; {len(ids)} model(s) available:")
    for i in ids:
        print(f"       {i}")
    if list_only:
        return 0
    if not cfg.llm.model:
        print("[WARN] [llm] model is empty in config/nova.toml. Pick one from the list above.")
        return 0
    if cfg.llm.model not in ids:
        print(f"[WARN] model '{cfg.llm.model}' is not in the list above.")

    body = {
        "model": cfg.llm.model,
        "messages": [{"role": "user", "content": "Open Firefox for me."}],
        "tools": [{"type": "function", "function": {
            "name": "open_app", "description": "Open a registered application.",
            "parameters": {"type": "object",
                           "properties": {"app": {"type": "string", "enum": ["browser", "vscode"]}},
                           "required": ["app"]}}}],
    }
    start = time.monotonic()
    try:
        out = call(f"{cfg.llm.base_url}/chat/completions", key, cfg.llm.timeout_s, body)
    except Exception as exc:
        print(f"[FAIL] chat call: {explain(exc)}")
        return 1
    elapsed = time.monotonic() - start
    msg = out["choices"][0]["message"]
    calls = msg.get("tool_calls") or []
    print(f"[OK  ] chat call took {elapsed:.1f}s; usage: {out.get('usage')}")
    if calls:
        print(f"[OK  ] tool calling works: {calls[0]['function']['name']}({calls[0]['function']['arguments']})")
    else:
        print(f"[WARN] model replied without a tool call: {msg.get('content')!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
