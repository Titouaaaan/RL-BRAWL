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
#   4. Time limit: a real ending, like CleanRL does (the time left is in the
#      observation, so the 7-minute limit is part of the game, not a cutoff),
#      with a small tie-breaker reward for whoever is ahead at the limit.
#   5. Hyperparameters: gamma 0.995 (design doc), 256-unit MLP, 256 envs.
#   5b. Reward shaping, annealed to zero: potential-based approach shaping
#      (F = gamma*phi(s') - phi(s), phi = -distance; Ng et al. 1999, doesn't
#      change the optimal policy) and a damage reward that starts at 0.02 per %.
#      Both scale linearly from 1 down to shaping_floor (0.2) over the first
#      shaping_anneal of training, so the KO reward dominates later on.
#   6. Logging/eval: win rates vs fixed bots, button usage, checkpoints,
#      replays for watching in Godot.
#   8. --resume runs/<run> continues a run from its last resume.pt (saved at
#      every eval): weights, optimizer, iteration, self-play opponents.
#   7. Self-play is gated: the opponent is a frozen copy of the agent, replaced
#      only once the learner beats it (like AlphaGo Zero's 55% gate). Retired
#      opponents form a pool that plays pool_frac of the games.
#   9. Against collapse onto one strategy: scripted bots keep bot_frac of the
#      self-play games; the entropy bonus starts high and is annealed; a combo
#      bonus grows the damage reward for hits deep in a combo (zero-sum,
#      capped, annealed with the damage reward); and extensive logging of
#      moves, states, actions and mechanics (the mech/ charts) to spot it.
#  11. League (--league runs/<older run>): that run's saved checkpoints play
#      league_frac of the games, each picked by how close the matchup is
#      (weight p*(1-p), p = the learner's recent win rate against it; AlphaStar's
#      prioritized fictitious self-play), and the old run's final agent is an
#      extra evaluation opponent (eval/win_vs_league_best).
#  10. Counterattack bonus (zero-sum, annealed with the damage reward): a hit
#      right after a successful dodge / parry / shield block, or on an opponent
#      recovering from a missed move, earns counter_reward. Defending alone
#      earns nothing, so dodge spam isn't rewarded; whiffing gets punished.
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
    reaction_delay: int = 6
    """observe the game this many frames late (6 = 0.1 s, like a quick human reaction)"""
    timeout_reward: float = 0.3
    """at the 7-minute limit: + for the player ahead (stocks, then damage), - for the other"""
    combo_growth: float = 1.0
    """the k-th hit of a true combo gets its damage reward times combo_growth^(k-1)... (off: at
    ~0.1 true combos/min it did nothing; the streak bonus replaces it)"""
    combo_cap: float = 1.0
    """...up to this multiplier. 1.0 turns the combo bonus off"""
    streak_growth: float = 1.1
    """the k-th hit of a hit streak (hits since you were last hit; dodges and pauses don't break
    it) gets its damage reward times streak_growth^(k-1)..."""
    streak_cap: float = 1.5
    """...up to this multiplier (hit 6+ of a streak counts 1.5x). Zero-sum, annealed"""
    counter_reward: float = 0.1
    """bonus for a counterattack or whiff punish (half a 10% hit's damage reward at the start;
    annealed with it). 0 turns it off"""
    bot_frac: float = 0.1
    """selfplay: fraction of games against the scripted bots, so beating simple play is never forgotten"""
    ent_coef_final: float = 0.01
    """the entropy bonus goes from ent_coef down to this over the first ent_anneal of training"""
    ent_anneal: float = 0.5
    """fraction of training over which the entropy bonus is lowered"""
    damage_reward: float = 0.02
    """reward per % of damage dealt (and penalty per % taken), at the start (annealed)"""
    approach_reward: float = 0.1
    """potential-based shaping: reward per 10 units of distance closed, at the start (annealed)"""
    shaping_anneal: float = 0.5
    """fraction of training over which both shaping terms fade to 0 (0 = no shaping at all)"""
    shaping_floor: float = 0.2
    """the shaping never fades below this fraction (0 = fade to nothing). Without a floor, a
    run drifted into cautious spacing after the fade: hits/min halved, timeouts 8% -> 37%"""
    gate_win_rate: float = 0.55
    """selfplay: replace the frozen opponent once the learner wins this often against it..."""
    gate_min_games: int = 200
    """...measured over at least this many finished games"""
    gate_max_iters: int = 100
    """selfplay: replace it anyway after this many iterations, so a stuck learner moves on"""
    pool_frac: float = 0.2
    """selfplay: fraction of envs that face an older (retired) opponent instead of the current one"""
    league: str = ""
    """selfplay: a finished run folder whose saved checkpoints (agent_*.pt) join as opponents"""
    league_frac: float = 0.2
    """selfplay with --league: fraction of envs that face a league checkpoint"""
    league_switch: int = 12
    """iterations between league opponent changes (about one game), so results can be credited to one checkpoint"""
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
    pool_max: int = 50
    """selfplay: most retired opponents kept (a random one is dropped when full)"""
    resume: str = ""
    """continue a run: path to its folder (runs/<run>); all other options come from that run"""

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
    ent_coef: float = 0.03  # OURS: was 0.01; annealed to ent_coef_final (more exploration early)
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


