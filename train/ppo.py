# docs and experiment results can be found at https://docs.cleanrl.dev/rl-algorithms/ppo/#ppopy
#
# OURS: adapted from CleanRL's ppo.py (MIT license, see train/LICENSE-cleanrl),
# commit fe8d8a03c41a7ef5b523e2e354bd01c363e786bb. The unmodified file is the
# previous git commit, so `git log -p train/ppo.py` shows every change. Every
# changed or added line is marked "OURS". The PPO update itself is untouched.
# Changes:
#   1. Env: our C++ fightcore.VecEnv instead of gym.vector (no gymnasium).
#      The learner is always player 0 (observations and actions are egocentric,
#      so both sides are equivalent).
#   2. Actions: Discrete(45), decoded to (stick_x, stick_y, button) by ACTIONS.
#   3. Opponent: player 1 is a frozen bot or network. Only the learner's
#      experience is stored and trained on.
#   4. Truncation bootstrap: when a game hits the time limit, add
#      gamma * V(final_obs) to that step's reward (CleanRL treats timeouts as
#      real endings; with 3-minute games that are often timeouts early on,
#      that would teach the critic the wrong values).
#   5. Hyperparameters: gamma 0.995 (design doc), 256-unit MLP, 256 envs.
#   5b. Reward shaping, annealed to zero: potential-based approach shaping
#      (F = gamma*phi(s') - phi(s), phi = -distance; Ng et al. 1999, doesn't
#      change the optimal policy) and a damage reward that starts at 0.02 per %.
#      Both scale linearly from 1 to 0 over the first shaping_anneal of training,
#      leaving only the +-1 KO reward.
#   6. Logging/eval: win rates vs fixed bots, button usage, checkpoints,
#      snapshot pool for self-play, replays for watching in Godot.
import copy  # OURS
import os
import random
import sys  # OURS
import time
from dataclasses import dataclass
from pathlib import Path  # OURS

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
import tyro
from torch.distributions.categorical import Categorical
from torch.utils.tensorboard import SummaryWriter

import fightcore  # OURS

sys.path.insert(0, str(Path(__file__).resolve().parent))  # OURS: import train/ siblings
import bots  # noqa: E402  OURS
import replay  # noqa: E402  OURS

ROOT = Path(__file__).resolve().parent.parent  # OURS
# OURS: the 45 discrete actions: stick x (3) x stick y (3) x button (none, jump, light, heavy, dodge).
ACTIONS = np.array([(sx, sy, b) for sx in (-1, 0, 1) for sy in (-1, 0, 1) for b in range(5)], dtype=np.int8)


