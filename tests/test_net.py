"""The game's C++ bot network (core/net.cpp) must match PyTorch's actor."""
import sys
from pathlib import Path

import numpy as np
import torch

import fightcore

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "train"))
from ppo import ACTIONS, Agent, save_bot  # noqa: E402


def test_cpp_net_matches_pytorch(tmp_path):
    torch.manual_seed(0)
    agent = Agent(fightcore.OBS_DIM, len(ACTIONS), 64)
    for p in agent.actor.parameters():  # bigger weights than the init, so the logits differ a lot
        p.data.normal_(0, 0.3)
    save_bot(agent.state_dict(), tmp_path / "bot.bin")
    data = (tmp_path / "bot.bin").read_bytes()
    env = fightcore.VecEnv(1, str(Path(__file__).resolve().parent.parent / "data" / "moves"))
    rng = np.random.default_rng(0)
    for t in range(50):  # real observations from a random game
        env.step(ACTIONS[rng.integers(len(ACTIONS), size=(1, 2))])
        obs = env.obs[0, t % 2].astype(np.float32)
        want = agent.actor(torch.as_tensor(obs)).detach().numpy()
        got = np.array(fightcore.net_logits(data, obs))
        assert np.allclose(got, want, atol=1e-4), np.abs(got - want).max()
        # Sampling: u walks the softmax's cumulative sum like an inverse CDF.
        cdf = np.cumsum(np.exp(want - want.max()))
        for u in (0.0, 0.3, 0.7, 0.999):
            assert fightcore.net_act(data, obs, u) == min(np.searchsorted(cdf, u * cdf[-1], side="right"), 44)


def test_wrong_game_version_is_refused(tmp_path):
    agent = Agent(fightcore.OBS_DIM + 1, len(ACTIONS), 8)
    save_bot(agent.state_dict(), tmp_path / "old.bin")
    try:
        fightcore.net_logits((tmp_path / "old.bin").read_bytes(), np.zeros(fightcore.OBS_DIM, np.float32))
    except ValueError as e:
        assert "another game version" in str(e)
    else:
        raise AssertionError("an old bot was accepted")
