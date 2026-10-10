# RL-BRAWL

A small Smash-style fighting game (Smash Ultimate's mechanics and Mario's numbers, on a Battlefield-like stage) and the reinforcement-learning agents that learn to play it.

- **The game:** one deterministic C++ simulation (`core/`), drawn and played in Godot (`godot/`), with a menu, characters, replays, keyboard and controller support.
- **The agents:** PPO self-play in PyTorch (`train/`) on millions of games per hour through a batched C++ environment, with a league of past checkpoints; you can fight them live in the game.

Start with [`docs/README.md`](docs/README.md): playing, setup, assets, the engine, Godot, Python and training each have their own page.

```
.\play.bat                                     # build and open the game (menu)
python -u train/ppo.py --opponent selfplay     # train agents
```
