"""Game mechanics, tested through the Python env one frame at a time
(frame_skip=1). Actions are egocentric: for P2, +1 on the stick is "toward
the left of the screen" at the start, i.e. toward P1."""
import json
import re
from pathlib import Path

import numpy as np
import pytest

import fightcore

MOVES = Path(__file__).parent.parent / "data" / "moves"
FRESH = 1.05           # GameConfig::fresh_bonus: a move not used recently hits a bit harder
WEIGHT = fightcore.WEIGHT
S = {name: i for i, name in enumerate(fightcore.STATE_NAMES)}


def load_move(name):
    text = (MOVES / f"{name}.json").read_text()
    return json.loads(re.sub(r"//.*", "", text))  # strip // comments


def smash_knockback(hb, percent_after, damage):
    """SmashWiki "Knockback" formula (core/sim.cpp knockback())."""
    p = percent_after
    kb = (p / 10 + p * damage / 20) * 200 / (WEIGHT + 100) * 1.4 + 18
    return kb * hb["kb_growth"] / 100 + hb["base_kb"]


def move_id(name):
    return fightcore.MOVE_NAMES.index(name)


def new_env():
    return fightcore.VecEnv(1, str(MOVES), frame_skip=1)


def act(env, p1=(0, 0, 0), p2=(0, 0, 0)):
    env.step(np.array([[p1, p2]], dtype=np.int8))


def f(env, p):
    return env.fighter(0, p)


def approach(env, gap):
    """P1 runs right until the fighters are closer than `gap`, then stops."""
    while f(env, 1)["x"] - f(env, 0)["x"] > gap:
        act(env, p1=(1, 0, 0))
    for _ in range(20):
        act(env)


def face_off(env, p2=(0, 0, 0)):
    """Both run to the middle of the stage (clear of the platforms) and stand
    face to face, pushing against each other. P2 then does `p2` every frame."""
    for _ in range(200):
        a = (1, 0, 0) if f(env, 0)["x"] < -1.5 else (0, 0, 0)
        b = (1, 0, 0) if f(env, 1)["x"] > 1.5 else (0, 0, 0)
        act(env, p1=a, p2=b)
    for _ in range(3):
        act(env, p2=p2)


def to_center(env):
    """P1 runs to the middle (no platform overhead low enough to land on)."""
    while f(env, 0)["x"] < -0.5:
        act(env, p1=(1, 0, 0))
    for _ in range(25):
        act(env)


def jump_to_apex(env):
    act(env, p1=(0, 0, 1))
    for _ in range(60):  # jumpsquat, then rise until falling
        act(env, p1=(0, 0, 1))
        if not f(env, 0)["grounded"] and f(env, 0)["vy"] <= 0:
            break


def hold(env, frames, p1=(0, 0, 0), p2=(0, 0, 0)):
    for _ in range(frames):
        act(env, p1=p1, p2=p2)


def run_out_move(env):
    while f(env, 0)["state"] == S["ATTACK"]:
        act(env)


# --- Basics ------------------------------------------------------------------

def test_jab_damage_knockback_hitstun():
    env = new_env()
    approach(env, 3.2)  # into jab range
    for _ in range(10):  # hold light until the jab connects
        act(env, p1=(0, 0, 2))
        if f(env, 1)["percent"] > 0:
            break
    hb = load_move("jab")["hitboxes"][0]
    victim = f(env, 1)
    damage = hb["damage"] * FRESH
    kb = smash_knockback(hb, damage, damage)  # uses % *after* the hit
    assert victim["percent"] == pytest.approx(damage)
    assert victim["state"] == fightcore.ST_HITSTUN
    assert victim["hitstun"] == int(kb * 0.4) - 1  # Ultimate: 0.4 frames per knockback unit, minus 1
    assert victim["launch_vx"] > 0  # sent away from P1, to the right...
    assert victim["launch_vy"] == 0  # ...along the ground: a weak Sakurai-angle hit
    assert victim["launch_vx"] == pytest.approx(kb * fightcore.LAUNCH_SPEED_SCALE)


