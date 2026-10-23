#!/usr/bin/env python3
"""Nova environment check (Phase 0). Run from the repo root:  python3 scripts/check_env.py

Prints OK / WARN / FAIL / INFO lines. Exit code 1 if anything FAILs. Never prints secret values.
"""
from __future__ import annotations

import importlib.metadata as md
import os
import shutil
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))
from nova.config import ConfigError, get_secret, load_config  # noqa: E402

fails = 0


def say(level: str, msg: str) -> None:
    global fails
    if level == "FAIL":
        fails += 1
    print(f"[{level:<4}] {msg}")


def check_python() -> None:
    v = sys.version_info
    say("OK" if v >= (3, 10) else "FAIL", f"Python {v.major}.{v.minor}.{v.micro} (need 3.10+)")
    print("\nPin report (copy into server/requirements.txt at the end of Phase 0):")
    for pkg in ("fastapi", "uvicorn", "tomli", "pytest"):
        try:
            print(f"  {pkg}=={md.version(pkg)}")
        except md.PackageNotFoundError:
            tag = "FAIL" if pkg in ("fastapi", "uvicorn") else "WARN"
            if pkg == "tomli" and v >= (3, 11):
                continue
            say(tag, f"package '{pkg}' not installed (pip install -r server/requirements.txt)")
    print()


def check_session() -> None:
    session = os.environ.get("XDG_SESSION_TYPE", "unknown")
    desktop = os.environ.get("XDG_CURRENT_DESKTOP", "unknown")
    say("OK" if session == "x11" else "WARN",
        f"session type: {session} (xdotool/wmctrl need x11)")
    say("OK" if "xfce" in desktop.lower() else "WARN", f"desktop: {desktop} (expected XFCE)")
    for tool in ("git", "curl", "xdotool", "wmctrl", "pactl"):
        say("OK" if shutil.which(tool) else "WARN",
            f"{tool}: {shutil.which(tool) or 'not found (sudo apt install ' + ('pulseaudio-utils' if tool == 'pactl' else tool) + ')'}")


def lan_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))
            return s.getsockname()[0]
    except OSError:
        return "unknown"


def port_free(port: int) -> bool:
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) != 0


def inside(path: Path, parents: list[Path]) -> bool:
    real = path.expanduser().resolve()
    return any(real == p.resolve() or p.resolve() in real.parents for p in parents)


def check_config() -> None:
    try:
        cfg = load_config()
    except ConfigError as exc:
        say("FAIL", f"config: {exc}")
        return
    say("OK", f"config loaded from {cfg.root / 'config'}")

    for name, app in cfg.apps.items():
        found = shutil.which(app.command[0])
        say("OK" if found else "WARN", f"app '{name}': {app.command[0]} -> {found or 'not found, fix [apps.%s] in nova.toml' % name}")

    for folder in cfg.folders.allowed:
        say("OK" if folder.is_dir() else "WARN", f"allowed folder {folder}{'' if folder.is_dir() else ' does not exist'}")

    usb = cfg.folders.usb_mount_path
    if usb is None:
        say("WARN", "[usb] mount_path is empty. Plug in the pen drive and set it in nova.toml")
        user = os.environ.get("USER", "")
        for base in (Path("/media") / user, Path("/run/media") / user):
            if base.is_dir():
                for c in sorted(base.iterdir()):
                    say("INFO", f"candidate USB mount: {c}")
    else:
        say("OK" if usb.is_dir() else "WARN",
            f"USB {usb} {'is mounted' if usb.is_dir() else 'not found (plugged in?)'}")

    allowed = list(cfg.folders.allowed) + ([usb] if usb else [])
    if not cfg.projects:
        say("WARN", "no projects registered in config/projects.toml yet")
    for p in cfg.projects:
        if not p.path.is_dir():
            say("WARN", f"project '{p.name}': {p.path} not found")
        elif not inside(p.path, allowed):
            say("FAIL", f"project '{p.name}': {p.path} is outside the allowed folders")
        else:
            say("OK", f"project '{p.name}': {p.path}")
        say("OK" if shutil.which(p.test_command[0]) else "WARN",
            f"project '{p.name}' test command '{p.test_command[0]}' {'found' if shutil.which(p.test_command[0]) else 'not found'}")

    for label, env in (("LLM key", cfg.llm.api_key_env),
                       ("Picovoice key", cfg.voice.picovoice_key_env),
                       ("access token", cfg.safety.access_token_env)):
        say("OK" if get_secret(env) else "WARN", f"{label} ({env}): {'set' if get_secret(env) else 'missing, add it to .env'}")
    say("OK" if cfg.llm.model else "WARN",
        f"LLM model: {cfg.llm.model or 'not chosen yet (run scripts/check_llm.py --list-only)'}")

    ip = lan_ip()
    say("INFO", f"laptop LAN IP: {ip} (reserve this on the router)")
    say("OK" if port_free(cfg.server.port) else "WARN",
        f"port {cfg.server.port} {'free' if port_free(cfg.server.port) else 'already in use'}")
    try:
        with socket.create_connection((cfg.device.address, cfg.device.port), timeout=2):
            say("OK", f"ESP32 reachable at {cfg.device.address}:{cfg.device.port}")
    except OSError:
        say("INFO", f"ESP32 not reachable at {cfg.device.address}:{cfg.device.port} (expected until Phase 1)")


if __name__ == "__main__":
    check_python()
    check_session()
    check_config()
    print(f"\n{fails} failure(s).")
    sys.exit(1 if fails else 0)