@dataclass
class Args:
    exp_name: str = os.path.basename(__file__)[: -len(".py")]
    """the name of this experiment"""
    seed: int = 1
    """seed of the experiment"""
    torch_deterministic: bool = True
    """if toggled, `torch.backends.cudnn.deterministic=False`"""
    cuda: bool = True
    """if toggled, cuda will be enabled by default"""
    track: bool = False
    """if toggled, this experiment will be tracked with Weights and Biases"""
    wandb_project_name: str = "cleanRL"
    """the wandb's project name"""
    wandb_entity: str = None
    """the entity (team) of wandb's project"""

    # OURS: environment and opponent
    opponent: str = "dummy"
    """who player 1 is: dummy, random, scripted (all four bots mixed), or selfplay"""
    moves_dir: str = str(ROOT / "data" / "moves")
    """folder with the move JSON files"""
    frame_skip: int = 4
    """sim frames per decision"""
    reaction_delay: int = 0
    """observe the game this many frames late"""
    damage_reward: float = 0.02
    """reward per % of damage dealt (and penalty per % taken), at the start (annealed)"""
    approach_reward: float = 0.1
    """potential-based shaping: reward per 10 units of distance closed, at the start (annealed)"""
    shaping_anneal: float = 0.5
    """fraction of training over which both shaping terms fade to 0 (0 = no shaping at all)"""
    snapshot_every: int = 20
    """selfplay: add the current weights to the opponent pool every N iterations"""
    pool_frac: float = 0.2
    """selfplay: fraction of envs that face a pool snapshot instead of the latest weights"""
    eval_every: int = 25
    """every N iterations: win rate vs the fixed bots, and agent_latest.pt"""
    keep_every: int = 250
    """every N iterations: keep a permanent checkpoint (they're ~1 MB each)"""
    replay_every: int = 100
    """every N iterations: record one game to replays/<run>/"""
    eval_games: int = 32
    """games per bot in each evaluation"""
    hidden: int = 256
    """MLP width"""

    # Algorithm specific arguments
    total_timesteps: int = 20_000_000  # OURS: was 500000; counted in learner decisions
    """total timesteps of the experiments"""
    learning_rate: float = 2.5e-4
    """the learning rate of the optimizer"""
    num_envs: int = 256  # OURS: was 4
    """the number of parallel game environments"""
    num_steps: int = 128
    """the number of steps to run in each environment per policy rollout"""
    anneal_lr: bool = True
    """Toggle learning rate annealing for policy and value networks"""
    gamma: float = 0.995  # OURS: was 0.99; ~13 s horizon at 15 decisions/s
    """the discount factor gamma"""
    gae_lambda: float = 0.95
    """the lambda for the general advantage estimation"""
    num_minibatches: int = 4
    """the number of mini-batches"""
    update_epochs: int = 4
    """the K epochs to update the policy"""
    norm_adv: bool = True
    """Toggles advantages normalization"""
    clip_coef: float = 0.2
    """the surrogate clipping coefficient"""
    clip_vloss: bool = True
    """Toggles whether or not to use a clipped loss for the value function, as per the paper."""
    ent_coef: float = 0.01
    """coefficient of the entropy"""
    vf_coef: float = 0.5
    """coefficient of the value function"""
    max_grad_norm: float = 0.5
    """the maximum norm for the gradient clipping"""
    target_kl: float = None
    """the target KL divergence threshold"""

    # to be filled in runtime
    batch_size: int = 0
    """the batch size (computed in runtime)"""
    minibatch_size: int = 0
    """the mini-batch size (computed in runtime)"""
    num_iterations: int = 0
    """the number of iterations (computed in runtime)"""


def layer_init(layer, std=np.sqrt(2), bias_const=0.0):
    torch.nn.init.orthogonal_(layer.weight, std)
    torch.nn.init.constant_(layer.bias, bias_const)
    return layer


class Agent(nn.Module):
    def __init__(self, obs_dim, n_actions, hidden):  # OURS: sizes passed in (no gym spaces)
        super().__init__()
        self.critic = nn.Sequential(
            layer_init(nn.Linear(obs_dim, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, 1), std=1.0),
        )
        self.actor = nn.Sequential(
            layer_init(nn.Linear(obs_dim, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, n_actions), std=0.01),
        )

    def get_value(self, x):
        return self.critic(x)

    def get_action_and_value(self, x, action=None):
        logits = self.actor(x)
        probs = Categorical(logits=logits)
        if action is None:
            action = probs.sample()
        return action, probs.log_prob(action), probs.entropy(), self.critic(x)


# OURS: helpers for playing games outside the training rollout.
def net_policy(net, device):
    """Wraps a network as a bot-style policy: obs (n, OBS_DIM) -> actions (n, 3)."""

    def policy(o):
        with torch.no_grad():
            a, _, _, _ = net.get_action_and_value(torch.as_tensor(o, device=device))
        return ACTIONS[a.cpu().numpy()]

    return policy


def evaluate(policy, opponent, args, n_games):  # OURS
    """Plays n_games to the end; returns (win, loss, draw) fractions for player 0."""
    env = fightcore.VecEnv(n_games, args.moves_dir, args.frame_skip, args.reaction_delay)
    winner = np.full(n_games, -2)
    while (winner == -2).any():
        env.step(np.stack([policy(env.obs[:, 0]), opponent(env.obs[:, 1])], axis=1))
        done = (env.terminated | env.truncated).astype(bool) & (winner == -2)
        winner[done] = env.winner[done]
    return (winner == 0).mean(), (winner == 1).mean(), (winner == -1).mean()