def test_hitlag_freezes_both_fighters():
    env = new_env()
    approach(env, 3.2)
    for _ in range(10):
        act(env, p1=(0, 0, 2))
        if f(env, 1)["percent"] > 0:
            break
    a, v = f(env, 0), f(env, 1)
    assert a["hitlag"] == v["hitlag"] > 0
    x = v["x"]
    act(env)
    assert f(env, 1)["x"] == x  # frozen: the launch hasn't started yet


def test_walk_off_stage_loses_stock_and_respawns():
    env = new_env()
    for _ in range(600):
        # P2 runs off the right edge, holding down once airborne so it doesn't grab the ledge
        act(env, p2=(-1, 0, 0) if f(env, 1)["grounded"] else (-1, -1, 0))
        if f(env, 1)["stocks"] < 3:
            break
    p2 = f(env, 1)
    assert p2["stocks"] == 2
    assert p2["x"] == fightcore.SPAWN_X and p2["invuln"] == 120 and p2["respawn_wait"] > 0  # waiting off the field
    for _ in range(p2["respawn_wait"]):
        act(env, p2=(0, 0, 1))  # inputs are ignored while waiting
    p2 = f(env, 1)
    assert p2["respawn_wait"] == 0 and p2["invuln"] == 120 and p2["y"] == 14.0  # the drop-in starts with full i-frames


def test_bad_move_file_gives_readable_error(tmp_path):
    for name in fightcore.MOVE_NAMES:
        (tmp_path / f"{name}.json").write_text((MOVES / f"{name}.json").read_text())
    (tmp_path / "heavy.json").write_text('{"total_frames": 40, "landing_lag": 0, "hitboxes": [{"start": 14}]}')
    with pytest.raises(ValueError, match=r"heavy\.json: hitbox 1: missing field \"end\""):
        fightcore.VecEnv(1, str(tmp_path))


def test_dash_then_run():
    env = new_env()
    xs = [f(env, 0)["x"]]
    for _ in range(30):
        act(env, p1=(1, 0, 0))
        xs.append(f(env, 0)["x"])
    speeds = np.diff(xs)
    assert speeds[0] > 0.25                        # an instant burst: that's the dash
    assert max(speeds[:12]) == pytest.approx(0.387, abs=1e-4)  # Mario's dash speed...
    assert speeds[-1] == pytest.approx(0.352, abs=1e-4)        # ...settling into his run


def test_grounded_fighters_cannot_overlap():
    env = new_env()
    for _ in range(120):  # P1 runs into the standing P2 and keeps pushing
        act(env, p1=(1, 0, 0))
        assert f(env, 1)["x"] - f(env, 0)["x"] > 1.999  # hurtboxes are 2 wide: they touch, never overlap


def test_cannot_push_someone_off_the_stage():
    env = new_env()
    hold(env, 300, p2=(1, 0, 0))  # P2 runs left into P1 and keeps shoving toward the edge
    p1, p2 = f(env, 0), f(env, 1)
    assert p1["grounded"] and p1["stocks"] == 3 and p1["x"] >= -30.0  # pinned at the edge, still on stage
    assert p2["x"] - p1["x"] > 1.999


# --- Jumps, fast-fall, platforms -----------------------------------------------

def peak_height(p1_inputs):
    env = new_env()
    to_center(env)
    top = 0.0
    for a in p1_inputs:
        act(env, p1=a)
        top = max(top, f(env, 0)["y"])
    return top


def test_short_hop_is_lower_than_a_full_hop():
    full = peak_height([(0, 0, 1)] * 10 + [(0, 0, 0)] * 60)
    tapped = peak_height([(0, 0, 1)] + [(0, 0, 0)] * 70)        # let go during jumpsquat
    down_jump = peak_height([(0, -1, 1)] * 10 + [(0, 0, 0)] * 60)  # down + jump
    assert tapped < full * 0.6 and down_jump == pytest.approx(tapped)


