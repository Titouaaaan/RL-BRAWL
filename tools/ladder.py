"""Round-robin tournament between checkpoints (and scripted bots), with Elo.

    python tools/ladder.py runs/<run>/agent_02500.pt runs/<run>/agent_latest.pt ... [--games 128]
    python tools/ladder.py runs/<run> --every 4      # every 4th saved checkpoint of a run, plus its latest

Every pair plays --games games under the training conditions (frame skip 4,
reaction delay 6, no input buffer). Prints the win-rate matrix (row beats
column; draws count half) and Elo ratings fitted to all results
(Bradley-Terry), anchored so the first entry is 1000. Names can also be
scripted bots (fighter, spacer, ...).
"""
import argparse
import itertools
import sys
from pathlib import Path

import numpy as np
import torch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "train"))
import bots  # noqa: E402
from ppo import ACTIONS, Agent, net_policy  # noqa: E402

import fightcore  # noqa: E402


def load(entry, hidden):
    if entry in bots.BOTS:
        return entry, bots.BOTS[entry]
    path = Path(entry)
    agent = Agent(fightcore.OBS_DIM, len(ACTIONS), hidden)
    agent.load_state_dict(torch.load(path, map_location="cpu"))
    return f"{path.parent.name[-12:]}/{path.stem.replace('agent_', '')}", net_policy(agent, "cpu")


def play(p0, p1, n):
    """Score of p0 against p1 over n games (win 1, draw 0.5)."""
    env = fightcore.VecEnv(n, str(ROOT / "data" / "moves"), reaction_delay=6)
    result = np.full(n, -2)
    while (result == -2).any():
        env.step(np.stack([p0(env.obs[:, 0]), p1(env.obs[:, 1])], axis=1))
        done = (env.terminated | env.truncated).astype(bool) & (result == -2)
        result[done] = env.winner[done]
    return float(np.mean(np.where(result == 0, 1.0, np.where(result == -1, 0.5, 0.0))))


def elo(score, games):
    """Bradley-Terry fit by simple gradient steps on the log-likelihood."""
    k = len(score)
    r = np.zeros(k)
    for _ in range(5000):
        grad = np.zeros(k)
        for i, j in itertools.permutations(range(k), 2):
            if np.isnan(score[i, j]):
                continue
            expected = 1 / (1 + 10 ** ((r[j] - r[i]) / 400))
            grad[i] += games * (score[i, j] - expected)
        r += 2.0 * grad / (games * k)
    return r - r[0] + 1000


def main():
    p = argparse.ArgumentParser()
    p.add_argument("entries", nargs="+", help="checkpoints (.pt), run folders, or scripted bot names")
    p.add_argument("--games", type=int, default=128)
    p.add_argument("--every", type=int, default=1, help="for a run folder: every Nth saved checkpoint")
    p.add_argument("--hidden", type=int, default=256)
    args = p.parse_args()

    entries = []
    for e in args.entries:
        if Path(e).is_dir():
            saved = sorted(Path(e).glob("agent_[0-9]*.pt"))[args.every - 1 :: args.every]
            entries += [str(f) for f in saved] + [str(Path(e) / "agent_latest.pt")]
        else:
            entries.append(e)
    players = [load(e, args.hidden) for e in entries]
    names = [n for n, _ in players]
    k = len(players)
    score = np.full((k, k), np.nan)
    torch.manual_seed(0)
    np.random.seed(0)
    for i, j in itertools.combinations(range(k), 2):
        s = play(players[i][1], players[j][1], args.games)
        score[i, j], score[j, i] = s, 1 - s
        print(f"{names[i]:>20} vs {names[j]:<20} {s:.2f}", flush=True)

    ratings = elo(score, args.games)
    width = max(len(n) for n in names)
    print("\nwin rate of the row against the column (draws count half)")
    print(" " * (width + 2) + "".join(f"{n[-8:]:>9}" for n in names))
    for i in range(k):
        row = "".join("        -" if i == j else f"{score[i, j]:9.2f}" for j in range(k))
        print(f"{names[i]:>{width}}  {row}")
    print("\nElo (first entry = 1000)")
    for i in np.argsort(-ratings):
        print(f"  {ratings[i]:7.0f}  {names[i]}")


if __name__ == "__main__":
    main()