if __name__ == "__main__":
    args = tyro.cli(Args)
    args.batch_size = int(args.num_envs * args.num_steps)
    args.minibatch_size = int(args.batch_size // args.num_minibatches)
    args.num_iterations = args.total_timesteps // args.batch_size
    run_name = f"{args.opponent}__{args.exp_name}__{args.seed}__{int(time.time())}"  # OURS: opponent, not env_id
    if args.track:
        import wandb

        wandb.init(
            project=args.wandb_project_name,
            entity=args.wandb_entity,
            sync_tensorboard=True,
            config=vars(args),
            name=run_name,
            monitor_gym=True,
            save_code=True,
        )
    run_dir = ROOT / "runs" / run_name  # OURS: absolute, so it works from any folder
    writer = SummaryWriter(str(run_dir))
    writer.add_text(
        "hyperparameters",
        "|param|value|\n|-|-|\n%s" % ("\n".join([f"|{key}|{value}|" for key, value in vars(args).items()])),
    )

    # TRY NOT TO MODIFY: seeding
    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.backends.cudnn.deterministic = args.torch_deterministic

    device = torch.device("cuda" if torch.cuda.is_available() and args.cuda else "cpu")

    # OURS: env setup. envs.obs etc. are NumPy views that step() updates in place.
    envs = fightcore.VecEnv(args.num_envs, args.moves_dir, args.frame_skip, args.reaction_delay, args.damage_reward)
    obs_shape, n_actions = (fightcore.OBS_DIM,), len(ACTIONS)

    agent = Agent(fightcore.OBS_DIM, n_actions, args.hidden).to(device)
    optimizer = optim.Adam(agent.parameters(), lr=args.learning_rate, eps=1e-5)

    # OURS: the opponent (player 1). Bots are plain functions; self-play uses
    # the live agent (frozen for the rollout: weights only change in the update
    # phase) plus a pool of past snapshots for pool_frac of the envs.
    # "scripted" mixes all four bots: training only against bots that attack taught
    # an agent to wait for them, and it then stood idle against a passive dummy.
    opp_bots = {"dummy": [bots.dummy], "random": [bots.random],
                "scripted": [bots.jabber, bots.spacer, bots.dummy, bots.random]}
    pool = []  # past agent weights (CPU state_dicts)
    pool_agent = copy.deepcopy(agent)  # holds whichever snapshot is being played
    n_pool = int(args.num_envs * args.pool_frac) if args.opponent == "selfplay" else 0

    def opponent_actions(o):
        if args.opponent in opp_bots:
            groups = opp_bots[args.opponent]  # envs split evenly between the bots
            splits = np.array_split(np.arange(args.num_envs), len(groups))
            return np.concatenate([bot(o[idx]) for bot, idx in zip(groups, splits)])
        latest = net_policy(agent, device)(o[: args.num_envs - n_pool])
        if n_pool == 0:
            return latest
        past = net_policy(pool_agent if pool else agent, device)(o[args.num_envs - n_pool :])
        return np.concatenate([latest, past])

    run_dir.mkdir(parents=True, exist_ok=True)  # OURS
    ep_return = np.zeros(args.num_envs)  # OURS: running episode stats for player 0
    ep_len = np.zeros(args.num_envs, dtype=np.int64)
    ep_stats = []  # (return, length, win) of games finished since the last log

    # OURS: approach shaping potential. phi = -coef * (distance in units) / 10.
    DXI = 2 * fightcore.OBS_FIGHTER  # obs index of dx (/blast_x); dy (/blast_top) follows

    def phi(o):
        dist = np.hypot(o[:, DXI] * bots.BLAST_X, o[:, DXI + 1] * 40.0)
        return -args.approach_reward * dist / 10.0

    # ALGO Logic: Storage setup
    obs = torch.zeros((args.num_steps, args.num_envs) + obs_shape).to(device)  # OURS: obs_shape
    actions = torch.zeros((args.num_steps, args.num_envs)).to(device)  # OURS: Discrete -> scalar per env
    logprobs = torch.zeros((args.num_steps, args.num_envs)).to(device)
    rewards = torch.zeros((args.num_steps, args.num_envs)).to(device)
    dones = torch.zeros((args.num_steps, args.num_envs)).to(device)
    values = torch.zeros((args.num_steps, args.num_envs)).to(device)

    # TRY NOT TO MODIFY: start the game
    global_step = 0
    start_time = time.time()
    envs.reset()  # OURS
    next_obs = torch.Tensor(envs.obs[:, 0].copy()).to(device)  # OURS: learner = player 0
    next_done = torch.zeros(args.num_envs).to(device)

    for iteration in range(1, args.num_iterations + 1):
        # Annealing the rate if instructed to do so.
        if args.anneal_lr:
            frac = 1.0 - (iteration - 1.0) / args.num_iterations
            lrnow = frac * args.learning_rate
            optimizer.param_groups[0]["lr"] = lrnow

        # OURS: shaping scale, 1 -> 0 over the first shaping_anneal of training.
        progress = (iteration - 1.0) / args.num_iterations
        shaping = max(0.0, 1.0 - progress / args.shaping_anneal) if args.shaping_anneal > 0 else 0.0
        envs.damage_reward = args.damage_reward * shaping

        if pool:  # OURS: a fresh pool opponent for this rollout
            pool_agent.load_state_dict(random.choice(pool))

        for step in range(0, args.num_steps):
            global_step += args.num_envs
            obs[step] = next_obs
            dones[step] = next_done

            # ALGO LOGIC: action logic
            with torch.no_grad():
                action, logprob, _, value = agent.get_action_and_value(next_obs)
                values[step] = value.flatten()
            actions[step] = action
            logprobs[step] = logprob

            # OURS: execute the game. Both players act; only player 0's step is stored.
            joint = np.stack([ACTIONS[action.cpu().numpy()], opponent_actions(envs.obs[:, 1])], axis=1)
            phi_before = phi(envs.obs[:, 0])
            envs.step(joint)
            reward = envs.reward[:, 0].copy()
            terminations, truncations = envs.terminated.astype(bool), envs.truncated.astype(bool)
            if shaping > 0:  # OURS: potential-based approach shaping
                phi_after = phi(envs.obs[:, 0])  # for finished games this is already the NEXT game...
                phi_after[truncations] = phi(envs.final_obs[truncations, 0])  # ...so use its last obs
                phi_after[terminations] = 0.0  # a real ending has potential 0 by convention
                reward += shaping * (args.gamma * phi_after - phi_before)
            if truncations.any():  # OURS: bootstrap through time-limit cutoffs
                with torch.no_grad():
                    final_v = agent.get_value(torch.Tensor(envs.final_obs[truncations, 0].copy()).to(device))
                reward[truncations] += args.gamma * final_v.flatten().cpu().numpy()
            next_done = np.logical_or(terminations, truncations)
            rewards[step] = torch.tensor(reward).to(device).view(-1)
            next_obs, next_done = torch.Tensor(envs.obs[:, 0].copy()).to(device), torch.Tensor(next_done).to(device)

            # OURS: episode stats (replaces gym's RecordEpisodeStatistics)
            ep_return += envs.reward[:, 0]
            ep_len += 1
            for i in np.flatnonzero(terminations | truncations):
                ep_stats.append((ep_return[i], ep_len[i], envs.winner[i] == 0))
                ep_return[i], ep_len[i] = 0, 0

        # bootstrap value if not done
        with torch.no_grad():
            next_value = agent.get_value(next_obs).reshape(1, -1)
            advantages = torch.zeros_like(rewards).to(device)
            lastgaelam = 0
            for t in reversed(range(args.num_steps)):
                if t == args.num_steps - 1:
                    nextnonterminal = 1.0 - next_done
                    nextvalues = next_value
                else:
                    nextnonterminal = 1.0 - dones[t + 1]
                    nextvalues = values[t + 1]
                delta = rewards[t] + args.gamma * nextvalues * nextnonterminal - values[t]
                advantages[t] = lastgaelam = delta + args.gamma * args.gae_lambda * nextnonterminal * lastgaelam
            returns = advantages + values

        # flatten the batch
        b_obs = obs.reshape((-1,) + obs_shape)  # OURS: obs_shape
        b_logprobs = logprobs.reshape(-1)
        b_actions = actions.reshape(-1)  # OURS: Discrete -> scalar per env
        b_advantages = advantages.reshape(-1)
        b_returns = returns.reshape(-1)
        b_values = values.reshape(-1)

        # Optimizing the policy and value network
        b_inds = np.arange(args.batch_size)
        clipfracs = []
        for epoch in range(args.update_epochs):
            np.random.shuffle(b_inds)
            for start in range(0, args.batch_size, args.minibatch_size):
                end = start + args.minibatch_size
                mb_inds = b_inds[start:end]

                _, newlogprob, entropy, newvalue = agent.get_action_and_value(b_obs[mb_inds], b_actions.long()[mb_inds])
                logratio = newlogprob - b_logprobs[mb_inds]
                ratio = logratio.exp()

                with torch.no_grad():
                    # calculate approx_kl http://joschu.net/blog/kl-approx.html
                    old_approx_kl = (-logratio).mean()
                    approx_kl = ((ratio - 1) - logratio).mean()
                    clipfracs += [((ratio - 1.0).abs() > args.clip_coef).float().mean().item()]

                mb_advantages = b_advantages[mb_inds]
                if args.norm_adv:
                    mb_advantages = (mb_advantages - mb_advantages.mean()) / (mb_advantages.std() + 1e-8)

                # Policy loss
                pg_loss1 = -mb_advantages * ratio
                pg_loss2 = -mb_advantages * torch.clamp(ratio, 1 - args.clip_coef, 1 + args.clip_coef)
                pg_loss = torch.max(pg_loss1, pg_loss2).mean()

                # Value loss
                newvalue = newvalue.view(-1)
                if args.clip_vloss:
                    v_loss_unclipped = (newvalue - b_returns[mb_inds]) ** 2
                    v_clipped = b_values[mb_inds] + torch.clamp(
                        newvalue - b_values[mb_inds],
                        -args.clip_coef,
                        args.clip_coef,
                    )
                    v_loss_clipped = (v_clipped - b_returns[mb_inds]) ** 2
                    v_loss_max = torch.max(v_loss_unclipped, v_loss_clipped)
                    v_loss = 0.5 * v_loss_max.mean()
                else:
                    v_loss = 0.5 * ((newvalue - b_returns[mb_inds]) ** 2).mean()

                entropy_loss = entropy.mean()
                loss = pg_loss - args.ent_coef * entropy_loss + v_loss * args.vf_coef

                optimizer.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(agent.parameters(), args.max_grad_norm)
                optimizer.step()

            if args.target_kl is not None and approx_kl > args.target_kl:
                break

        y_pred, y_true = b_values.cpu().numpy(), b_returns.cpu().numpy()
        var_y = np.var(y_true)
        explained_var = np.nan if var_y == 0 else 1 - np.var(y_true - y_pred) / var_y

        # TRY NOT TO MODIFY: record rewards for plotting purposes
        writer.add_scalar("charts/learning_rate", optimizer.param_groups[0]["lr"], global_step)
        writer.add_scalar("losses/value_loss", v_loss.item(), global_step)
        writer.add_scalar("losses/policy_loss", pg_loss.item(), global_step)
        writer.add_scalar("losses/entropy", entropy_loss.item(), global_step)
        writer.add_scalar("losses/old_approx_kl", old_approx_kl.item(), global_step)
        writer.add_scalar("losses/approx_kl", approx_kl.item(), global_step)
        writer.add_scalar("losses/clipfrac", np.mean(clipfracs), global_step)
        writer.add_scalar("losses/explained_variance", explained_var, global_step)
        print("SPS:", int(global_step / (time.time() - start_time)))
        writer.add_scalar("charts/SPS", int(global_step / (time.time() - start_time)), global_step)

        # OURS: game stats, action usage, self-play pool, periodic eval.
        if ep_stats:
            r, length, win = map(np.array, zip(*ep_stats))
            writer.add_scalar("charts/episodic_return", r.mean(), global_step)
            writer.add_scalar("charts/episodic_length", length.mean(), global_step)
            writer.add_scalar("charts/win_rate_train", win.mean(), global_step)
            print(f"iter {iteration}/{args.num_iterations}  games {len(r)}  return {r.mean():.2f}  "
                  f"win {win.mean():.2f}  length {length.mean():.0f}")
            ep_stats.clear()
        writer.add_scalar("charts/shaping_scale", shaping, global_step)
        # OURS: per-move usage (share of all moves started) and hit rate. A move
        # stuck near 0% is either useless or badly tuned: fix it in data/moves.
        uses, hits = envs.move_uses.copy(), envs.move_hits.copy()
        envs.move_uses[:] = 0
        envs.move_hits[:] = 0
        for m, name in enumerate(fightcore.MOVE_NAMES):
            writer.add_scalar(f"moves/{name}_use", uses[m] / max(uses.sum(), 1), global_step)
            if uses[m] > 0:
                writer.add_scalar(f"moves/{name}_hit_rate", hits[m] / uses[m], global_step)
        if iteration % args.eval_every == 0:
            share = uses / max(uses.sum(), 1)
            print("  moves used: " + "  ".join(f"{n} {s:.0%}" for n, s in zip(fightcore.MOVE_NAMES, share)))
        buttons = np.bincount(ACTIONS[b_actions.long().cpu().numpy()][:, 2], minlength=5) / args.batch_size
        for name, frac_used in zip(("none", "jump", "light", "heavy", "dodge"), buttons):
            writer.add_scalar(f"actions/{name}", frac_used, global_step)

        if args.opponent == "selfplay" and iteration % args.snapshot_every == 0:
            pool.append({k: v.detach().cpu().clone() for k, v in agent.state_dict().items()})

        if iteration % args.eval_every == 0 or iteration == args.num_iterations:
            me = net_policy(agent, device)
            for name in ("dummy", "random", "jabber", "spacer"):
                w, l, d = evaluate(me, bots.BOTS[name], args, args.eval_games)
                writer.add_scalar(f"eval/win_vs_{name}", w, global_step)
                print(f"  eval vs {name:7s} win {w:.2f} loss {l:.2f} draw {d:.2f}")
            torch.save(agent.state_dict(), run_dir / "agent_latest.pt")
            if iteration % args.keep_every == 0:
                torch.save(agent.state_dict(), run_dir / f"agent_{iteration:05d}.pt")

        if iteration % args.replay_every == 0 or iteration == args.num_iterations:
            # A replay against the training opponent, to watch in Godot (play.bat replay).
            me = net_policy(agent, device)
            opp = me if args.opponent == "selfplay" else opp_bots[args.opponent][0]
            opp_name = "self" if args.opponent == "selfplay" else opp_bots[args.opponent][0].__name__
            rep = replay.play_and_record(args.moves_dir, me, opp, args.frame_skip, args.reaction_delay,
                                         meta={"run": run_name, "iteration": iteration, "p1": "agent", "p2": opp_name})
            replay.save(rep, ROOT / "replays" / run_name / f"iter_{iteration:05d}.json")

    writer.close()  # OURS: no envs.close() needed
