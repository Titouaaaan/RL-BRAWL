"""Input buffer, smash attack charge, pummel."""
import pytest

import fightcore
from test_combat import S, act, face_off, f, hold, move_id, new_env, to_center


def test_a_press_just_before_you_can_act_is_buffered():
    for early, buffered in ((5, True), (15, False)):
        env = new_env()
        to_center(env)
        act(env, p1=(0, 1, 2))  # up-tilt: 29 frames
        total = 29
        hold(env, total - 1 - early)
        act(env, p1=(0, 0, 1))  # tap jump `early` frames before the up-tilt ends...
        jumped = False
        for _ in range(early + 3):
            act(env)  # ...and let go
            jumped |= f(env, 0)["state"] == S["JUMPSQUAT"]
        assert jumped == buffered


def test_holding_heavy_charges_a_smash():
    hits = []
    for charge_frames in (0, 80):
        env = new_env()
        face_off(env)
        act(env, p1=(0, 0, 3))  # forward smash, heavy held for a while
        for _ in range(charge_frames):
            act(env, p1=(0, 0, 3))
        charged = f(env, 0)["charge"]
        for _ in range(30):
            act(env)
            if f(env, 1)["percent"] > 0:
                break
        hits.append((charged, f(env, 1)["percent"]))
    (c0, d0), (c1, d1) = hits
    assert c0 == 0 and c1 == 60  # capped at charge_max
    assert d1 == pytest.approx(d0 * 1.4, rel=1e-4)


def test_pummel_hurts_without_letting_go():
    env = new_env()
    face_off(env, p2=(0, 0, 4))
    act(env, p1=(0, 0, 4), p2=(0, 0, 4))
    for _ in range(10):
        act(env, p1=(0, 0, 2), p2=(0, 0, 4))  # grab out of shield
        if f(env, 1)["state"] == S["GRABBED"]:
            break
    hold(env, 5)
    act(env, p1=(0, 0, 2))  # pummel
    act(env)
    assert f(env, 1)["percent"] == pytest.approx(1.3) and f(env, 1)["state"] == S["GRABBED"]
