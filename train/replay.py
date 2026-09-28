"""Game recordings ("replays") for watching in Godot. Not an RL replay buffer.

The sim is deterministic, so a replay is just the per-frame inputs of both
players, plus a copy of the move files it was played with. Godot re-simulates
the game from them and checks the final state hash to catch desyncs.

A policy here is any function: obs (n, OBS_DIM) float32 -> actions (n, 3) int8,
where actions are egocentric (stick_x, stick_y, button), like VecEnv expects.
"""
import json
import time
from pathlib import Path

import numpy as np

import fightcore

MOVE_NAMES = fightcore.MOVE_NAMES  # the files core/moves.cpp loads, from C++


def play_and_record(moves_dir, policy0, policy1, frame_skip=4, reaction_delay=0, meta=None):
    """Plays one full game in a 1-env VecEnv (same rules as training) and
    returns it as a replay dict."""
    env = fightcore.VecEnv(1, str(moves_dir), frame_skip=frame_skip, reaction_delay=reaction_delay)
    inputs = []  # one [p1_sx, p1_sy, p1_b, p2_sx, p2_sy, p2_b] per sim frame, real (unmirrored) inputs
    while True:
        a = np.stack([policy0(env.obs[:, 0]), policy1(env.obs[:, 1])], axis=1).astype(np.int8)  # (1, 2, 3)
        env.step(a)
        real = a[0].copy()
        real[1, 0] = -np.clip(real[1, 0], -1, 1)  # undo player 1's mirroring, as vecenv.cpp does
        # Each decision repeats for frame_skip frames. If the game ended partway,
        # the extra frames are harmless: step() ignores a finished game.
        inputs.extend([real.reshape(-1).tolist()] * frame_skip)
        if env.terminated[0] or env.truncated[0]:
            break
    return {
        "version": 1,
        "created": time.strftime("%Y-%m-%d %H:%M:%S"),
        "meta": meta or {},
        "winner": int(env.winner[0]),
        "final_hash": f"{int(env.final_hash[0]):016x}",
        "moves": {name: (Path(moves_dir) / f"{name}.json").read_text() for name in MOVE_NAMES},
        "inputs": inputs,
    }


def save(replay, path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # separators=(",", ":") drops spaces: a 3-minute game is ~10k input rows.
    path.write_text(json.dumps(replay, separators=(",", ":")))
    return path


def random_policy(obs):
    n = len(obs)
    a = np.random.randint(-1, 2, size=(n, 3))
    a[:, 2] = np.random.randint(0, 5, size=n)
    return a.astype(np.int8)


if __name__ == "__main__":
    # Quick check: record a random-vs-random game.
    #   python train/replay.py   then   play.bat replay
    root = Path(__file__).resolve().parent.parent
    r = play_and_record(root / "data" / "moves", random_policy, random_policy, meta={"p1": "random", "p2": "random"})
    out = save(r, root / "replays" / "random" / "random_vs_random.json")
    print(f"saved {out} ({len(r['inputs'])} frames, winner {r['winner']}, hash {r['final_hash']})")
