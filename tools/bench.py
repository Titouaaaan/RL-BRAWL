"""Throughput benchmark: game frames simulated per second, across all envs."""
import time

import numpy as np

import fightcore

N, STEPS = 4096, 500
env = fightcore.VecEnv(N)
rng = np.random.default_rng(0)
actions = rng.integers(-1, 2, size=(STEPS, N, 2, 3)).astype(np.int8)  # sticks
actions[..., 2] = rng.integers(0, 4, size=(STEPS, N, 2))  # buttons

t = time.perf_counter()
for a in actions:
    env.step(a)
dt = time.perf_counter() - t
print(f"{N * STEPS / dt:,.0f} steps/s  ({N} envs x {STEPS} steps in {dt:.2f}s)")
