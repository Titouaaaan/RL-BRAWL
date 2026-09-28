"""Record one game of a trained checkpoint against a bot (or itself), to watch in Godot.

    python train/record.py runs/<run>/agent_00600.pt --opponent jabber
    play.bat replay
"""
import argparse
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bots  # noqa: E402
import replay  # noqa: E402
from ppo import ACTIONS, ROOT, Agent, net_policy  # noqa: E402

import fightcore  # noqa: E402

p = argparse.ArgumentParser()
p.add_argument("checkpoint")
p.add_argument("--opponent", default="jabber", help="dummy, random, jabber, spacer, or self")
p.add_argument("--hidden", type=int, default=256)
args = p.parse_args()

agent = Agent(fightcore.OBS_DIM, len(ACTIONS), args.hidden)
agent.load_state_dict(torch.load(args.checkpoint, map_location="cpu"))
me = net_policy(agent, "cpu")
opp = me if args.opponent == "self" else bots.BOTS[args.opponent]

ckpt = Path(args.checkpoint)
rep = replay.play_and_record(ROOT / "data" / "moves", me, opp,
                             meta={"checkpoint": f"{ckpt.parent.name}/{ckpt.name}", "p1": "agent", "p2": args.opponent})
out = replay.save(rep, ROOT / "replays" / "recorded" / f"{ckpt.parent.name}_{ckpt.stem}_vs_{args.opponent}.json")
print(f"saved {out}  ({len(rep['inputs']) // 60} s, winner: {['draw', 'agent', args.opponent][rep['winner'] + 1]})")