def save_bot(state_dict, path):  # OURS
    """Writes the actor for the game's C++ bot (core/net.cpp): int32 obs size, hidden size,
    actions, then each layer's weight (out x in) and bias as float32, little-endian."""
    w = state_dict["actor.0.weight"]
    head = np.array([w.shape[1], w.shape[0], state_dict["actor.4.weight"].shape[0]], "<i4")
    body = [state_dict[f"actor.{i}.{k}"].detach().cpu().numpy().ravel() for i in (0, 2, 4) for k in ("weight", "bias")]
    Path(path).write_bytes(head.tobytes() + np.concatenate(body).astype("<f4").tobytes())


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
    resume = None  # OURS: continue an earlier run with its own settings
    if args.resume:
        resume_dir = Path(args.resume)
        resume = torch.load(resume_dir / "resume.pt", map_location="cpu", weights_only=False)
        args = Args(**{**resume["args"], "resume": args.resume})
    args.batch_size = int(args.num_envs * args.num_steps)
    args.minibatch_size = int(args.batch_size // args.num_minibatches)
    args.num_iterations = args.total_timesteps // args.batch_size
    run_name = f"{args.opponent}__{args.exp_name}__{args.seed}__{int(time.time())}"  # OURS: opponent, not env_id
    if resume is not None:  # OURS: same folder, so TensorBoard continues the same curves
        run_name = resume_dir.name
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
    envs = fightcore.VecEnv(args.num_envs, args.moves_dir, args.frame_skip, args.reaction_delay,
                            args.damage_reward, args.timeout_reward, args.combo_growth, args.combo_cap,
                            args.counter_reward, args.streak_growth, args.streak_cap)
    obs_shape, n_actions = (fightcore.OBS_DIM,), len(ACTIONS)

    agent = Agent(fightcore.OBS_DIM, n_actions, args.hidden).to(device)
    optimizer = optim.Adam(agent.parameters(), lr=args.learning_rate, eps=1e-5)

    # OURS: the opponent (player 1). Bots are plain functions. Self-play uses a
    # frozen copy of the agent ("gated" self-play): it stays fixed while the
    # learner trains against it, so the learner has a stable target to beat
    # (with the live weights on both sides, the two sides mirrored each other
    # and settled into standoffs). Retired opponents join a pool that plays
    # pool_frac of the games, so older strategies aren't forgotten.
    # "scripted" mixes all four bots: training only against bots that attack taught
    # an agent to wait for them, and it then stood idle against a passive dummy.
    opp_bots = {"dummy": [bots.dummy], "random": [bots.random],
                "scripted": [bots.fighter, bots.jabber, bots.spacer, bots.dummy, bots.random]}
    frozen = copy.deepcopy(agent)  # the current opponent; starts as the untrained agent
    generation, gen_start_iter, gate_games, gate_wins = 0, 1, 0, 0
    pool = []  # retired opponents' weights (CPU state_dicts)
    pool_agent = copy.deepcopy(agent)  # holds whichever retired opponent is being played
    # Self-play layout of the envs: [frozen opponent | retired pool | league | scripted bots].
    selfplay = args.opponent == "selfplay"
    n_pool = int(args.num_envs * args.pool_frac) if selfplay else 0
    n_league = int(args.num_envs * args.league_frac) if selfplay and args.league else 0
    n_bots = int(args.num_envs * args.bot_frac) if selfplay else 0
    n_frozen = args.num_envs - n_pool - n_league - n_bots  # envs [0, n_frozen) face the frozen opponent
    league_lo = n_frozen + n_pool  # envs [league_lo, league_lo + n_league) face the league
    selfplay_bots = [bots.fighter, bots.jabber, bots.spacer, bots.dummy, bots.random]

    # OURS: the league: an older run's checkpoints, each with the learner's
    # recent win rate against it (p, a moving average over its games).
    league = []  # [{"name", "weights", "p"}]
    if n_league:
        files = sorted(Path(args.league).glob("agent_[0-9]*.pt")) + [Path(args.league) / "agent_latest.pt"]
        for f in files:
            league.append({"name": f.stem.replace("agent_", ""), "p": 0.5,
                           "weights": torch.load(f, map_location="cpu")})
        print(f"league: {len(league)} checkpoints from {args.league}")
    league_agent = copy.deepcopy(agent)  # holds whichever league checkpoint is being played
    league_best = copy.deepcopy(agent)  # the league run's final agent, an extra evaluation opponent
    if league:
        league_best.load_state_dict(league[-1]["weights"])
    league_cur = 0

    def pick_league():
        # Closest matchups first: weight p*(1-p) peaks at a 50% win rate; the
        # 0.02 floor keeps every checkpoint occasionally tried (its p can move).
        w = np.array([c["p"] * (1 - c["p"]) + 0.02 for c in league])
        return int(np.random.choice(len(league), p=w / w.sum()))

    def group_of(i):  # which kind of opponent env i faces, for logging
        if not selfplay:
            return args.opponent
        if i < n_frozen:
            return "frozen"
        if i < league_lo:
            return "pool"
        return "league" if i < league_lo + n_league else "bots"

    def opponent_actions(o):
        if args.opponent in opp_bots:
            groups = opp_bots[args.opponent]  # envs split evenly between the bots
            splits = np.array_split(np.arange(args.num_envs), len(groups))
            return np.concatenate([bot(o[idx]) for bot, idx in zip(groups, splits)])
        parts = [net_policy(frozen, device)(o[:n_frozen])]
        if n_pool > 0:
            parts.append(net_policy(pool_agent if pool else frozen, device)(o[n_frozen : n_frozen + n_pool]))
        if n_league > 0:
            parts.append(net_policy(league_agent, device)(o[league_lo : league_lo + n_league]))
        if n_bots > 0:
            idx = np.array_split(np.arange(league_lo + n_league, args.num_envs), len(selfplay_bots))
            parts += [bot(o[ix]) for bot, ix in zip(selfplay_bots, idx)]
        return np.concatenate(parts)

    run_dir.mkdir(parents=True, exist_ok=True)  # OURS
    ep_return = np.zeros(args.num_envs)  # OURS: running episode stats for player 0
    ep_len = np.zeros(args.num_envs, dtype=np.int64)
    ep_stats = []  # (return, length, winner, timed out, opponent group) of games finished since the last log

    # OURS: approach shaping potential. phi = -coef * (distance in units) / 10.
    DXI = 2 * fightcore.OBS_FIGHTER  # obs index of dx (/blast_x); dy (/blast_top) follows

    def phi(o):
        dist = np.hypot(o[:, DXI] * bots.BLAST_X, o[:, DXI + 1] * bots.BLAST_TOP)
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
    start_iteration = 1  # OURS
    if resume is not None:  # OURS: restore everything the next iterations depend on
        agent.load_state_dict(resume["agent"])
        optimizer.load_state_dict(resume["optimizer"])
        frozen.load_state_dict(resume["frozen"])
        pool = resume["pool"]
        generation, gen_start_iter, gate_games, gate_wins = resume["gate"]
        for c, p in zip(league, resume.get("league_p", [])):
            c["p"] = p
        start_iteration = resume["iteration"] + 1
        global_step = resume["global_step"]
        print(f"resumed {run_name} at iteration {start_iteration}")
    start_step = global_step  # OURS: for SPS after a resume
    start_time = time.time()
    envs.reset()  # OURS
    next_obs = torch.Tensor(envs.obs[:, 0].copy()).to(device)  # OURS: learner = player 0
    next_done = torch.zeros(args.num_envs).to(device)

    for iteration in range(start_iteration, args.num_iterations + 1):  # OURS: start_iteration
        # Annealing the rate if instructed to do so.
        if args.anneal_lr:
            frac = 1.0 - (iteration - 1.0) / args.num_iterations
            lrnow = frac * args.learning_rate
            optimizer.param_groups[0]["lr"] = lrnow

        # OURS: shaping scale, 1 -> 0 over the first shaping_anneal of training.
        progress = (iteration - 1.0) / args.num_iterations
        shaping = max(0.0, 1.0 - progress / args.shaping_anneal) if args.shaping_anneal > 0 else 0.0
        shaping = max(shaping, args.shaping_floor) if args.shaping_anneal > 0 else 0.0
        envs.damage_reward = args.damage_reward * shaping
        envs.counter_reward = args.counter_reward * shaping
        ent_now = args.ent_coef_final + (args.ent_coef - args.ent_coef_final) * max(0.0, 1.0 - progress / args.ent_anneal)

        if pool:  # OURS: a fresh pool opponent for this rollout
            pool_agent.load_state_dict(random.choice(pool))
        if league and (iteration - start_iteration) % args.league_switch == 0:  # OURS: next league opponent
            league_cur = pick_league()
            league_agent.load_state_dict(league[league_cur]["weights"])

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
                phi_after = phi(envs.obs[:, 0])  # for finished games this is already the NEXT game,
                phi_after[terminations | truncations] = 0.0  # but an ending has potential 0 anyway
                reward += shaping * (args.gamma * phi_after - phi_before)
            next_done = np.logical_or(terminations, truncations)
            rewards[step] = torch.tensor(reward).to(device).view(-1)
            next_obs, next_done = torch.Tensor(envs.obs[:, 0].copy()).to(device), torch.Tensor(next_done).to(device)

            # OURS: episode stats (replaces gym's RecordEpisodeStatistics)
            ep_return += envs.reward[:, 0]
            ep_len += 1
            for i in np.flatnonzero(terminations | truncations):
                ep_stats.append((ep_return[i], ep_len[i], int(envs.winner[i]), bool(truncations[i]), group_of(i)))
                if i < n_frozen:  # the gate only counts games against the frozen opponent
                    gate_games += 1
                    gate_wins += int(envs.winner[i] == 0)
                elif league_lo <= i < league_lo + n_league:  # credit the league checkpoint being played
                    result = 1.0 if envs.winner[i] == 0 else 0.5 if envs.winner[i] == -1 else 0.0
                    league[league_cur]["p"] += 0.05 * (result - league[league_cur]["p"])
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
                loss = pg_loss - ent_now * entropy_loss + v_loss * args.vf_coef  # OURS: scheduled ent_coef

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
        print("SPS:", int((global_step - start_step) / (time.time() - start_time)))  # OURS: - start_step
        writer.add_scalar("charts/SPS", int((global_step - start_step) / (time.time() - start_time)), global_step)

        # OURS: game stats, action usage, self-play pool, periodic eval.
        if ep_stats:
            r, length, winner, timeout, group = map(np.array, zip(*ep_stats))
            win, draw = winner == 0, winner == -1
            writer.add_scalar("charts/episodic_return", r.mean(), global_step)
            writer.add_scalar("charts/episodic_length", length.mean(), global_step)
            writer.add_scalar("charts/win_rate_train", win.mean(), global_step)
            writer.add_scalar("charts/draw_rate", draw.mean(), global_step)       # stalling alarm
            writer.add_scalar("charts/timeout_rate", timeout.mean(), global_step)  # games reaching 3:00
            for g in np.unique(group):  # win rate per kind of opponent
                writer.add_scalar(f"charts/win_rate_vs_{g}", win[group == g].mean(), global_step)
            print(f"iter {iteration}/{args.num_iterations}  games {len(r)}  return {r.mean():.2f}  "
                  f"win {win.mean():.2f}  draw {draw.mean():.2f}  timeouts {timeout.mean():.2f}  length {length.mean():.0f}")
            ep_stats.clear()
        writer.add_scalar("charts/shaping_scale", shaping, global_step)
        writer.add_scalar("charts/ent_coef", ent_now, global_step)

        # OURS: game mechanics used by the learner (player 0), from the env's
        # counters: rates per minute of game time, plus a few ratios.
        st = dict(zip(fightcore.STAT_NAMES, envs.stats.copy()))
        envs.stats[:] = 0
        minutes = max(st["frames"], 1) / 3600.0
        for name, value in st.items():
            if name in ("max_combo", "max_streak"):
                writer.add_scalar(f"mech/{name}", value, global_step)
            elif name.startswith("frames_"):
                writer.add_scalar(f"mech/share_{name[7:]}", value / max(st["frames"], 1), global_step)
            elif name != "frames":
                writer.add_scalar(f"mech/{name}_per_min", value / minutes, global_step)
        ratio = lambda a, b: a / b if b > 0 else 0.0  # noqa: E731
        writer.add_scalar("mech/combo_hit_share", ratio(st["combo_hits"], st["hits"]), global_step)
        writer.add_scalar("mech/di_rate", ratio(st["di_used"], st["hits_taken"]), global_step)
        writer.add_scalar("mech/tech_rate", ratio(st["techs"], st["techs"] + st["knockdowns"]), global_step)
        writer.add_scalar("mech/short_hop_share", ratio(st["short_hops"], st["short_hops"] + st["full_jumps"]), global_step)
        # Share of hits that were counters / whiff punishes, and how often an evade got punished back.
        writer.add_scalar("mech/counter_hit_share", ratio(st["counters"], st["hits"]), global_step)
        writer.add_scalar("mech/streak_hit_share", ratio(st["streak_hits"], st["hits"]), global_step)
        writer.add_scalar("mech/whiff_punish_share", ratio(st["whiff_punishes"], st["hits"]), global_step)
        writer.add_scalar("mech/evade_conversion", ratio(st["counters"], st["evades"] + st["parries"] + st["blocks"]), global_step)

        # OURS: share of the learner's time in each state (from its own
        # observations: the state one-hot starts at index bots.STATE).
        state_share = obs[:, :, bots.STATE : bots.STATE + len(fightcore.STATE_NAMES)].mean(dim=(0, 1)).cpu().numpy()
        for name, share in zip(fightcore.STATE_NAMES, state_share):
            writer.add_scalar(f"states/{name}", share, global_step)
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
            print(f"  mechanics per min: hits {st['hits'] / minutes:.1f}  combo hits {st['combo_hits'] / minutes:.1f} "
                  f"(max {st['max_combo']:.0f})  blocks {st['blocks'] / minutes:.1f}  grabs {st['grabs'] / minutes:.1f}  "
                  f"techs {st['techs'] / minutes:.1f}  short hops {st['short_hops'] / minutes:.1f}  "
                  f"DI {ratio(st['di_used'], st['hits_taken']):.0%} of hits taken")
            print(f"  defense per min: evades {st['evades'] / minutes:.1f}  parries {st['parries'] / minutes:.1f}  "
                  f"counters {st['counters'] / minutes:.1f}  whiff punishes {st['whiff_punishes'] / minutes:.1f}  "
                  f"punished {st['counters_taken'] / minutes:.1f}  streak hits {ratio(st['streak_hits'], st['hits']):.0%} "
                  f"(max {st['max_streak']:.0f})")
        chosen = ACTIONS[b_actions.long().cpu().numpy()]
        buttons = np.bincount(chosen[:, 2], minlength=5) / args.batch_size
        for name, frac_used in zip(("none", "jump", "light", "heavy", "dodge"), buttons):
            writer.add_scalar(f"actions/{name}", frac_used, global_step)
        for axis, col in (("x", 0), ("y", 1)):  # stick directions (egocentric)
            for name, v in (("neg", -1), ("zero", 0), ("pos", 1)):
                writer.add_scalar(f"actions/stick_{axis}_{name}", (chosen[:, col] == v).mean(), global_step)
        # How many distinct actions are really in use (exp of the action entropy
        # of this batch's choices): 45 = all equally, 1 = always the same one.
        counts = np.bincount(b_actions.long().cpu().numpy(), minlength=len(ACTIONS)) / args.batch_size
        nz = counts[counts > 0]
        writer.add_scalar("actions/effective_count", float(np.exp(-(nz * np.log(nz)).sum())), global_step)

        if args.opponent == "selfplay":  # OURS: the gate
            gate_rate = gate_wins / max(gate_games, 1)
            writer.add_scalar("selfplay/win_rate_vs_frozen", gate_rate, global_step)
            writer.add_scalar("selfplay/generation", generation, global_step)
            writer.add_scalar("selfplay/pool_size", len(pool), global_step)
            for c in league:  # the learner's recent win rate against each league checkpoint
                writer.add_scalar(f"league/win_rate_{c['name']}", c["p"], global_step)
            beaten = gate_games >= args.gate_min_games and gate_rate >= args.gate_win_rate
            if beaten or iteration - gen_start_iter >= args.gate_max_iters:
                pool.append({k: v.detach().cpu().clone() for k, v in frozen.state_dict().items()})
                if len(pool) > args.pool_max:  # keep checkpoints small; random drop keeps old and new
                    pool.pop(random.randrange(len(pool)))
                frozen.load_state_dict(agent.state_dict())
                generation += 1
                print(f"  new opponent: generation {generation} ({'beaten' if beaten else 'timed out'} "
                      f"at {gate_rate:.0%} over {gate_games} games)")
                gen_start_iter, gate_games, gate_wins = iteration, 0, 0

        if iteration % args.eval_every == 0 or iteration == args.num_iterations:
            me = net_policy(agent, device)
            for name in ("dummy", "random", "jabber", "spacer", "fighter"):
                w, l, d = evaluate(me, bots.BOTS[name], args, args.eval_games)
                writer.add_scalar(f"eval/win_vs_{name}", w, global_step)
                print(f"  eval vs {name:7s} win {w:.2f} loss {l:.2f} draw {d:.2f}")
            if league:  # head-to-head against the older run's final agent: the real yardstick
                w, l, d = evaluate(me, net_policy(league_best, device), args, args.eval_games)
                writer.add_scalar("eval/win_vs_league_best", w, global_step)
                print(f"  eval vs league best ({league[-1]['name']}) win {w:.2f} loss {l:.2f} draw {d:.2f}")
                print("  league win rates: " + "  ".join(f"{c['name']} {c['p']:.2f}" for c in league))
            torch.save(agent.state_dict(), run_dir / "agent_latest.pt")
            save_bot(agent.state_dict(), run_dir / "agent_latest.bin")  # OURS: for the game's C++ bot
            torch.save({  # everything --resume needs
                "args": {k: v for k, v in vars(args).items() if k != "resume"},
                "agent": agent.state_dict(), "optimizer": optimizer.state_dict(),
                "frozen": frozen.state_dict(), "pool": pool,
                "gate": (generation, gen_start_iter, gate_games, gate_wins),
                "league_p": [c["p"] for c in league],
                "iteration": iteration, "global_step": global_step,
            }, run_dir / "resume.pt")
            if iteration % args.keep_every == 0:
                torch.save(agent.state_dict(), run_dir / f"agent_{iteration:05d}.pt")
                save_bot(agent.state_dict(), run_dir / f"agent_{iteration:05d}.bin")  # OURS

        if iteration % args.replay_every == 0 or iteration == args.num_iterations:
            # A replay against the training opponent, to watch in Godot (play.bat replay).
            me = net_policy(agent, device)
            opp = net_policy(frozen, device) if args.opponent == "selfplay" else opp_bots[args.opponent][0]
            opp_name = f"generation {generation}" if args.opponent == "selfplay" else opp_bots[args.opponent][0].__name__
            rep = replay.play_and_record(args.moves_dir, me, opp, args.frame_skip, args.reaction_delay,
                                         meta={"run": run_name, "iteration": iteration, "p1": "agent", "p2": opp_name})
            replay.save(rep, ROOT / "replays" / run_name / f"iter_{iteration:05d}.json")

    writer.close()  # OURS: no envs.close() needed
