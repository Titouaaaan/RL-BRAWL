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
X, VX, VY, GROUNDED, JUMPS = 0, 2, 3, 8, 11
STATE = 13  # the state one-hot starts here (order: FighterState in core/sim.h)
ST_ATTACK, ST_LANDING, ST_HITSTUN = 5, 4, 6
EDGE_RIGHT, EDGE_LEFT = F - 2, F - 1  # distance to the stage edges, / stage_half_width
DX, DY = 2 * F, 2 * F + 1  # opponent x - my x, / blast_x; opponent y - my y, / blast_top
BLAST_X, BLAST_TOP, STAGE_HALF = fightcore.BLAST_X, fightcore.BLAST_TOP, fightcore.STAGE_HALF_WIDTH  # to turn scaled obs back into units
NONE, JUMP, LIGHT, HEAVY, DODGE = 0, 1, 2, 3, 4
Y, LAUNCH_VY, PERCENT = 1, 5, 6
ST = {name: i for i, name in enumerate(fightcore.STATE_NAMES)}
# Features after the state one-hot (order: write_fighter in core/vecenv.cpp).
_AFTER = STATE + len(fightcore.STATE_NAMES)
STATE_FRAME, MOVE_LEFT, LAG_LEFT = _AFTER, _AFTER + 2, _AFTER + 3  # all in frames / 60
TUMBLE, COUNTER = _AFTER + 12, _AFTER + 15  # COUNTER: counter window left (1 = just defended)
FLOORS = (0.0, 4.8, 9.4)  # heights of the stage and platforms (GameConfig::surfaces)


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
    # Jump needs a fresh press. Pressing on every falling frame would hold it
    # down from the hitstun on (when jumping is impossible) and never release,
    # so press on about half the frames: each press after a gap is fresh.
    a[off_stage & falling & (np.random.random(n) < 0.5), 2] = JUMP
    # Don't walk off the stage chasing someone: stop within 1.5 units of the edge.
    edge_ahead = np.where(toward > 0, obs[:, ME + EDGE_RIGHT], obs[:, ME + EDGE_LEFT]) * STAGE_HALF
    return a, np.abs(d), toward, off_stage, edge_ahead < 1.5


def _no_charge(obs, a):
    """Let go of heavy once an attack is out: holding it would charge a smash."""
    attacking = obs[:, ME + STATE + ST_ATTACK] > 0
    a[attacking & (a[:, 2] == HEAVY), 2] = NONE
    return a


def jabber(obs):
    """Walks toward the opponent and jabs when close. If they stand on a
    platform above, jumps up to them and uses light (an aerial) there."""
    a, dist, toward, busy, at_edge = _base(obs)
    free = ~busy
    close = dist < 3.2
    above = obs[:, DY] * BLAST_TOP > 2.0
    grounded = obs[:, ME + GROUNDED] > 0
    a[free, 0] = np.where(close[free] | ~at_edge[free], toward[free], 0)
    a[free & close, 2] = LIGHT
    climb = free & close & above
    a[climb, 2] = np.where(grounded[climb], JUMP, LIGHT)
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
    return _no_charge(obs, a)


