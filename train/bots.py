"""Fixed opponents for the sanity ladder and for evaluation.

Each bot is a function: obs (n, OBS_DIM) float32 -> actions (n, 3) int8, with
egocentric actions (stick_x, stick_y, button), exactly what VecEnv expects.
Because observations are egocentric too, "toward the opponent" is simply
sign(dx) for both players; the env un-mirrors player 1 by itself.
"""
import numpy as np

import fightcore

# Observation indices, from write_fighter / write_obs in core/vecenv.cpp.
# Offsets after the state one-hot come from the module's constants, so adding
# a state doesn't silently shift them (tests/test_bots.py checks the rest).
F = fightcore.OBS_FIGHTER
ME, OPP = 0, F  # start of my / the opponent's fighter block
X, VY, GROUNDED, JUMPS = 0, 3, 8, 10
STATE = 12  # the state one-hot starts here (order: FighterState in core/sim.h)
ST_ATTACK, ST_LANDING, ST_HITSTUN = 5, 4, 6
EDGE_RIGHT, EDGE_LEFT = F - 2, F - 1  # distance to the stage edges, / stage_half_width
DX = 2 * F  # opponent x - my x, / blast_x
BLAST_X, STAGE_HALF = 55.0, 30.0  # GameConfig values, to turn scaled obs back into units
NONE, JUMP, LIGHT, HEAVY, DODGE = 0, 1, 2, 3, 4


def dummy(obs):
    return np.zeros((len(obs), 3), dtype=np.int8)


def random(obs):
    n = len(obs)
    a = np.random.randint(-1, 2, size=(n, 3))
    a[:, 2] = np.random.randint(0, 5, size=n)
    return a.astype(np.int8)


def _base(obs):
    """Shared helpers: distance and direction to the opponent, plus recovery.

    Returns (actions, dist, toward, busy). `busy` marks rows already decided by
    the recovery rule (off the stage: drift back and jump when falling)."""
    n = len(obs)
    a = np.zeros((n, 3), dtype=np.int8)
    d = obs[:, DX] * BLAST_X
    toward = np.where(d >= 0, 1, -1)
    off_stage = (obs[:, ME + EDGE_RIGHT] < 0) | (obs[:, ME + EDGE_LEFT] < 0)
    center = np.where(obs[:, ME + X] > 0, -1, 1)
    falling = obs[:, ME + VY] < 0
    a[off_stage, 0] = center[off_stage]
    # Jump needs a fresh press, so press only while falling: the frames in
    # between (rising, button released) re-arm it for the double jump.
    a[off_stage & falling, 2] = JUMP
    # Don't walk off the stage chasing someone: stop within 1.5 units of the edge.
    edge_ahead = np.where(toward > 0, obs[:, ME + EDGE_RIGHT], obs[:, ME + EDGE_LEFT]) * STAGE_HALF
    return a, np.abs(d), toward, off_stage, edge_ahead < 1.5


def jabber(obs):
    """Walks toward the opponent and jabs when close."""
    a, dist, toward, busy, at_edge = _base(obs)
    free = ~busy
    close = dist < 3.2
    a[free, 0] = np.where(close[free] | ~at_edge[free], toward[free], 0)
    a[free & close, 2] = LIGHT
    return a


def spacer(obs):
    """Keeps a mid distance and punishes with the heavy when the opponent
    whiffs (stuck in an attack or its landing lag) within heavy range."""
    a, dist, toward, busy, at_edge = _base(obs)
    free = ~busy
    opp_stuck = (obs[:, OPP + STATE + ST_ATTACK] > 0) | (obs[:, OPP + STATE + ST_LANDING] > 0)
    grounded = obs[:, ME + GROUNDED] > 0
    punish = free & grounded & opp_stuck & (dist < 4.3)
    too_close = free & ~punish & (dist < 4.5)
    too_far = free & ~punish & (dist > 7.0)
    a[punish, 0] = toward[punish]
    a[punish, 2] = HEAVY
    # Back off, unless the edge is behind us: then jab our way out instead.
    edge_behind = np.where(toward > 0, obs[:, ME + EDGE_LEFT], obs[:, ME + EDGE_RIGHT]) * STAGE_HALF < 2.0
    back = too_close & ~edge_behind
    cornered = too_close & edge_behind
    a[back, 0] = -toward[back]
    a[cornered, 0] = toward[cornered]
    a[cornered, 2] = LIGHT
    a[too_far & ~at_edge, 0] = toward[too_far & ~at_edge]
    return a


BOTS = {"dummy": dummy, "random": random, "jabber": jabber, "spacer": spacer}