def test_holding_down_fast_falls():
    env = new_env()
    to_center(env)
    jump_to_apex(env)
    hold(env, 5, p1=(0, -1, 0))
    assert not f(env, 0)["fastfall"]  # 5 frames: not yet
    act(env, p1=(0, -1, 0))
    assert f(env, 0)["fastfall"]  # 6th frame held: fast-fall


def test_land_on_a_platform_and_drop_through():
    env = new_env()  # P1 starts at x = -8.8, under the left platform (y = 4.8)
    act(env, p1=(0, 0, 1))
    for _ in range(120):
        act(env, p1=(0, 0, 1))
        if f(env, 0)["grounded"] and f(env, 0)["y"] > 0:
            break
    assert f(env, 0)["y"] == pytest.approx(4.8) and f(env, 0)["surface"] == 1
    for _ in range(120):
        act(env, p1=(0, -1, 0))  # down: drop through
        if f(env, 0)["grounded"] and f(env, 0)["y"] == 0:
            break
    assert f(env, 0)["surface"] == 0  # back on the main stage


# --- Moves -----------------------------------------------------------------------

def test_stick_picks_the_move():
    env = new_env()
    act(env, p1=(0, 1, 2))  # light + up on the ground
    assert f(env, 0)["move"] == move_id("up_tilt")
    hold(env, 30)  # up-tilt lasts 29 frames
    act(env, p1=(0, 0, 1))  # jump...
    hold(env, 4)
    act(env, p1=(0, -1, 3))  # ...heavy + down in the air
    assert f(env, 0)["move"] == move_id("heavy_down_air")


def test_every_attack_is_reachable():
    # Each (ground/air, light/heavy, stick down/neutral/up) picks its own move.
    seen = set()
    for button in (2, 3):
        for sy in (-1, 0, 1):
            env = new_env()
            act(env, p1=(0, sy, button))  # on the ground
            seen.add(f(env, 0)["move"])
            env = new_env()
            to_center(env)
            jump_to_apex(env)
            act(env, p1=(0, sy, button))  # in the air
            seen.add(f(env, 0)["move"])
    assert seen == set(range(move_id("grab")))  # the 12 attacks (grab and throws come from the shield)


def test_down_plus_attack_is_a_move_not_a_drop():
    env = new_env()
    to_center(env)
    jump_to_apex(env)
    hold(env, 10, p1=(0, -1, 2))  # hold down + light: down-air, and no fast-fall
    assert f(env, 0)["move"] == move_id("down_air") and not f(env, 0)["fastfall"]


def test_holding_down_after_a_down_air_does_not_fast_fall():
    env = new_env()
    to_center(env)
    jump_to_apex(env)
    act(env, p1=(0, -1, 2))  # down-air...
    for _ in range(40):
        act(env, p1=(0, -1, 0))  # ...then keep holding down, attack released
        if f(env, 0)["grounded"]:
            break
    assert not f(env, 0)["fastfall"]  # the down was "used" by the attack
    env2 = new_env()
    to_center(env2)
    jump_to_apex(env2)
    act(env2, p1=(0, -1, 2))
    act(env2)  # stick back to neutral...
    hold(env2, 6, p1=(0, -1, 0))  # ...then a fresh hold of down
    assert f(env2, 0)["fastfall"]


