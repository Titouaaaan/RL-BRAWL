from pathlib import Path

import numpy as np

import fightcore

MOVES = str(Path(__file__).parent.parent / "data" / "moves")


def random_actions(steps, num_envs, seed=0):
    rng = np.random.default_rng(seed)
    sticks = rng.integers(-1, 2, size=(steps, num_envs, 2, 2))
    buttons = rng.integers(0, 5, size=(steps, num_envs, 2, 1))
    return np.concatenate([sticks, buttons], axis=-1).astype(np.int8)


def run(actions):
    env = fightcore.VecEnv(actions.shape[1], MOVES)
    start = np.zeros(env.num_envs, dtype=np.uint64)
    end = np.zeros(env.num_envs, dtype=np.uint64)
    env.hashes(start)
    for a in actions:
        env.step(a)
    env.hashes(end)
    return start, end


def test_same_inputs_same_state():
    actions = random_actions(2000, 64)
    start, a = run(actions)
    _, b = run(actions)
    assert (a == b).all()
    assert (a != start).all()  # the sim actually changed state
    assert len(set(a.tolist())) == 64  # different inputs gave different games


def test_bad_shape_rejected():
    env = fightcore.VecEnv(4, MOVES)
    try:
        env.step(np.zeros((3, 2, 3), dtype=np.int8))
    except ValueError:
        return
    raise AssertionError("wrong num_envs should raise")
