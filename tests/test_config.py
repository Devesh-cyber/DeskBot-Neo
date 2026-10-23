import os
import shutil
from pathlib import Path

import pytest

from nova.config import (
    ConfigError, find_repo_root, get_secret, load_config, load_env, load_projects,
)

REPO = Path(__file__).resolve().parents[1]


@pytest.fixture()
def root(tmp_path):
    """A copy of the real config folder, so tests can edit it freely."""
    shutil.copytree(REPO / "config", tmp_path / "config")
    return tmp_path


def edit(root: Path, old: str, new: str, name: str = "nova.toml") -> None:
    path = root / "config" / name
    text = path.read_text()
    assert old in text, f"{old!r} not in {name}"
    path.write_text(text.replace(old, new, 1))


def test_shipped_config_loads(root):
    cfg = load_config(root, load_dotenv=False)
    assert cfg.server.port == 8000
    assert cfg.voice.wake_phrase == "Hey Nova"
    assert cfg.safety.max_steps == 5
    assert set(cfg.apps) == {"browser", "file_manager", "vscode", "text_editor"}
    assert cfg.apps["browser"].command == ["firefox"]
    assert cfg.projects == []
    assert cfg.folders.usb_mount_path is None


def test_allowed_folders_are_expanded(root):
    cfg = load_config(root, load_dotenv=False)
    assert all(p.is_absolute() for p in cfg.folders.allowed)
    assert cfg.folders.allowed[0] == Path("~/Downloads").expanduser()


def test_usb_path_loaded(root):
    edit(root, 'mount_path = ""', 'mount_path = "/media/devesh/PEN"')
    assert load_config(root, load_dotenv=False).folders.usb_mount_path == Path("/media/devesh/PEN")


def test_activity_db_is_inside_repo(root):
    assert load_config(root, load_dotenv=False).activity_db == root / "data" / "activity.db"


def test_missing_section_gives_clear_error(root):
    edit(root, "[safety]", "[safety_renamed]")
    with pytest.raises(ConfigError, match=r"\[safety\]"):
        load_config(root, load_dotenv=False)


def test_missing_required_key(root):
    edit(root, 'address = "192.168.0.108"\n', "")
    with pytest.raises(ConfigError, match="address is missing"):
        load_config(root, load_dotenv=False)


def test_wrong_type(root):
    edit(root, "port = 8000", 'port = "8000"')
    with pytest.raises(ConfigError, match="must be of type int"):
        load_config(root, load_dotenv=False)


def test_bool_is_not_an_int(root):
    edit(root, "port = 8000", "port = true")
    with pytest.raises(ConfigError, match="must be of type int"):
        load_config(root, load_dotenv=False)


def test_relative_folder_rejected(root):
    edit(root, '"~/Downloads"', '"Downloads"')
    with pytest.raises(ConfigError, match="absolute"):
        load_config(root, load_dotenv=False)


def test_bad_confirm_mode(root):
    edit(root, 'confirm_mode = "either"', 'confirm_mode = "maybe"')
    with pytest.raises(ConfigError, match="confirm_mode"):
        load_config(root, load_dotenv=False)


def test_bad_app_command(root):
    edit(root, 'command = ["firefox"]', 'command = "firefox"')
    with pytest.raises(ConfigError, match="non-empty list"):
        load_config(root, load_dotenv=False)


def test_invalid_toml(root):
    (root / "config" / "nova.toml").write_text("[server\nport = 1")
    with pytest.raises(ConfigError, match="not valid TOML"):
        load_config(root, load_dotenv=False)


def test_projects_registry(root):
    (root / "config" / "projects.toml").write_text(
        '[[projects]]\nname = "Desk Bot"\npath = "/media/x/p/desk_bot"\n'
        'test_command = ["python3", "-m", "pytest", "-q"]\ntimeout_s = 90\n'
    )
    projects = load_config(root, load_dotenv=False).projects
    assert len(projects) == 1
    assert projects[0].name == "Desk Bot"
    assert projects[0].test_command == ["python3", "-m", "pytest", "-q"]
    assert projects[0].timeout_s == 90


def test_project_errors(tmp_path):
    p = tmp_path / "projects.toml"
    p.write_text('[[projects]]\nname = "a"\npath = "/x"\n')  # no test_command
    with pytest.raises(ConfigError, match="test_command"):
        load_projects(p)
    p.write_text('[[projects]]\nname = "a"\npath = "/x"\ntest_command = ["t"]\n'
                 '[[projects]]\nname = "A"\npath = "/y"\ntest_command = ["t"]\n')
    with pytest.raises(ConfigError, match="duplicate"):
        load_projects(p)
    p.write_text('[[projects]]\nname = "a"\npath = "relative"\ntest_command = ["t"]\n')
    with pytest.raises(ConfigError, match="absolute"):
        load_projects(p)


def test_missing_projects_file_is_empty(tmp_path):
    assert load_projects(tmp_path / "nope.toml") == []


def test_env_parsing(tmp_path):
    f = tmp_path / ".env"
    f.write_text('# comment\n\nA=1\nB = "two"\nC=\nbad line\n')
    assert load_env(f) == {"A": "1", "B": "two", "C": ""}
    assert load_env(tmp_path / "missing") == {}


def test_dotenv_does_not_override_real_environment(root, monkeypatch):
    (root / ".env").write_text("XAI_API_KEY=from_file\nNOVA_TOKEN=tok\n")
    monkeypatch.setenv("XAI_API_KEY", "from_env")
    monkeypatch.delenv("NOVA_TOKEN", raising=False)
    load_config(root)
    assert get_secret("XAI_API_KEY") == "from_env"
    assert get_secret("NOVA_TOKEN") == "tok"
    monkeypatch.delenv("NOVA_TOKEN", raising=False)


def test_empty_secret_counts_as_missing(monkeypatch):
    monkeypatch.setenv("SOME_KEY", "  ")
    assert get_secret("SOME_KEY") is None
    monkeypatch.delenv("SOME_KEY")
    assert get_secret("SOME_KEY") is None


def test_find_repo_root(root):
    deep = root / "a" / "b"
    deep.mkdir(parents=True)
    assert find_repo_root(deep) == root.resolve()


def test_find_repo_root_fails_cleanly(tmp_path):
    with pytest.raises(ConfigError):
        find_repo_root(tmp_path)