def test_two_missed_air_attacks_lock_air_attacks_until_landing():
    env = new_env()
    to_center(env)  # far from P2: every air attack misses
    hold(env, 5, p1=(0, 0, 1))  # full hop
    act(env, p1=(0, 1, 2))  # first air attack (up-air: short enough to finish in the air), misses
    run_out_move(env)
    assert not f(env, 0)["air_attack_locked"]  # one miss is allowed
    act(env, p1=(0, 0, 1))  # double jump for more air time
    act(env, p1=(0, 1, 2))  # second air attack, misses
    run_out_move(env)
    assert f(env, 0)["air_attack_locked"] and not f(env, 0)["grounded"]
    act(env, p1=(0, 0, 3))  # another attack: refused
    assert f(env, 0)["state"] != S["ATTACK"]
    act(env, p1=(0, 0, 4))  # dodging is still allowed
    assert f(env, 0)["state"] == fightcore.ST_AIRDODGE
    for _ in range(200):
        act(env)
        if f(env, 0)["grounded"]:
            break
    assert not f(env, 0)["air_attack_locked"]  # landing clears it


def test_connected_air_attack_keeps_air_attacks():
    env = new_env()
    face_off(env)
    hold(env, 5, p1=(0, 0, 1), p2=(0, 0, 1))  # both full hop together, so they're level...
    act(env, p1=(0, 1, 2))  # ...and P1 up-airs into P2
    run_out_move(env)
    assert f(env, 1)["percent"] > 0 and not f(env, 0)["grounded"]  # it connected, still airborne
    assert not f(env, 0)["air_attack_locked"]
    act(env, p1=(0, 1, 2))  # a follow-up up-air comes out
    assert f(env, 0)["move"] == move_id("up_air")


def landing_lag_for_aerial_started_at(height):
    env = new_env()
    to_center(env)
    jump_to_apex(env)
    for _ in range(120):
        me = f(env, 0)
        if me["y"] < height and me["vy"] < 0:
            break
        act(env)
    act(env, p1=(0, 0, 2))  # air attack
    for _ in range(60):
        act(env)
        if f(env, 0)["grounded"]:
            return f(env, 0)["landing_lag"]


def test_aerial_auto_cancel_windows():
    air = load_move("air")
    assert landing_lag_for_aerial_started_at(1.0) == 2  # lands on move frame 1-3: auto-cancel, normal landing
    assert landing_lag_for_aerial_started_at(6.0) == air["landing_lag"]  # lands mid-move: the aerial's lag


def test_repeating_a_move_weakens_it():
    env = new_env()
    hits = []
    for _ in range(3):
        face_off(env)
        before = f(env, 1)["percent"]
        for _ in range(10):
            act(env, p1=(0, 0, 2))
            if f(env, 1)["percent"] > before:
                break
        hits.append(f(env, 1)["percent"] - before)
        hold(env, 60)  # let P2 recover
    base = load_move("jab")["hitboxes"][0]["damage"]
    assert hits[0] == pytest.approx(base * FRESH, abs=1e-4)   # fresh: a small bonus
    assert hits[1] == pytest.approx(base * (1 - 0.08), abs=1e-4)  # used once: -8%
    assert hits[2] < hits[1]  # used twice: weaker still


def test_di_bends_the_launch():
    angles = []
    for di in ((0, 0, 0), (0, 1, 0)):  # no DI, then P2 holds up while hit
        env = new_env()
        approach(env, 3.2)
        for _ in range(10):
            act(env, p1=(0, 0, 2), p2=di)
            if f(env, 1)["percent"] > 0:
                break
        while f(env, 1)["hitlag"] > 0:
            act(env, p2=di)
        v = f(env, 1)
        angles.append(np.degrees(np.arctan2(v["launch_vy"], abs(v["launch_vx"]))))
    assert angles[1] - angles[0] == pytest.approx(15 * np.cos(np.radians(angles[0])), abs=0.5)


# --- Dodges, shield, grabs -------------------------------------------------------

def test_spot_dodge_makes_a_jab_whiff():
    env = new_env()
    approach(env, 3.2)
    act(env, p2=(0, -1, 4))  # P2 spot dodges (dodge + down)...
    for _ in range(4):
        act(env, p1=(0, 0, 2), p2=(0, -1, 4))  # ...and P1 jabs into it
    assert f(env, 1)["intangible"] and f(env, 1)["percent"] == 0


