"""Throughput benchmark for VecEnv.step, including observations and rewards.

Run from the repo root: python tools/bench.py
"""
import time

import numpy as np

import fightcore

N, STEPS, FRAME_SKIP = 4096, 300, 4
env = fightcore.VecEnv(N, "data/moves", frame_skip=FRAME_SKIP)
rng = np.random.default_rng(0)
actions = rng.integers(-1, 2, size=(STEPS, N, 2, 3)).astype(np.int8)  # sticks
actions[..., 2] = rng.integers(0, 4, size=(STEPS, N, 2))  # buttons

t = time.perf_counter()
for a in actions:
    env.step(a)
dt = time.perf_counter() - t
decisions = N * STEPS
print(f"{decisions / dt:,.0f} decisions/s = {decisions * FRAME_SKIP / dt:,.0f} sim frames/s "
      f"({N} envs x {STEPS} steps x frame_skip {FRAME_SKIP} in {dt:.2f}s, single thread)")
