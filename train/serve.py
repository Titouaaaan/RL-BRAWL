"""Serves a bot to the live Godot game, so you can fight it.

    python train/serve.py latest          # newest run's agent (play.bat --vs latest starts this for you)
    python train/serve.py runs/<run>/agent_00600.pt
    python train/serve.py fighter         # or a scripted bot: dummy, random, jabber, spacer, fighter

Godot connects over TCP on 127.0.0.1 (a local connection, nothing leaves the
machine). Protocol, all little-endian:
  1. Godot sends its observation size (int32); the server answers with its own.
     If they differ the two were built from different game versions, and the
     server quits instead of playing nonsense.
  2. Then, once per bot decision (every 4 frames), Godot sends the bot's
     observation (OBS_DIM float32, already delayed by the reaction time) and
     the server answers with an action: 3 int8 (stick x, stick y, button),
     egocentric like in training (Godot un-mirrors it for player 2).
The server exits when Godot disconnects.
"""
import argparse
import socket
import struct
import sys
import time
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bots  # noqa: E402
from ppo import ACTIONS, ROOT, Agent, net_policy  # noqa: E402

import fightcore  # noqa: E402


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None  # Godot closed the connection
        buf += chunk
    return buf


def load_policy(name, hidden):
    if name in bots.BOTS:
        return bots.BOTS[name], name
    if name == "latest":
        path = max((ROOT / "runs").glob("*/agent_latest.pt"), key=lambda f: f.stat().st_mtime)
    else:
        path = Path(name)
    agent = Agent(fightcore.OBS_DIM, len(ACTIONS), hidden)
    agent.load_state_dict(torch.load(path, map_location="cpu"))
    return net_policy(agent, "cpu"), f"{path.parent.name}/{path.name}"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("bot", nargs="?", default="latest", help="'latest', a .pt checkpoint, or a scripted bot name")
    p.add_argument("--port", type=int, default=5055)
    p.add_argument("--hidden", type=int, default=256)
    args = p.parse_args()

    torch.set_num_threads(1)  # one tiny network, one decision at a time: threads only add overhead
    policy, name = load_policy(args.bot, args.hidden)
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", args.port))
    server.listen(1)
    print(f"[serve] {name} waiting for Godot on port {args.port}", flush=True)
    server.settimeout(120)  # if the game never shows up (it crashed?), don't hold the port forever
    try:
        conn, _ = server.accept()
    except socket.timeout:
        print("[serve] no game connected within 2 minutes, quitting", flush=True)
        return
    conn.settimeout(None)
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)  # send small replies right away

    hello = recv_exact(conn, 4)
    their_dim = struct.unpack("<i", hello)[0] if hello else -1
    conn.sendall(struct.pack("<i", fightcore.OBS_DIM))
    if their_dim != fightcore.OBS_DIM:
        print(f"[serve] Godot's observation size is {their_dim}, this bot's is {fightcore.OBS_DIM}: "
              "rebuild the game (play.bat) and the Python module (pip install .) from the same code.", flush=True)
        return
    print("[serve] connected", flush=True)

    n, total = 0, 0.0
    while True:
        data = recv_exact(conn, 4 * fightcore.OBS_DIM)
        if data is None:
            break
        t = time.perf_counter()
        obs = np.frombuffer(data, dtype="<f4").reshape(1, -1).copy()  # copy: PyTorch wants a writable array
        action = np.asarray(policy(obs), dtype=np.int8).reshape(3)
        conn.sendall(action.tobytes())
        total += time.perf_counter() - t
        n += 1
        if n % 900 == 0:  # about once a minute of play
            print(f"[serve] {n} decisions, {1000 * total / n:.2f} ms each on average", flush=True)
    print(f"[serve] Godot disconnected after {n} decisions", flush=True)


if __name__ == "__main__":
    main()
