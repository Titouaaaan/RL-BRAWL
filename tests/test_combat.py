import json
import re
from pathlib import Path

import numpy as np
import pytest

import fightcore

MOVES = Path(__file__).parent.parent / "data" / "moves"
HITSTUN_PER_KB = 25.0  # GameConfig::hitstun_per_kb in core/sim.h


def load_move(name):
    text = (MOVES / f"{name}.json").read_text()
    return json.loads(re.sub(r"//.*", "", text))  # strip // comments


def act(env, p1=(0, 0, 0), p2=(0, 0, 0)):
    env.step(np.array([[p1, p2]], dtype=np.int8))


def test_jab_damage_knockback_hitstun():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    for _ in range(66):  # walk P1 right until P2 is in jab range
        act(env, p1=(1, 0, 0))
    for _ in range(10):  # hold light until the jab connects
        act(env, p1=(0, 0, 2))
        if env.fighter(0, 1)["percent"] > 0:
            break
    hb = load_move("jab")["hitboxes"][0]
    victim = env.fighter(0, 1)
    kb = hb["base_kb"] + hb["kb_growth"] * hb["damage"]  # uses % *after* the hit
    assert victim["percent"] == hb["damage"]
    assert victim["state"] == fightcore.ST_HITSTUN
    assert victim["hitstun"] == round(kb * HITSTUN_PER_KB)
    assert victim["launch_vx"] > 0  # sent away from P1, to the right
    assert abs(np.hypot(victim["launch_vx"], victim["launch_vy"]) - kb) < 1e-5


def test_walk_off_stage_loses_stock_and_respawns():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    for _ in range(600):
        act(env, p2=(-1, 0, 0))  # P2 walks left off the stage
        if env.fighter(0, 1)["stocks"] < 3:
            break
    p2 = env.fighter(0, 1)
    assert p2["stocks"] == 2
    assert p2["x"] == 10.0 and p2["invuln"] > 0  # back at spawn, protected


def test_bad_move_file_gives_readable_error(tmp_path):
    for name in ("jab", "heavy", "aerial"):
        (tmp_path / f"{name}.json").write_text((MOVES / f"{name}.json").read_text())
    (tmp_path / "heavy.json").write_text('{"total_frames": 40, "landing_lag": 0, "hitboxes": [{"start": 14}]}')
    with pytest.raises(ValueError, match=r"heavy\.json: hitbox 1: missing field \"end\""):
        fightcore.VecEnv(1, str(tmp_path))


def test_can_turn_in_air_but_not_mid_aerial():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    act(env, p1=(0, 0, 1))  # jump (P1 starts facing right)
    for _ in range(5):  # jumpsquat, then airborne, holding left
        act(env, p1=(-1, 0, 0))
    assert not env.fighter(0, 0)["grounded"]
    assert env.fighter(0, 0)["facing"] == -1  # turned around in the air
    act(env, p1=(-1, 0, 2))  # aerial toward the left
    for _ in range(3):
        act(env, p1=(1, 0, 0))  # stick right during the aerial...
    assert env.fighter(0, 0)["facing"] == -1  # ...doesn't flip the move


def test_spot_dodge_makes_a_jab_whiff():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    for _ in range(66):
        act(env, p1=(1, 0, 0))  # walk into jab range
    act(env, p2=(0, 0, 4))  # P2 starts a spot dodge...
    for _ in range(4):
        act(env, p1=(0, 0, 2), p2=(0, 0, 4))  # ...and P1 jabs into it
    assert env.fighter(0, 1)["intangible"]
    assert env.fighter(0, 1)["percent"] == 0  # the jab passed through


def test_air_dodge_leaves_you_helpless_until_landing():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    act(env, p1=(0, 0, 1))  # jump
    for _ in range(4):
        act(env)  # jumpsquat, then airborne
    act(env, p1=(0, 0, 4))  # air dodge
    for _ in range(30):
        act(env)
    f = env.fighter(0, 0)
    assert f["state"] == fightcore.ST_HELPLESS and not f["grounded"]
    act(env, p1=(0, 0, 1))  # jump is ignored while helpless
    assert env.fighter(0, 0)["state"] == fightcore.ST_HELPLESS
    for _ in range(200):
        act(env, p1=(0, 0, 2))  # attacks are ignored too, all the way down
        if env.fighter(0, 0)["grounded"]:
            break
    assert env.fighter(0, 0)["state"] == fightcore.ST_LANDING  # landing lag, then free
