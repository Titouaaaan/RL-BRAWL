"""Combo finder: which true combos exist in the game at all?

For each opening move and victim %, runs thousands of copies of the same
situation in parallel (one VecEnv game per follow-up plan) with frame-perfect
inputs, no reaction delay and a victim who doesn't DI, and reports whether any
follow-up lands while the victim is still in hitstun (a true combo). This
measures the game, not the agent: if nothing is found here, no agent can combo.

    python tools/combos.py
"""
import itertools
import sys
from pathlib import Path

import numpy as np

import fightcore

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "train"))
import bots  # noqa: E402  (obs indices)

COMBO = bots.STATE + len(fightcore.STATE_NAMES) + 10  # obs: my combo length / 5
S = {n: i for i, n in enumerate(fightcore.STATE_NAMES)}
LIGHT, HEAVY, JUMP, DODGE = 2, 3, 1, 4

# Follow-up plans, all starting on the frame the opener connects:
#   ("ground", wait, run, button, stick_y): wait, run toward the victim, then attack
#   ("air", wait, full_hop, air_frames, button, stick_y): wait, jump, drift toward, then attack
GROUND = [("ground", w, r, b, sy) for w, r, b, sy in
          itertools.product(range(0, 42, 2), range(0, 18, 3), (LIGHT, HEAVY), (-1, 0, 1))]
AIR = [("air", w, full, a, b, sy) for w, full, a, b, sy in
       itertools.product(range(0, 42, 2), (0, 1), range(1, 32, 3), (LIGHT, HEAVY), (-1, 0, 1))]
PLANS = GROUND + AIR

# Openers: (name, setup) where setup is a list of P1 inputs after the face-off.
GRAB = [(0, 0, DODGE)] + [(0, 0, LIGHT)] * 8  # shield, grab (the victim shields too, see below)
OPENERS = {
    "jab": [(0, 0, LIGHT)],
    "forward_tilt": [(1, 0, LIGHT)],
    "down_tilt": [(0, -1, LIGHT)],
    "up_tilt": [(0, 1, LIGHT)],
    "dash_attack": "dash",
    "down_throw": GRAB + [(0, 0, 0)] * 4 + [(0, -1, 0)],
    "up_throw": GRAB + [(0, 0, 0)] * 4 + [(0, 1, 0)],
    "forward_throw": GRAB + [(0, 0, 0)] * 4 + [(1, 0, 0)],
    "up_heavy": [(0, 1, HEAVY)],
    # Short hop (tap jump), then the aerial on the way up.
    "air": [(0, 0, JUMP)] + [(0, 0, 0)] * 4 + [(0, 0, LIGHT)],
    "up_air": [(0, 0, JUMP)] + [(0, 0, 0)] * 4 + [(0, 1, LIGHT)],
    "heavy_air": [(0, 0, JUMP)] + [(0, 0, 0)] * 4 + [(1, 0, HEAVY)],
    "down_air": [(0, 0, JUMP)] + [(0, 0, 0)] * 6 + [(0, -1, LIGHT)],
    "down_heavy": [(0, -1, HEAVY)],
}


def run(opener, percent):
    n = len(PLANS)
    env = fightcore.VecEnv(n, str(ROOT / "data" / "moves"), frame_skip=1)

    def step(a0, a1=None):
        acts = np.zeros((n, 2, 3), dtype=np.int8)
        acts[:, 0] = a0
        if a1 is not None:
            acts[:, 1] = a1
        env.step(acts)

    # Face off in the middle (identical in every copy).
    for _ in range(200):
        f0, f1 = env.fighter(0, 0), env.fighter(0, 1)
        step((1, 0, 0) if f0["x"] < -1.5 else (0, 0, 0), (1, 0, 0) if f1["x"] > 1.5 else (0, 0, 0))
    for i in range(n):
        env.set_percent(i, 1, float(percent))
    setup = OPENERS[opener]
    if setup == "dash":  # back off, then dash attack in
        for _ in range(12):
            step((-1, 0, 0))
        for _ in range(8):
            step((1, 0, 0))
        setup = [(1, 0, LIGHT)]
    victim_shield = (0, 0, DODGE) if "throw" in opener else None
    connected = False
    for t in range(90):  # the setup inputs, then let go (holding heavy would charge a smash)
        a = setup[t] if t < len(setup) else (0, 0, 0)
        step(a, victim_shield if t < len(setup) - 1 else None)
        if env.fighter(0, 1)["state"] == S["HITSTUN"]:
            connected = True
            break
    if not connected:
        return None

    # Follow-ups, one plan per game, vectorized over games.
    obs = env.obs
    best = np.zeros(n)
    downed = np.zeros(n, dtype=bool)  # hits on a knocked-down victim are tech chases, not true combos
    for t in range(80):
        dx = obs[:, 0, bots.DX]
        toward = np.where(dx >= 0, 1, -1).astype(np.int8)
        acts = np.zeros((n, 2, 3), dtype=np.int8)
        for i, p in enumerate(PLANS):
            if p[0] == "ground":
                _, w, r, b, sy = p
                if w <= t < w + r:
                    acts[i, 0] = (toward[i], 0, 0)
                elif t >= w + r:
                    acts[i, 0] = (0, sy, b)
            else:
                _, w, full, af, b, sy = p
                if t == w or (full and w <= t < w + 5):
                    acts[i, 0] = (toward[i], 0, JUMP)
                elif t > w and t < w + af:
                    acts[i, 0] = (toward[i], 0, 0)
                elif t >= w + af:
                    acts[i, 0] = (toward[i] if sy == 0 else 0, sy, b)
        downed |= obs[:, 0, bots.OPP + bots.STATE + S["KNOCKDOWN"]] > 0
        env.step(acts)
        best = np.where(downed, best, np.maximum(best, np.round(obs[:, 0, COMBO] * 5)))
    hits = np.nonzero(best >= 2)[0]
    return [PLANS[i] for i in hits]


def describe(p):
    btn = {LIGHT: "light", HEAVY: "heavy"}[p[-2]]
    stick = {-1: "down", 0: "", 1: "up"}[p[-1]]
    if p[0] == "ground":
        return f"wait {p[1]}, run {p[2]}, {stick} {btn}".replace("  ", " ")
    return f"wait {p[1]}, {'full hop' if p[2] else 'short hop'}, {p[3]} frames later {stick} {btn} (aerial)".replace("  ", " ")


if __name__ == "__main__":
    percents = (0, 20, 40, 60, 80, 100)
    print(f"{len(PLANS)} follow-up plans per case; entries = plans that true-combo / of those, "
          "starting 8+ frames after the hit (what a 6-frame-delayed agent can react to)")
    print("opener".ljust(15) + "".join(f"{p:>10}%" for p in percents))
    examples = {}
    for name in OPENERS:
        row = []
        for pct in percents:
            found = run(name, pct)
            late = None if found is None else sum(1 for p in found if p[1] >= 8)
            row.append("   miss   " if found is None else f"{len(found):5d}/{late:<4d}")
            if found:
                examples.setdefault(name, (pct, describe(found[0])))
        print(name.ljust(15) + " ".join(row))
    print("\nexamples:")
    for name, (pct, d) in examples.items():
        print(f"  {name} at {pct}%: {d}")
