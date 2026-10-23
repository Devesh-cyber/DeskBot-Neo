"""Nova configuration loader (Phase 0).

Reads config/nova.toml, config/projects.toml and an optional .env file.
No third-party dependencies: TOML comes from tomllib (Python 3.11+) or tomli (older).
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # Python 3.10 and older: pip install tomli
    import tomli as tomllib  # type: ignore[no-redef]


class ConfigError(Exception):
    """Raised with a human-readable message when the configuration is wrong."""


_MISSING = object()


# ---------- small typed sections ----------
@dataclass(frozen=True)
class ServerCfg:
    host: str
    port: int


@dataclass(frozen=True)
class DeviceCfg:
    address: str
    port: int
    heartbeat_s: int
    disconnect_after_s: int


@dataclass(frozen=True)
class FoldersCfg:
    allowed: list[Path]
    usb_mount_path: Path | None
    usb_required: bool


@dataclass(frozen=True)
class AppEntry:
    name: str
    command: list[str]


@dataclass(frozen=True)
class LlmCfg:
    provider: str
    base_url: str
    model: str
    api_key_env: str
    timeout_s: int


@dataclass(frozen=True)
class VoiceCfg:
    assistant_name: str
    wake_phrase: str
    wake_engine: str
    picovoice_key_env: str
    stt_engine: str
    stt_model: str
    tts_engine: str
    mute_mic_while_speaking: bool


@dataclass(frozen=True)
class SafetyCfg:
    max_steps: int
    tool_timeout_s: int
    output_limit_chars: int
    confirm_mode: str
    access_token_env: str


@dataclass(frozen=True)
class ProjectEntry:
    name: str
    path: Path
    test_command: list[str]
    timeout_s: int


@dataclass(frozen=True)
class NovaConfig:
    root: Path
    server: ServerCfg
    device: DeviceCfg
    folders: FoldersCfg
    apps: dict[str, AppEntry]
    llm: LlmCfg
    voice: VoiceCfg
    safety: SafetyCfg
    conversation: dict[str, int]
    activity_db: Path
    log_level: str
    projects: list[ProjectEntry]


# ---------- helpers ----------
def find_repo_root(start: Path | None = None) -> Path:
    """Walk up from `start` (default: this file) until config/nova.toml is found."""
    here = (start or Path(__file__)).resolve()
    for parent in [here, *here.parents]:
        if (parent / "config" / "nova.toml").is_file():
            return parent
    raise ConfigError("Could not find config/nova.toml in this folder or any parent folder.")


def load_env(path: Path) -> dict[str, str]:
    """Parse a simple KEY=VALUE file. Ignores blank lines and # comments."""
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key:
            values[key] = value
    return values


def get_secret(name: str) -> str | None:
    """Return a secret from the environment, or None if missing or empty."""
    value = os.environ.get(name, "").strip()
    return value or None


def _read_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as handle:
            return tomllib.load(handle)
    except FileNotFoundError:
        raise ConfigError(f"{path} not found.") from None
    except tomllib.TOMLDecodeError as exc:
        raise ConfigError(f"{path.name} is not valid TOML: {exc}") from None


def _get(raw: dict[str, Any], section: str, key: str, typ: type, default: Any = _MISSING) -> Any:
    sec = raw.get(section)
    if not isinstance(sec, dict):
        raise ConfigError(f"[{section}] section is missing in nova.toml")
    if key not in sec:
        if default is _MISSING:
            raise ConfigError(f"[{section}] {key} is missing in nova.toml")
        return default
    value = sec[key]
    wrong_bool = typ is int and isinstance(value, bool)
    if not isinstance(value, typ) or wrong_bool:
        raise ConfigError(f"[{section}] {key} must be of type {typ.__name__}, got {value!r}")
    return value


def _command(value: Any, where: str) -> list[str]:
    if not isinstance(value, list) or not value or not all(isinstance(v, str) and v for v in value):
        raise ConfigError(f"{where} must be a non-empty list of strings, e.g. [\"firefox\"]")
    return list(value)


def _abs_path(text: str, where: str) -> Path:
    path = Path(text).expanduser()
    if not path.is_absolute():
        raise ConfigError(f"{where} must be an absolute path (or start with ~), got {text!r}")
    return path


