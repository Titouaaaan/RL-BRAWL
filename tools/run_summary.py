"""Writes runs/<run>/summary.json for the game's Training screen (godot/menu.gd).

    python tools/run_summary.py          # every run whose summary is missing or older than its logs

Godot can't read TensorBoard files, and parsing one takes about a minute, so
this caches what the screen shows: settings, network size, training totals,
every curve (downsampled), and the run's Elo from runs/ladder*.txt.
The menu starts it in the background each time the Training screen opens.
"""
import json
import math
import re
from pathlib import Path

import torch
from tensorboard.backend.event_processing.event_accumulator import EventAccumulator

ROOT = Path(__file__).resolve().parent.parent
POINTS = 300  # per curve


def ladder():
    """[(elo, name)] and {(row, col): score} from every runs/ladder*.txt."""
    elos, scores = [], {}
    for f in ROOT.glob("runs/ladder*.txt"):
        lines = f.read_text().splitlines()
        for line in lines[lines.index("Elo (first entry = 1000)") + 1:] if "Elo (first entry = 1000)" in lines else []:
            if m := re.match(r"\s*(-?\d+)\s+(\S+)$", line):
                elos.append((int(m[1]), m[2]))
        for line in lines:
            if m := re.match(r"\s*(\S+) vs (\S+)\s+([\d.]+)$", line):
                scores[m[1], m[2]] = float(m[3])
    return elos, scores


def summarize(run, elos, scores):
    ea = EventAccumulator(str(run), size_guidance={"scalars": 0, "tensors": 1})
    ea.Reload()
    curves, t0, t1 = {}, None, None
    for tag in ea.Tags()["scalars"]:
        ev = [e for e in ea.Scalars(tag) if math.isfinite(e.value)]  # some rates are NaN when nothing happened
        if not ev:
            continue
        k = -(-len(ev) // POINTS)  # bucket size (ceil): each point is the mean of k logged values
        curves[tag] = [[ev[i + k // 2 if i + k // 2 < len(ev) else -1].step,
                        round(sum(e.value for e in ev[i:i + k]) / len(ev[i:i + k]), 5)] for i in range(0, len(ev), k)]
        t0 = min(t0 or ev[0].wall_time, ev[0].wall_time)
        t1 = max(t1 or 0, ev[-1].wall_time)
    resume = torch.load(run / "resume.pt", map_location="cpu", weights_only=False) if (run / "resume.pt").exists() else {}
    sd = torch.load(run / "agent_latest.pt", map_location="cpu") if (run / "agent_latest.pt").exists() else {}
    tag = run.name[-12:]  # ladder.py names checkpoints by the run folder's last 12 characters
    mine = [(e, n.split("/")[1]) for e, n in elos if n.startswith(tag + "/")]
    latest = tag + "/latest"
    return {
        "name": run.name,
        "args": resume.get("args", {}),
        "iteration": resume.get("iteration", 0),
        "global_step": resume.get("global_step", 0),
        "generation": resume.get("gate", [0])[0],
        "wall_hours": round((t1 - t0) / 3600, 2) if t0 else 0,
        "started": int(t0 or 0),
        "checkpoints": len(list(run.glob("agent_[0-9]*.pt"))),
        "network": {
            "params": sum(v.numel() for v in sd.values()),
            "layers": [[k, list(v.shape)] for k, v in sd.items() if k.endswith("weight")],
        },
        "elo": sorted(mine, key=lambda x: x[1]),
        "head_to_head": sorted(  # the run's final agent against everything it was laddered with
            [[c, s] for (r, c), s in scores.items() if r == latest]
            + [[r, round(1 - s, 2)] for (r, c), s in scores.items() if c == latest],
            key=lambda x: x[0]),
        "curves": curves,
    }


def main():
    elos, scores = ladder()
    for run in sorted(p for p in (ROOT / "runs").iterdir() if p.is_dir()):
        logs = list(run.glob("events.out.tfevents.*"))
        out = run / "summary.json"
        newest = max([f.stat().st_mtime for f in logs + list(ROOT.glob("runs/ladder*.txt"))], default=0)
        if not logs or (out.exists() and out.stat().st_mtime >= newest):
            continue
        print("summarizing", run.name, flush=True)
        tmp = out.with_suffix(".tmp")
        tmp.write_text(json.dumps(summarize(run, elos, scores)))
        tmp.replace(out)  # atomic: the game never reads a half-written file


if __name__ == "__main__":
    main()
