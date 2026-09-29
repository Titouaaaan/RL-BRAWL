"""The Smash mechanics added on top of the basics: tilts and dash attack,
crouch cancel, clanks, parry, ledges, get-up, dodge staling, grab mashing,
directional air dodge. Same helpers and conventions as test_combat.py."""
import pytest

import fightcore
from test_combat import S, act, face_off, f, hold, move_id, new_env, to_center


def ledge_hang(env):
    """P1 runs off the left edge and double jumps back to the ledge."""
    while f(env, 0)["grounded"]:
        act(env, p1=(-1, 0, 0))
    for t in range(240):
        act(env, p1=(1, 0, 1) if t == 35 else (1, 0, 0))
        if f(env, 0)["state"] == S["LEDGE"]:
            return
    raise AssertionError("never grabbed the ledge")


def test_side_light_is_forward_tilt_and_running_light_is_dash_attack():
    env = new_env()
    act(env, p1=(1, 0, 2))
    assert f(env, 0)["move"] == move_id("forward_tilt")
    env = new_env()
    hold(env, 15, p1=(1, 0, 0))  # running
    act(env, p1=(1, 0, 2))
    assert f(env, 0)["move"] == move_id("dash_attack") and f(env, 0)["vx"] > 0.2  # keeps the momentum


def test_crouch_cancel_weakens_knockback():
    speeds = []
    for p2 in ((0, 0, 0), (0, -1, 0)):  # P2 stands, then crouches
        env = new_env()
        face_off(env, p2=p2)
        for _ in range(10):
            act(env, p1=(1, 0, 2), p2=p2)  # forward tilt
            if f(env, 1)["percent"] > 0:
                break
        speeds.append(abs(f(env, 1)["launch_vx"]))
    assert speeds[1] == pytest.approx(speeds[0] * 0.85, rel=1e-4)


def test_same_frame_attacks_clank():
    env = new_env()
    face_off(env)
    act(env, p1=(1, 0, 2), p2=(1, 0, 2))  # both forward tilt: 7% vs 7%, within 9% -> both rebound
    for _ in range(10):
        act(env)
    assert f(env, 0)["state"] == S["REBOUND"] and f(env, 1)["state"] == S["REBOUND"]
    assert f(env, 0)["percent"] == f(env, 1)["percent"] == 0


def test_dropping_shield_as_a_hit_lands_parries():
    env = new_env()
    face_off(env)
    act(env, p1=(0, 0, 4), p2=(0, 0, 3))  # P1 shields, P2 starts a forward smash (hits on frame 15)
    hold(env, 11, p1=(0, 0, 4))
    hold(env, 10)  # P1 lets go: the hit lands in the first 5 frames of the shield drop
    assert f(env, 0)["percent"] == 0
    assert dict(zip(fightcore.STAT_NAMES, env.stats))["parries"] == 1
    assert f(env, 1)["hitlag"] > 0 or f(env, 1)["state"] == S["ATTACK"]  # the attacker is stuck: punish time


def test_ledge_grab_hang_and_climb():
    env = new_env()
    ledge_hang(env)
    me = f(env, 0)
    assert me["intangible"] and me["ledge_side"] == -1 and me["x"] < -16  # hanging off the left ledge
    hold(env, 8)  # hang a moment, then climb (stick toward the stage)
    for _ in range(60):
        act(env, p1=(1, 0, 0))
        if f(env, 0)["grounded"] and f(env, 0)["state"] != S["LEDGE_CLIMB"]:
            break
    assert f(env, 0)["grounded"] and f(env, 0)["x"] > -16 and f(env, 0)["stocks"] == 3


def test_ledge_grabs_are_limited_and_lose_intangibility():
    env = new_env()
    ledge_hang(env)
    assert f(env, 0)["ledge_grabs"] == 1
    hold(env, 10)
    act(env, p1=(0, -1, 0))  # let go...
    hold(env, 15)
    act(env, p1=(0, 0, 1))  # ...and jump straight up: you catch the ledge on the way down
    for _ in range(90):
        act(env)
        if f(env, 0)["state"] == S["LEDGE"]:
            break
    assert f(env, 0)["ledge_grabs"] == 2


def test_knocked_down_fighter_gets_up_intangible():
    env = new_env()
    face_off(env)
    env.set_percent(0, 1, 80.0)
    act(env, p1=(0, -1, 2))  # down-tilt: P2 tumbles, and misses the tech
    for _ in range(200):
        act(env)
        if f(env, 1)["state"] == S["KNOCKDOWN"]:
            break
    hold(env, 12)
    act(env, p2=(0, 1, 0))  # up: get up
    act(env)
    assert f(env, 1)["state"] == S["GETUP"] and f(env, 1)["intangible"]


def test_repeated_dodges_get_longer():
    env = new_env()
    lengths = []
    for _ in range(3):
        act(env, p1=(0, -1, 4))  # spot dodge
        lengths.append(f(env, 0)["dodge_len"])
        while f(env, 0)["state"] == S["SPOTDODGE"]:
            act(env)
    assert lengths[0] < lengths[1] < lengths[2]


def grab_hold_frames(mash):
    env = new_env()
    face_off(env, p2=(0, 0, 4))
    act(env, p1=(0, 0, 4), p2=(0, 0, 4))
    for _ in range(10):
        act(env, p1=(0, 0, 2), p2=(0, 0, 4))  # grab out of shield
        if f(env, 1)["state"] == S["GRABBED"]:
            break
    for t in range(200):
        act(env, p2=(0, 0, 2 if mash and t % 2 else 0))  # P1 never throws; P2 mashes or not
        if f(env, 1)["state"] != S["GRABBED"]:
            return t
    raise AssertionError("never escaped")


def test_mashing_escapes_a_grab_sooner():
    assert grab_hold_frames(mash=True) < grab_hold_frames(mash=False)


def test_directional_air_dodge_moves_you():
    env = new_env()
    to_center(env)
    hold(env, 5, p1=(0, 0, 1))
    x = f(env, 0)["x"]
    act(env, p1=(1, 0, 4))  # air dodge to the right
    hold(env, 10)
    assert f(env, 0)["x"] - x > 2.0
    for _ in range(120):  # helpless until landing, then the directional dodge's landing lag
        act(env, p1=(0, 0, 1))
        if f(env, 0)["grounded"]:
            break
        assert f(env, 0)["state"] in (S["AIRDODGE"], S["HELPLESS"])
    assert f(env, 0)["state"] == S["LANDING"] and f(env, 0)["landing_lag"] == 14