# ---------- main loader ----------
def load_config(root: Path | None = None, *, load_dotenv: bool = True) -> NovaConfig:
    root = (root or find_repo_root()).resolve()
    if load_dotenv:
        for key, value in load_env(root / ".env").items():
            os.environ.setdefault(key, value)  # real environment variables win

    raw = _read_toml(root / "config" / "nova.toml")

    allowed_raw = _get(raw, "folders", "allowed", list)
    if not allowed_raw or not all(isinstance(p, str) for p in allowed_raw):
        raise ConfigError("[folders] allowed must be a non-empty list of folder paths")
    allowed = [_abs_path(p, "[folders] allowed entry") for p in allowed_raw]

    usb_text = _get(raw, "usb", "mount_path", str, "")
    usb_path = _abs_path(usb_text, "[usb] mount_path") if usb_text else None

    apps_raw = raw.get("apps", {})
    if not isinstance(apps_raw, dict):
        raise ConfigError("[apps] must contain one table per app, e.g. [apps.browser]")
    apps = {
        name: AppEntry(name, _command(entry.get("command") if isinstance(entry, dict) else None,
                                      f"[apps.{name}] command"))
        for name, entry in apps_raw.items()
    }

    confirm_mode = _get(raw, "safety", "confirm_mode", str, "either")
    if confirm_mode not in ("either", "both"):
        raise ConfigError("[safety] confirm_mode must be \"either\" or \"both\"")

    conv = {
        "max_turns": _get(raw, "conversation", "max_turns", int, 8),
        "idle_reset_minutes": _get(raw, "conversation", "idle_reset_minutes", int, 10),
    }

    activity_db = Path(_get(raw, "logging", "activity_db", str, "data/activity.db"))
    if not activity_db.is_absolute():
        activity_db = root / activity_db

    cfg = NovaConfig(
        root=root,
        server=ServerCfg(_get(raw, "server", "host", str), _get(raw, "server", "port", int)),
        device=DeviceCfg(
            _get(raw, "device", "address", str),
            _get(raw, "device", "port", int, 80),
            _get(raw, "device", "heartbeat_s", int, 5),
            _get(raw, "device", "disconnect_after_s", int, 15),
        ),
        folders=FoldersCfg(allowed, usb_path, _get(raw, "usb", "required_for_projects", bool, True)),
        apps=apps,
        llm=LlmCfg(
            _get(raw, "llm", "provider", str),
            _get(raw, "llm", "base_url", str).rstrip("/"),
            _get(raw, "llm", "model", str, ""),
            _get(raw, "llm", "api_key_env", str),
            _get(raw, "llm", "timeout_s", int, 30),
        ),
        voice=VoiceCfg(
            _get(raw, "voice", "assistant_name", str),
            _get(raw, "voice", "wake_phrase", str),
            _get(raw, "voice", "wake_engine", str),
            _get(raw, "voice", "picovoice_key_env", str),
            _get(raw, "voice", "stt_engine", str),
            _get(raw, "voice", "stt_model", str),
            _get(raw, "voice", "tts_engine", str),
            _get(raw, "voice", "mute_mic_while_speaking", bool, True),
        ),
        safety=SafetyCfg(
            _get(raw, "safety", "max_steps", int, 5),
            _get(raw, "safety", "tool_timeout_s", int, 60),
            _get(raw, "safety", "output_limit_chars", int, 4000),
            confirm_mode,
            _get(raw, "safety", "access_token_env", str, "NOVA_TOKEN"),
        ),
        conversation=conv,
        activity_db=activity_db,
        log_level=_get(raw, "logging", "level", str, "INFO"),
        projects=load_projects(root / "config" / "projects.toml"),
    )
    return cfg


def load_projects(path: Path) -> list[ProjectEntry]:
    """Load the project registry. A missing file or an empty registry gives []."""
    if not path.is_file():
        return []
    raw = _read_toml(path)
    entries = raw.get("projects", [])
    if not isinstance(entries, list):
        raise ConfigError("projects.toml: use [[projects]] tables")
    projects: list[ProjectEntry] = []
    seen: set[str] = set()
    for i, entry in enumerate(entries, start=1):
        if not isinstance(entry, dict):
            raise ConfigError(f"projects.toml: entry {i} is not a table")
        name = entry.get("name")
        if not isinstance(name, str) or not name.strip():
            raise ConfigError(f"projects.toml: entry {i} needs a name")
        key = name.strip().lower()
        if key in seen:
            raise ConfigError(f"projects.toml: duplicate project name {name!r}")
        seen.add(key)
        path_text = entry.get("path")
        if not isinstance(path_text, str):
            raise ConfigError(f"projects.toml: project {name!r} needs a path")
        timeout = entry.get("timeout_s", 120)
        if not isinstance(timeout, int) or isinstance(timeout, bool) or timeout <= 0:
            raise ConfigError(f"projects.toml: project {name!r} timeout_s must be a positive integer")
        projects.append(ProjectEntry(
            name=name.strip(),
            path=_abs_path(path_text, f"projects.toml: project {name!r} path"),
            test_command=_command(entry.get("test_command"), f"projects.toml: project {name!r} test_command"),
            timeout_s=timeout,
        ))
    return projects
