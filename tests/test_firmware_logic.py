"""Builds and runs the C++ unit tests for the firmware's pure logic (needs g++; skipped if missing)."""
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent


@pytest.mark.skipif(shutil.which("g++") is None, reason="g++ not installed (sudo apt install g++)")
def test_nova_logic_cpp(tmp_path):
    exe = tmp_path / "nova_logic_test"
    build = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-o", str(exe),
                            str(HERE / "firmware" / "nova_logic_test.cpp")],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "checks passed" in run.stdout
