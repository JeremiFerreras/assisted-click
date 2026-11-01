"""Compile and run all SDK-independent checks for Assisted Click.

Usage: python tests/run_checks.py
Set CXX to override the C++ compiler, e.g. CXX=clang++.
"""
from __future__ import annotations

import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CXX = shlex.split(os.environ.get("CXX", "c++"))


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="assisted-click-checks-") as temp:
        temp_path = Path(temp)
        core_bin = temp_path / ("core_tests.exe" if os.name == "nt" else "core_tests")
        orb_bin = temp_path / ("orb_assist_tests.exe" if os.name == "nt" else "orb_assist_tests")
        common = [*CXX, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-Isrc"]
        run([*common, "tests/core_tests.cpp", "-o", str(core_bin)])
        run([str(core_bin)])
        run([*common, "tests/orb_assist_tests.cpp", "-o", str(orb_bin)])
        run([str(orb_bin)])
        run([sys.executable, "tests/verify_video_profile.py"])
    print("All SDK-independent checks passed. This is not a substitute for a Geode build or live GD test.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
