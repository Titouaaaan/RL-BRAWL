# Documentation

| I want to... | Read |
|---|---|
| Play the game, learn the controls and mechanics, fight a bot, watch replays | [playing.md](playing.md) |
| Install everything on a new PC, find a command, fix an error | [setup.md](setup.md) |
| Add characters, draw sprites and animations | [assets.md](assets.md) |
| Understand the C++ game engine (the simulation), tune moves | [engine.md](engine.md) |
| Understand the Godot side (drawing, input, replays, the live bot) | [godot.md](godot.md) |
| Use the game from Python, the bots, tools and tests | [python.md](python.md) |
| Train agents, read the training charts | [training.md](training.md) |
| Know why things are the way they are (plan and dated decisions) | [design.md](design.md) |
| See every change: when, why, who decided, which commit | [log.md](log.md) |

[runs.md](runs.md) is the lab notebook of every
training run.

## The project in one picture

```
                     data/moves/*.json      assets/characters/
                              |                    |
                     +--------v---------+          |
                     |  core/  (C++)    |  the game: one struct + one step()
                     +---+----------+---+          |
        bindings/python/ |          | bindings/godot/
          (nanobind)     |          | (GDExtension)
                +--------v--+    +--v-----------v--+
                | fightcore |    | Godot: main.gd  |<-- you (keyboard / controller)
                |  (Python) |    |  draws, replays |
                +----+------+    +--^----------^---+
   train/ppo.py      |              |          |
   train/bots.py ----+--- replays --+          +--- train/serve.py (scripted live bots)
   tools/, tests/
```

The game exists once, in C++. Training (Python) and the playable game (Godot)
both drive the same code, so what you watch is exactly what the agents learned on.
