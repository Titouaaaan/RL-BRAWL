"""Counterattacks and whiff punishes: the events and the zero-sum bonus."""
from pathlib import Path

import numpy as np

import fightcore

MOVES = str(Path(__file__).parent.parent / "data" / "moves")
S = {name: i for i, name in enumerate(fightcore.STATE_NAMES)}


def new_env():
    return fightcore.VecEnv(1, MOVES, frame_skip=1, damage_reward=0.0, counter_reward=0.5)


def act(env, p1=(0, 0, 0), p2=(0, 0, 0)):
    env.step(np.array([[p1, p2]], dtype=np.int8))


def f(env, p):
    return env.fighter(0, p)


def face_off(env):
    for _ in range(200):
        a = (1, 0, 0) if f(env, 0)["x"] < -1.5 else (0, 0, 0)
        b = (1, 0, 0) if f(env, 1)["x"] > 1.5 else (0, 0, 0)
        act(env, p1=a, p2=b)


def stat(env, name):
    return dict(zip(fightcore.STAT_NAMES, env.stats))[name]


def jab_until_hit(env, frames=40):
    """P1 presses light until it lands; returns P1's reward on that frame."""
    for t in range(frames):
        act(env, p1=(0, 0, 2) if t % 2 == 0 else (0, 0, 0))
        if env.reward[0, 0] != 0 or f(env, 1)["percent"] > 0:
            return env.reward[0].copy()
    raise AssertionError("never hit")


def test_spot_dodge_then_hit_back_is_a_counter():
    env = new_env()
    face_off(env)
    act(env, p2=(0, 0, 3))  # P2 forward smash: hits on frame 15
    for _ in range(8):
        act(env)
    act(env, p1=(0, -1, 4))  # P1 spot dodges through it
    while f(env, 0)["state"] == S["SPOTDODGE"]:
        act(env)
    assert stat(env, "evades") == 1 and f(env, 1)["percent"] == 0 and f(env, 0)["percent"] == 0
    assert 0 < f(env, 0)["since_defense"] <= 60
    r = jab_until_hit(env)  # P2 is still stuck in the smash: hit back
    assert stat(env, "counters") == 1
    assert np.allclose(r, [0.5, -0.5])  # one bonus even though it's also a whiff punish, zero-sum
    assert f(env, 0)["since_defense"] == 65535  # used up


def test_dodging_alone_earns_nothing():
    env = new_env()
    face_off(env)
    act(env, p2=(0, 0, 3))
    for _ in range(8):
        act(env)
    total = 0.0
    for _ in range(80):  # dodge through the smash, then do nothing
        act(env, p1=(0, -1, 4))
        total += env.reward[0, 0]
    assert stat(env, "evades") == 1 and total == 0.0


def test_counter_window_expires():
    env = new_env()
    face_off(env)
    act(env, p1=(0, 0, 4), p2=(0, 0, 1))  # P1 shields...
    for _ in range(3):
        act(env, p1=(0, 0, 4))
    for t in range(30):
        act(env, p1=(0, 0, 4), p2=(0, 0, 2) if t % 2 == 0 else (0, 0, 0))  # ...P2's jab gets blocked
        if stat(env, "blocks") > 0:
            break
    assert stat(env, "blocks") == 1 and f(env, 0)["since_defense"] == 0
    for _ in range(80):
        act(env)  # wait past the window (P2 recovers too, so no whiff punish)
    r = jab_until_hit(env)
    assert stat(env, "counters") == 0 and np.allclose(r, [0, 0])


def test_hitting_a_whiffed_move_is_a_punish():
    env = new_env()
    face_off(env)
    act(env, p2=(0, 0, 3))  # P2 forward smashes...
    for _ in range(8):
        act(env, p1=(-1, 0, 0))  # ...while P1 backs out of range
    for _ in range(12):
        act(env)
    assert f(env, 0)["percent"] == 0  # it whiffed
    for _ in range(30):  # come back and hit P2 in the smash's recovery
        close = f(env, 1)["x"] - f(env, 0)["x"] < 3.0
        act(env, p1=(1, 0, 0) if not close else (0, 0, 2))
        if f(env, 1)["percent"] > 0:
            break
    assert stat(env, "whiff_punishes") == 1 and stat(env, "counters") == 0
    assert np.allclose(env.reward[0], [0.5, -0.5])


def test_counter_window_is_observed():
    env = fightcore.VecEnv(1, MOVES, frame_skip=1)
    obs_before = env.obs[0, 0].copy()
    face_off(env)
    act(env, p1=(0, 0, 4), p2=(0, 0, 1))
    for t in range(40):
        act(env, p1=(0, 0, 4), p2=(0, 0, 2) if t % 2 == 0 else (0, 0, 0))
        if f(env, 0)["since_defense"] == 0:
            break
    # The feature sits before the smash charge and the 6 blast-zone / edge distances at the end of the fighter block.
    idx = fightcore.OBS_FIGHTER - 8
    assert obs_before[idx] == 0.0 and env.obs[0, 0, idx] == 1.0
