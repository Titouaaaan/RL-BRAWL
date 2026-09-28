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


def approach(env, gap):
    """P1 runs right until the fighters are closer than `gap`, then lets the dash/run slide stop."""
    while env.fighter(0, 1)["x"] - env.fighter(0, 0)["x"] > gap:
        act(env, p1=(1, 0, 0))
    for _ in range(12):
        act(env)


def test_jab_damage_knockback_hitstun():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    approach(env, 3.2)  # into jab range
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
        act(env, p2=(-1, 0, 0))  # P2 runs left off the stage
        if env.fighter(0, 1)["stocks"] < 3:
            break
    p2 = env.fighter(0, 1)
    assert p2["stocks"] == 2
    assert p2["x"] == 10.0 and p2["invuln"] > 0  # back at spawn, protected


def test_bad_move_file_gives_readable_error(tmp_path):
    for name in fightcore.MOVE_NAMES:
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
    approach(env, 3.2)  # into jab range
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


def move_id(name):
    return fightcore.MOVE_NAMES.index(name)


def test_stick_picks_the_move():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    act(env, p1=(0, 1, 2))  # light + up on the ground
    assert env.fighter(0, 0)["move"] == move_id("up_tilt")
    for _ in range(25):
        act(env)
    act(env, p1=(0, 0, 1))  # jump...
    for _ in range(4):
        act(env)
    act(env, p1=(0, -1, 3))  # ...heavy + down in the air
    assert env.fighter(0, 0)["move"] == move_id("heavy_down_air")


def test_dash_then_run():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    xs = [env.fighter(0, 0)["x"]]
    for _ in range(20):
        act(env, p1=(1, 0, 0))
        xs.append(env.fighter(0, 0)["x"])
    speeds = np.diff(xs)
    assert np.allclose(speeds[:8], 0.45) and np.allclose(speeds[10:], 0.34)  # GameConfig dash/run speeds


def test_grounded_fighters_cannot_overlap():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    for _ in range(120):  # P1 runs into the standing P2 and keeps pushing
        act(env, p1=(1, 0, 0))
        gap = env.fighter(0, 1)["x"] - env.fighter(0, 0)["x"]
        assert gap > 1.999  # hurtboxes are 2 wide: they touch but never overlap


def test_cannot_push_someone_off_the_stage():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    for _ in range(300):  # P2 runs left into P1 and keeps shoving toward the edge
        act(env, p2=(-1, 0, 0))
    p1, p2 = env.fighter(0, 0), env.fighter(0, 1)
    assert p1["grounded"] and p1["stocks"] == 3 and p1["x"] >= -30.0  # pinned at the edge, still on stage
    assert p2["x"] - p1["x"] > 1.999


def jump_to_apex(env):
    act(env, p1=(0, 0, 1))
    for _ in range(25):  # jumpsquat, then rise until falling
        act(env)


def test_holding_down_fast_falls():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    jump_to_apex(env)
    for _ in range(5):
        act(env, p1=(0, -1, 0))
    assert not env.fighter(0, 0)["fastfall"]  # 5 frames: not yet
    act(env, p1=(0, -1, 0))
    assert env.fighter(0, 0)["fastfall"]  # 6th frame held: fast-fall


def test_down_plus_attack_is_a_move_not_a_drop():
    env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
    jump_to_apex(env)
    for _ in range(10):  # hold down + light: down-air, and no fast-fall
        act(env, p1=(0, -1, 2))
    f = env.fighter(0, 0)
    assert f["move"] == move_id("down_air") and not f["fastfall"]


def test_every_move_is_reachable():
    # Each (ground/air, light/heavy, stick down/neutral/up) picks its own move.
    seen = set()
    for button in (2, 3):
        for sy in (-1, 0, 1):
            env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
            act(env, p1=(0, sy, button))  # on the ground
            seen.add(env.fighter(0, 0)["move"])
            env = fightcore.VecEnv(1, str(MOVES), frame_skip=1)
            jump_to_apex(env)
            act(env, p1=(0, sy, button))  # in the air
            seen.add(env.fighter(0, 0)["move"])
    assert seen == set(range(len(fightcore.MOVE_NAMES)))
