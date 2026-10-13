"""The bot server that the live Godot game talks to (train/serve.py)."""
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

import fightcore

ROOT = Path(__file__).resolve().parent.parent
PORT = 5077


def connect():
    for _ in range(200):  # the server needs a moment to start
        try:
            return socket.create_connection(("127.0.0.1", PORT), timeout=5)
        except OSError:
            time.sleep(0.1)
    raise AssertionError("server never came up")


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        buf += s.recv(n - len(buf))
    return buf


def test_handshake_and_one_decision():
    server = subprocess.Popen([sys.executable, str(ROOT / "train" / "serve.py"), "fighter", "--port", str(PORT)])
    try:
        s = connect()
        s.sendall(struct.pack("<i", fightcore.OBS_DIM))  # hello: our observation size
        assert struct.unpack("<i", recv_exact(s, 4))[0] == fightcore.OBS_DIM
        env = fightcore.VecEnv(1, str(ROOT / "data" / "moves"))
        s.sendall(env.obs[0, 1].astype("<f4").tobytes())  # player 2's view, as Godot sends it
        action = np.frombuffer(recv_exact(s, 3), dtype=np.int8)
        assert -1 <= action[0] <= 1 and -1 <= action[1] <= 1 and 0 <= action[2] <= 4
        s.close()
        assert server.wait(timeout=10) == 0  # the server exits when the game disconnects
    finally:
        server.kill()
