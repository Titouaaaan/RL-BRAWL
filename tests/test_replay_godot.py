"""The Python module and the Godot DLL are two separate builds of the core.
A replay recorded in Python must re-simulate to the same final state in Godot,
otherwise watching replays would show games that never happened.

Needs Godot (GODOT env var, or Godot_v4*_win64_console.exe under Downloads)
and a built Godot DLL (run play.bat once). Skipped if Godot isn't found."""
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "train"))
import replay  # noqa: E402  (train/replay.py)


def find_godot():
    if os.environ.get("GODOT"):
        exe = Path(os.environ["GODOT"])
        console = exe.with_name(exe.stem + "_console.exe")  # the console build prints to stdout
        return console if console.exists() else exe
    found = sorted((Path.home() / "Downloads").glob("**/Godot_v4*_win64_console.exe"))
    return found[-1] if found else None


def verify(godot, path):
    out = subprocess.run([str(godot), "--headless", "--path", str(ROOT / "godot"), "--", f"--replay={path}", "--verify"],
                         capture_output=True, text=True, timeout=120)
    return out.returncode, out.stdout


@pytest.mark.skipif(find_godot() is None, reason="Godot not found")
def test_python_replay_verifies_in_godot(tmp_path):
    godot = find_godot()
    r = replay.play_and_record(ROOT / "data" / "moves", replay.random_policy, replay.random_policy)
    good = replay.save(r, tmp_path / "good.json")
    code, out = verify(godot, good)
    assert code == 0 and "REPLAY verified" in out, out

    r["final_hash"] = "0" * 16  # tamper: Godot must notice
    bad = replay.save(r, tmp_path / "bad.json")
    code, out = verify(godot, bad)
    assert code != 0 and "DESYNC" in out, out