def test_air_dodge_leaves_you_helpless_until_landing():
    env = new_env()
    to_center(env)
    hold(env, 5, p1=(0, 0, 1))  # hold jump: a full hop (a tap would be a short hop)
    act(env, p1=(0, 0, 4))  # air dodge
    hold(env, 30)
    assert f(env, 0)["state"] == fightcore.ST_HELPLESS and not f(env, 0)["grounded"]
    act(env, p1=(0, 0, 1))  # jump is ignored while helpless
    assert f(env, 0)["state"] == fightcore.ST_HELPLESS
    for _ in range(200):
        act(env, p1=(0, 0, 2))  # attacks are ignored too, all the way down
        if f(env, 0)["grounded"]:
            break
    assert f(env, 0)["state"] == fightcore.ST_LANDING


def test_shield_blocks_and_wears_down():
    env = new_env()
    face_off(env, p2=(0, 0, 4))  # P2 holds shield
    for _ in range(10):
        act(env, p1=(0, 0, 2), p2=(0, 0, 4))
    p2 = f(env, 1)
    assert p2["percent"] == 0 and p2["shield_hp"] < 50
    hold(env, 400, p2=(0, 0, 4))  # holding it too long breaks it
    assert f(env, 1)["state"] == S["SHIELDBREAK"]


def test_grab_beats_shield_and_throws():
    env = new_env()
    face_off(env, p2=(0, 0, 4))  # P2 shields
    act(env, p1=(0, 0, 4), p2=(0, 0, 4))  # P1 shields too...
    for _ in range(10):
        act(env, p1=(0, 0, 2), p2=(0, 0, 4))  # ...then light: grab
        if f(env, 1)["state"] == S["GRABBED"]:
            break
    assert f(env, 1)["state"] == S["GRABBED"] and f(env, 0)["state"] == S["HOLDING"]
    hold(env, 4)
    for _ in range(40):
        act(env, p1=(0, 1, 0))  # up: up throw
        if f(env, 1)["percent"] > 0:
            break
    assert f(env, 1)["percent"] > 0 and f(env, 1)["launch_vy"] > 0


def test_spot_dodge_beats_grab_and_attack_beats_grab():
    env = new_env()
    face_off(env)
    act(env, p1=(0, 0, 4), p2=(0, -1, 4))  # P1 shields, P2 spot dodges
    hold(env, 10, p1=(0, 0, 2), p2=(0, -1, 4))  # P1 grabs into the dodge
    assert f(env, 1)["state"] != S["GRABBED"]

    env = new_env()
    face_off(env)
    act(env, p1=(0, 0, 4))  # P1 shields...
    act(env, p1=(0, 0, 2), p2=(0, 0, 2))  # ...and grabs while P2 jabs
    hold(env, 10)
    assert f(env, 0)["percent"] > 0 and f(env, 1)["state"] != S["GRABBED"]


def test_tech_or_knockdown_on_a_hard_landing():
    for tech, expected in ((False, "KNOCKDOWN"), (True, "TECH")):
        env = new_env()
        face_off(env)
        env.set_percent(0, 1, 80.0)
        for _ in range(20):
            act(env, p1=(0, -1, 2))  # down-tilt: pops P2 up, tumbling
            if f(env, 1)["state"] == fightcore.ST_HITSTUN:
                break
        assert f(env, 1)["tumble"]
        pressed = False
        for t in range(200):
            # For the tech, P2 presses dodge once, just before landing (the
            # window is 11 frames; mashing earlier would hit the lockout).
            v = f(env, 1)
            falling = v["vy"] + v["launch_vy"] < 0
            b = (0, 0, 0)
            if tech and not pressed and falling and v["y"] < 1.0 and v["hitlag"] == 0:
                b, pressed = (0, 0, 4), True
            act(env, p2=b)
            if f(env, 1)["state"] in (S["KNOCKDOWN"], S["TECH"]):
                break
        assert f(env, 1)["state"] == S[expected]
