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


def test_same_egocentric_action_gives_identical_obs():
    # Observations AND actions are egocentric (player 1's world is mirrored).
    # The start is symmetric, so if both players send the same action, both
    # must see exactly the same observation, every step.
    # Many seeds and long games: rare situations (same-frame grabs, trades,
    # DI) have broken the symmetry before.
    for seed in range(12):
        env = fightcore.VecEnv(1, MOVES)
        assert np.array_equal(env.obs[0, 0], env.obs[0, 1])
        rng = np.random.default_rng(seed)
        for _ in range(1500):
            a = (rng.integers(-1, 2), rng.integers(-1, 2), rng.integers(0, 5))
            env.step(acts(1, p1=a, p2=a))
            assert np.array_equal(env.obs[0, 0], env.obs[0, 1])


def test_player1_stick_is_mirrored():
    env = fightcore.VecEnv(1, MOVES, frame_skip=1)
    x0 = env.fighter(0, 1)["x"]
    env.step(acts(1, p2=(1, 0, 0)))  # "forward" in P1's own view...
    assert env.fighter(0, 1)["x"] < x0  # ...is toward the left in the real game


def test_damage_reward_is_zero_sum():
    env = fightcore.VecEnv(1, MOVES, frame_skip=1, damage_reward=0.01)
    while env.fighter(0, 1)["x"] - env.fighter(0, 0)["x"] > 3.2:
        env.step(acts(1, p1=(1, 0, 0)))  # run into jab range
    for _ in range(12):
        env.step(acts(1))  # let the run slide stop
    total = np.zeros(2)
    for _ in range(10):
        env.step(acts(1, p1=(0, 0, 2)))  # hold jab
        total += env.reward[0]
    dealt = env.fighter(0, 1)["percent"]
    assert dealt > 0 and np.allclose(total, [0.01 * dealt, -0.01 * dealt])


def test_ko_reward_termination_winner_and_autoreset():
    env = fightcore.VecEnv(1, MOVES)
    fresh = env.obs.copy()
    kos = 0
    for _ in range(3000):
        env.step(acts(1, p2=(-1, 0, 0)))  # P2 runs "backward" (its mirrored left = real right), off the far edge
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
    for _ in range(7 * 60 * 60 // 4):  # 7 minutes at frame_skip 4
        env.step(acts(1))
    assert env.truncated[0] and not env.terminated[0]
    assert env.winner[0] == -1  # nobody did anything: draw


def test_reaction_delay_shows_the_past():
    rng = np.random.default_rng(1)
    seq = rng.integers(-1, 2, size=(50, 1, 2, 3)).astype(np.int8)
    seq[..., 2] = rng.integers(0, 5, size=(50, 1, 2))
    now = fightcore.VecEnv(1, MOVES, frame_skip=1)
    late = fightcore.VecEnv(1, MOVES, frame_skip=1, reaction_delay=3)
    history = [now.obs.copy()]
    for a in seq:
        now.step(a)
        late.step(a)
        history.append(now.obs.copy())
        t = len(history) - 1
        assert np.array_equal(late.obs, history[max(t - 3, 0)])


def test_move_stats_count_player0_moves():
    env = fightcore.VecEnv(1, MOVES, frame_skip=1)
    env.step(acts(1, p1=(0, 1, 2), p2=(0, 0, 2)))  # P1 up-tilt, P2 jab (not counted)
    assert env.move_uses[fightcore.MOVE_NAMES.index("up_tilt")] == 1
    assert env.move_uses.sum() == 1


def test_timeout_tie_breaker_rewards_the_player_ahead():
    env = fightcore.VecEnv(1, MOVES, frame_skip=1, timeout_reward=0.3, damage_reward=0.0)
    while env.fighter(0, 1)["x"] - env.fighter(0, 0)["x"] > 3.2:
        env.step(acts(1, p1=(1, 0, 0)))  # run into jab range
    for _ in range(12):
        env.step(acts(1))
    for _ in range(10):
        env.step(acts(1, p1=(0, 0, 2)))  # land a jab: P1 is ahead on damage
    while not env.truncated[0]:
        env.step(acts(1))  # then both wait for the time limit
    assert env.winner[0] == 0
    assert np.allclose(env.reward[0], [0.3, -0.3])


def test_only_true_combos_count_and_get_the_bonus():
    # Forward tilt, then again after P2 has recovered: not a combo. The jab
    # chain (jab 1 -> 2 -> 3) hits P2 while still in hitstun: a true combo, and
    # the damage reward of each follow-up hit is boosted (combo_growth=2), zero-sum.
    env = fightcore.VecEnv(1, MOVES, frame_skip=1, damage_reward=0.01, combo_growth=2.0, combo_cap=4.0)
    while env.fighter(0, 1)["x"] - env.fighter(0, 0)["x"] > 3.2:
        env.step(acts(1, p1=(1, 0, 0)))
    for _ in range(20):
        env.step(acts(1))
    combos = []
    for t in range(80):  # tap forward tilt: P2 recovers between them (it's slow)
        env.step(acts(1, p1=(1, 0, 2) if t % 2 == 0 else (1, 0, 0)))
        if env.reward[0, 0] > 0:
            combos.append(env.fighter(0, 0)["combo"])
    assert combos and all(c == 1 for c in combos)

    env = fightcore.VecEnv(1, MOVES, frame_skip=1, damage_reward=0.01, combo_growth=2.0, combo_cap=4.0)
    while env.fighter(0, 1)["x"] - env.fighter(0, 0)["x"] > 3.2:
        env.step(acts(1, p1=(1, 0, 0)))
    for _ in range(20):
        env.step(acts(1))
    hits = []
    for _ in range(60):  # hold light: jab 1, 2, 3
        env.step(acts(1, p1=(0, 0, 2)))
        if env.reward[0, 0] > 0:
            hits.append((env.fighter(0, 0)["combo"], env.reward[0, 0], env.reward[0, 1]))
    assert [c for c, _, _ in hits[:3]] == [1, 2, 3]
    assert all(r0 == -r1 for _, r0, r1 in hits)


def test_stats_count_the_learners_actions():
    env = fightcore.VecEnv(1, MOVES, frame_skip=1)
    names = fightcore.STAT_NAMES
    env.step(acts(1, p1=(0, 0, 1)))
    for _ in range(10):
        env.step(acts(1, p1=(0, 0, 1)))  # a full jump
    st = dict(zip(names, env.stats))
    assert st["frames"] == 11 and st["full_jumps"] == 1 and st["short_hops"] == 0
    assert st["frames_airborne"] > 0
