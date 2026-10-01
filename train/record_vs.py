"""Records one game of an agent against every compatible checkpoint and the
scripted bots, headless (no drawing, so fast), under training conditions.

    python train/record_vs.py                          # the newest run's agent vs everything
    python train/record_vs.py --agent runs/<run>/agent_02500.pt

Saves replays/latest_vs_all/<agent>_vs_<opponent>.json, e.g.
league-480M_vs_main-150M.json or league-480M_vs_bot-fighter.json, where a
checkpoint is named by its run and its training decisions. The agent is P1.
Checkpoints from older game versions (a different observation size) are skipped.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bots  # noqa: E402
import replay  # noqa: E402
from ppo import ACTIONS, ROOT, Agent, net_policy  # noqa: E402

import fightcore  # noqa: E402


def run_label(run_dir):
    """runs/selfplay__league__1__... -> league; runs/ablation-selfplay__nobuffer__... -> ablation-nobuffer."""
    return run_dir.name.replace("selfplay__", "").split("__")[0]


def label(ckpt):
    """league-480M: run label + training decisions (iterations x batch size)."""
    resume = ckpt.parent / "resume.pt"
    batch = 32768
    final_step = None
    if resume.exists():
        r = torch.load(resume, map_location="cpu", weights_only=False)
        batch = r["args"]["num_envs"] * r["args"]["num_steps"]
        final_step = r["global_step"]
    if ckpt.stem == "agent_latest":
        steps = final_step
    else:
        steps = int(ckpt.stem.replace("agent_", "")) * batch
    return f"{run_label(ckpt.parent)}-{round(steps / 1e6):03d}M" if steps else f"{run_label(ckpt.parent)}-latest"


def load_policy(path):
    sd = torch.load(path, map_location="cpu")
    if sd["actor.0.weight"].shape[1] != fightcore.OBS_DIM:
        return None  # trained on an older version of the game
    agent = Agent(fightcore.OBS_DIM, len(ACTIONS), sd["actor.0.weight"].shape[0])
    agent.load_state_dict(sd)
    return net_policy(agent, "cpu")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--agent", default="latest", help="a .pt checkpoint, or 'latest' (the newest run's final agent)")
    p.add_argument("--out", default=str(ROOT / "replays" / "latest_vs_all"))
    args = p.parse_args()

    if args.agent == "latest":
        agent_path = max((ROOT / "runs").glob("*/agent_latest.pt"), key=lambda f: f.stat().st_mtime)
    else:
        agent_path = Path(args.agent)
    me = load_policy(agent_path)
    me_name = label(agent_path)
    opponents = [(f"bot-{name}", bots.BOTS[name]) for name in ("fighter", "spacer", "jabber", "random", "dummy")]
    skipped = 0
    for ckpt in sorted((ROOT / "runs").glob("*/agent_*.pt")):
        policy = load_policy(ckpt)
        if policy is None:
            skipped += 1
            continue
        opponents.append((label(ckpt), policy))
    print(f"{me_name} vs {len(opponents)} opponents ({skipped} checkpoints skipped: older game versions)")

    wins = 0
    for k, (name, opp) in enumerate(opponents):
        torch.manual_seed(k)  # each game is reproducible
        np.random.seed(k)
        rep = replay.play_and_record(ROOT / "data" / "moves", me, opp, reaction_delay=6,
                                     meta={"p1": me_name, "p2": name, "agent": f"{agent_path.parent.name}/{agent_path.name}"})
        out = replay.save(rep, Path(args.out) / f"{me_name}_vs_{name}.json")
        result = {0: f"{me_name} won", 1: f"{name} won", -1: "draw"}[rep["winner"]]
        wins += rep["winner"] == 0
        print(f"  {out.name:45s} {len(rep['inputs']) // 60:4d} s   {result}", flush=True)
    print(f"{me_name} won {wins} of {len(opponents)}")


if __name__ == "__main__":
    main()
