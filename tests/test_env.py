"""The RL interface: observations, rewards, episode ends, frame skip, delay."""
from pathlib import Path

import numpy as np

import fightcore

MOVES = str(Path(__file__).parent.parent / "data" / "moves")


def acts(n, p1=(0, 0, 0), p2=(0, 0, 0)):
    return np.array([[p1, p2]] * n, dtype=np.int8)


def test_obs_buffers_are_live_views():
    env = fightcore.VecEnv(8, MOVES)
    obs = env.obs  # grab once...
    assert obs.shape == (8, 2, fightcore.OBS_DIM) and obs.dtype == np.float32
    before = obs.copy()
    env.step(acts(8, p1=(1, 0, 0)))
    assert not np.array_equal(obs, before)  # ...and it updates in place
    assert np.isfinite(obs).all()


def test_mirrored_actions_give_identical_egocentric_obs():
    # The start is symmetric, and player 1's view is mirrored. So if both
    # players do mirror-image things, both must see exactly the same obs.
    env = fightcore.VecEnv(1, MOVES)
    assert np.array_equal(env.obs[0, 0], env.obs[0, 1])
    rng = np.random.default_rng(0)
    for _ in range(300):
        sx, sy, b = rng.integers(-1, 2), rng.integers(-1, 2), rng.integers(0, 4)
        env.step(acts(1, p1=(sx, sy, b), p2=(-sx, sy, b)))
        assert np.array_equal(env.obs[0, 0], env.obs[0, 1])


def test_damage_reward_is_zero_sum():
    env = fightcore.VecEnv(1, MOVES, frame_skip=1, damage_reward=0.01)
    for _ in range(66):
        env.step(acts(1, p1=(1, 0, 0)))  # walk into jab range
    total = np.zeros(2)
    for _ in range(10):
        env.step(acts(1, p1=(0, 0, 2)))  # hold jab
        total += env.reward[0]
    assert np.allclose(total, [0.03, -0.03])  # 3% dealt / taken


def test_ko_reward_termination_winner_and_autoreset():
    env = fightcore.VecEnv(1, MOVES)
    fresh = env.obs.copy()
    kos = 0
    for _ in range(3000):
        env.step(acts(1, p2=(-1, 0, 0)))  # P2 keeps walking off the left edge
        r = env.reward[0]
        if r[1] <= -1:
            kos += 1
            assert r[0] >= 1  # P1 gets the KO reward
        if env.terminated[0]:
            break
    assert kos == 3
    assert env.terminated[0] and not env.truncated[0]
    assert env.winner[0] == 0
    assert np.array_equal(env.obs, fresh)  # already reset to a new game
    assert not np.array_equal(env.final_obs[0], fresh[0])  # last obs of the old game


def test_time_limit_is_truncation_and_draw():
    env = fightcore.VecEnv(1, MOVES)
    for _ in range(3 * 60 * 60 // 4):  # 3 minutes at frame_skip 4
        env.step(acts(1))
    assert env.truncated[0] and not env.terminated[0]
    assert env.winner[0] == -1  # nobody did anything: draw


def test_reaction_delay_shows_the_past():
    rng = np.random.default_rng(1)
    seq = rng.integers(-1, 2, size=(50, 1, 2, 3)).astype(np.int8)
    seq[..., 2] = rng.integers(0, 4, size=(50, 1, 2))
    now = fightcore.VecEnv(1, MOVES, frame_skip=1)
    late = fightcore.VecEnv(1, MOVES, frame_skip=1, reaction_delay=3)
    history = [now.obs.copy()]
    for a in seq:
        now.step(a)
        late.step(a)
        history.append(now.obs.copy())
        t = len(history) - 1
        assert np.array_equal(late.obs, history[max(t - 3, 0)])
