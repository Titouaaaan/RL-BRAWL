"""Exports checkpoints for the game's C++ bot (core/net.cpp): agent_X.pt -> agent_X.bin.

    python tools/export_bot.py runs/<run>                     # every checkpoint of a run, next to each .pt
    python tools/export_bot.py runs/<run>/agent_00600.pt --out data/bots

ppo.py writes the .bin itself for new checkpoints; this is for older ones.
Only the actor is kept (about 0.5 MB): playing never needs the critic.
"""
import argparse
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "train"))
from ppo import save_bot  # noqa: E402

p = argparse.ArgumentParser()
p.add_argument("paths", nargs="+", type=Path, help="checkpoints (.pt) or run folders")
p.add_argument("--out", type=Path, help="folder to write into (default: next to each .pt)")
args = p.parse_args()
for path in args.paths:
    for pt in sorted(path.glob("agent_*.pt")) if path.is_dir() else [path]:
        out = (args.out or pt.parent) / pt.with_suffix(".bin").name
        save_bot(torch.load(pt, map_location="cpu"), out)
        print(out)
