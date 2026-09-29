"""The scripted bots are the yardstick for training, so check they're sane."""
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "train"))
import bots  # noqa: E402

import fightcore  # noqa: E402


def play(bot0, bot1, n=16, reaction_delay=0):
    """Plays n games to the end; returns winners (0, 1, or -1 draw)."""
    env = fightcore.VecEnv(n, str(ROOT / "data" / "moves"), reaction_delay=reaction_delay)
    winner = np.full(n, -2)
    while (winner == -2).any():
        env.step(np.stack([bot0(env.obs[:, 0]), bot1(env.obs[:, 1])], axis=1))
        done = (env.terminated | env.truncated).astype(bool) & (winner == -2)
        winner[done] = env.winner[done]
    return winner


def test_jabber_beats_the_dummy():
    assert (play(bots.jabber, bots.dummy) == 0).all()


def test_spacer_waits_for_whiffs():
    # The spacer only attacks to punish, so a dummy (never attacks) is a draw,
    # while a random player (whiffs constantly) gets beaten.
    assert (play(bots.spacer, bots.dummy, n=4) == -1).all()
    np.random.seed(0)  # the random bot uses NumPy's global generator
    assert (play(bots.spacer, bots.random, n=32) == 0).mean() >= 0.9


def test_fighter_is_the_strongest_bot():
    # With the 6-frame reaction delay training uses (the fighter reacts to
    # what it sees; the jabber just mashes, so it gains most from no delay).
    np.random.seed(1)
    for other, needed in ((bots.dummy, 0.8), (bots.random, 0.8), (bots.jabber, 0.6)):
        assert (play(bots.fighter, other, n=32, reaction_delay=6) == 0).mean() >= needed


def test_obs_indices_match_the_env():
    env = fightcore.VecEnv(1, str(ROOT / "data" / "moves"))
    o = env.obs[0, 0]
    assert abs(o[bots.DX] * bots.BLAST_X - 2 * fightcore.SPAWN_X) < 1e-4  # players start 2 x SPAWN_X apart
    assert o[bots.ME + bots.GROUNDED] == 1 and o[bots.ME + bots.STATE + 0] == 1  # grounded, IDLE
    assert abs(o[bots.ME + bots.EDGE_LEFT] * bots.STAGE_HALF - (bots.STAGE_HALF - fightcore.SPAWN_X)) < 1e-4
    # The named features after the state one-hot (see write_fighter in core/vecenv.cpp).
    assert bots.COUNTER == bots.F - 7 and bots.TUMBLE == bots.F - 10