def fighter(obs):
    """Plays the whole toolkit, as a tougher yardstick than jabber/spacer.

    Defends attacks that start close by (shield, spot dodge or roll away), then
    counterattacks (grab out of shield, tilt, or a smash at high %); punishes
    whiffed moves, landing lag and rebounds; spaces with forward tilt and mixes
    in jabs, down-tilts, grabs and short hops; juggles with up-tilt / up-air;
    throws toward the nearer edge; techs, mixes get-up and ledge options, mashes
    out of grabs, and edge-guards. Mixups use NumPy's global generator (seed it
    for repeatable games)."""
    a, dist, toward, busy, at_edge = _base(obs)
    n = len(obs)
    die = np.random.random(n)  # one roll per game and frame, for the mixups
    side = np.random.randint(-1, 2, size=n)

    def me(i):
        return obs[:, ME + i]

    def opp(i):
        return obs[:, OPP + i]

    def my_state(name):
        return obs[:, ME + STATE + ST[name]] > 0

    def opp_state(name):
        return obs[:, OPP + STATE + ST[name]] > 0

    def do(mask, sx, sy, button):
        """Sets the action for the rows in `mask`; sx and button may be per-row arrays."""
        a[mask, 0] = sx[mask] if isinstance(sx, np.ndarray) else sx
        a[mask, 1] = sy
        a[mask, 2] = button[mask] if isinstance(button, np.ndarray) else button

    free = ~busy
    grounded = me(GROUNDED) > 0
    dy = obs[:, DY] * BLAST_TOP
    level = np.abs(dy) < 1.5
    opp_pct = opp(PERCENT) * 100
    close = dist < 3.2

    offstage_opp = (opp(EDGE_LEFT) < 0) | (opp(EDGE_RIGHT) < 0)
    edge_ahead = np.where(toward > 0, me(EDGE_RIGHT), me(EDGE_LEFT))   # / stage_half_width
    edge_behind = np.where(toward > 0, me(EDGE_LEFT), me(EDGE_RIGHT))
    # Wide margins: in training the bot sees the game 6 frames late (~2 units at run speed).
    at_edge = edge_ahead * STAGE_HALF < 3.0 + 8.0 * np.abs(me(VX))  # a running fighter needs ~6 units
    safe = np.minimum(me(EDGE_LEFT), me(EDGE_RIGHT)) * STAGE_HALF > 3.0  # well inside the stage
    # Neutral: run in (never off the edge), letting go a bit early so the
    # dash slides into range instead of overshooting; then a mixup.
    do(free & grounded & (dist >= 3.2) & ~at_edge, toward, 0, NONE)
    fast = np.abs(me(VX)) > 0.2
    do(free & grounded & fast & (dist >= 3.2) & (dist < 4.5), 0, 0, NONE)  # brake: slide in
    do(free & ~grounded & (dist > 0.8) & ~at_edge, toward, 0, NONE)  # drift toward them in the air
    poke = free & close & level & grounded
    tip = dist >= 2.6  # at the tip of the range only the forward tilt reaches; up close, the jab is faster
    do(poke & tip & (die < 0.55), toward, 0, LIGHT)              # forward tilt
    do(poke & ~tip & (die < 0.45), 0, 0, LIGHT)                  # jab (frame 2)
    do(poke & ~tip & (die >= 0.45) & (die < 0.55), toward, 0, LIGHT)  # forward tilt
    do(poke & (die >= 0.55) & (die < 0.7), 0, -1, LIGHT)         # down tilt
    do(poke & (die >= 0.7) & (die < 0.8), 0, 0, DODGE)           # shield, then (below) grab
    do(poke & (die >= 0.8) & (die < 0.9), toward, 0, JUMP)       # short hop (a one-frame press)
    air = free & ~grounded & safe & (dist < 3.5) & (np.abs(dy) < 3.0)
    do(air & (dy > 1.0), 0, 1, LIGHT)                            # up air
    do(air & (dy <= 1.0) & (die < 0.5), toward, 0, HEAVY)        # heavy air
    do(air & (dy <= 1.0) & (die >= 0.5), 0, 0, LIGHT)            # neutral air
    # Opponent on a platform or in the air above: jump up to them, juggle.
    above = free & grounded & (dist < 1.5) & (dy > 1.5) & ~offstage_opp
    do(above & opp_state("HITSTUN") & (dy < 3.5), 0, 1, LIGHT)   # up tilt
    do(above & ~(opp_state("HITSTUN") & (dy < 3.5)), 0, 0, JUMP)
    # Edge-guard: opponent off the stage right next to my edge.
    do(free & grounded & at_edge & offstage_opp & (dist < 3.5), toward, 0, HEAVY)

    # Punish: the opponent is stuck, or I just defended (counter window open).
    opp_frame = opp(STATE_FRAME) * 60
    stuck = ((opp_state("ATTACK") & (opp_frame > 12) & (opp(MOVE_LEFT) * 60 > 8))
             | (opp_state("LANDING") & (opp(LAG_LEFT) * 60 > 4))
             | opp_state("REBOUND") | opp_state("SHIELDBREAK"))
    punish = free & (stuck | (me(COUNTER) > 0)) & ~(opp_state("ATTACK") & (opp_frame <= 12))
    shielding = my_state("SHIELD")
    do(punish & shielding & (dist < 2.5), 0, 0, LIGHT)           # grab out of shield
    do(punish & shielding & (dist >= 2.5), 0, 0, NONE)           # drop the shield
    hit_now = punish & ~shielding & grounded & close
    do(hit_now & (opp_pct > 85), toward, 0, HEAVY)               # forward smash
    do(hit_now & (opp_pct <= 85), toward, 0, LIGHT)              # forward tilt
    # Out of a clank both fighters recover on the same frame. Any fixed answer
    # can be mirrored and clank again forever, so mix: jab (fastest), shield
    # (then grab), or spot dodge.
    clanked = hit_now & opp_state("REBOUND") & (dist < 2.8)
    do(clanked & (die < 0.4), 0, 0, LIGHT)
    do(clanked & (die >= 0.4) & (die < 0.75), 0, 0, DODGE)
    do(clanked & (die >= 0.75), 0, -1, DODGE)
    room = edge_ahead * STAGE_HALF > 5.0  # a dash attack slides a few units: not off the edge
    do(punish & stuck & ~shielding & grounded & ~close & (dist < 7) & room, toward, 0, LIGHT)  # dash attack

    # Defend: an attack starting close by (reacting to its startup).
    threat = opp_state("ATTACK") & (opp_frame <= 10) & (dist < 4.5) & free & grounded
    do(threat & (die < 0.55), 0, 0, DODGE)                       # shield
    do(threat & (die >= 0.55) & (die < 0.8), 0, -1, DODGE)       # spot dodge
    do(threat & (die >= 0.8), -toward, 0, DODGE)                 # roll away
    keep_shield = shielding & opp_state("ATTACK") & ~stuck & free
    do(keep_shield, 0, 0, DODGE)
    grab_now = shielding & ~opp_state("ATTACK") & (dist < 2.5) & free
    do(grab_now, 0, 0, LIGHT)                                    # shield then grab

    # Holding: down throw at low % (sets up combos), else throw toward the nearer edge.
    holding = my_state("HOLDING")
    do(holding & (opp_pct < 60), 0, -1, NONE)
    to_edge = np.where(edge_ahead < edge_behind, toward, -toward)
    do(holding & (opp_pct >= 60), to_edge, 0, NONE)
    do(my_state("GRABBED"), 0, 0, np.where(die < 0.5, LIGHT, NONE))  # mash

    # Tech just before landing while tumbling; get-up mixups; ledge mixups.
    height = np.min([np.where(me(Y) * BLAST_TOP >= f - 0.1, me(Y) * BLAST_TOP - f, 99) for f in FLOORS], axis=0)
    falling = me(VY) + me(LAUNCH_VY) < 0
    over_stage = np.minimum(me(EDGE_LEFT), me(EDGE_RIGHT)) > 0  # off the stage a "tech" press would air dodge
    do((me(TUMBLE) > 0) & falling & (height < 1.5) & over_stage, side, 0, DODGE)
    down = my_state("KNOCKDOWN")
    do(down & (die < 0.4), 0, 1, NONE)                           # get up
    do(down & (die >= 0.4) & (die < 0.7), 0, 0, LIGHT)           # get-up attack
    do(down & (die >= 0.7), np.where(side == 0, 1, side), 0, DODGE)  # get-up roll
    # Choose a ledge option once, at an observed hang time of 10-13 frames: the
    # observation lags 6 frames, so pressing on every frame the bot still "sees"
    # itself hanging would turn a ledge roll or attack into an air dodge or aerial.
    hang = me(STATE_FRAME) * 60
    ledge = my_state("LEDGE") & (hang >= 10) & (hang < 14)
    do(my_state("LEDGE") & ~ledge, 0, 0, NONE)
    inward = np.where(me(X) > 0, -1, 1)
    do(ledge & (die < 0.4), inward, 0, NONE)                     # climb
    do(ledge & (die >= 0.4) & (die < 0.6), 0, 0, JUMP)           # ledge jump
    do(ledge & (die >= 0.6) & (die < 0.8), 0, 0, DODGE)          # ledge roll
    do(ledge & (die >= 0.8), 0, 0, LIGHT)                        # ledge attack
    return _no_charge(obs, a.astype(np.int8))


BOTS = {"dummy": dummy, "random": random, "jabber": jabber, "spacer": spacer, "fighter": fighter}
