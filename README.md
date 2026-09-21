# IkkriusBot

A **StarCraft: Brood War** Zerg AI bot built with [BWAPI 4.4.0](https://bwapi.github.io/). IkkriusBot plays aggressive Zerg early-game strategies against any opponent, tracks match statistics per opponent/map/strategy, and includes a self-tuning Genetic build-order engine.

> Forked from [STARTcraft](https://github.com/davechurchill/STARTcraft) — the BWAPI C++ starter template.

---

## Features

- **Multiple build orders** — 4-Pool, 5-Pool, 6-Pool, 7-Pool, 8-Pool, Overpool, and Genetic (adaptive)
- **Micromanagement system** — Aggressive, Defensive, and Neutral modes with smart attack, kite, flee, and scouting routines
- **Base tracking** — BWEM-powered map analysis, expansion finding, and enemy base detection
- **Match statistics** — Win-rate logging per opponent × race × map × strategy, used to pick the best strategy next game
- **Replay parser** — Basic replay analysis mode
- **Debug overlay** — Toggleable on-screen display of unit health, commands, bounding boxes, and map data

---

## Requirements

- StarCraft: Brood War **1.16.1**
- **Windows**: Visual Studio 2022 (for building)
- **Linux**: `mingw-w64` + `wine` (cross-compiles a Windows `.exe` run under Wine)

---

## Setup

### 1. Get the StarCraft files

Download and unzip [Starcraft Broodwar 1.16.1 + BWAPI 4.4.0](https://davechurchill.ca/starcraft/resources/) into the `starcraft/` folder at the repo root.

### 2. Build the bot

**Windows (Visual Studio 2022)**

Open `visualstudio/IkkriusBot.sln` and build the `StarterBot` project (Release or Debug). The output `.exe` is placed in `bin/`.

**Linux (cross-compile)**

```bash
sudo apt install build-essential mingw-w64
sudo dpkg --add-architecture i386 && sudo apt update && sudo apt install wine
make
```

### 3. Run

```bat
bin\RunStarterBotAndStarcraft.bat
```

This launches `StarterBot.exe` and then starts StarCraft with BWAPI injected. No Chaoslauncher required.

---

## In-Game Controls

These text commands can be typed in-game while the bot is running (requires `UserInput` flag, enabled by default):

| Key | Action |
|-----|--------|
| `m` | Toggle map debug overlay |
| `a` | Force all combat units to attack |
| `s` | Switch micro mode to Neutral |
| `1` | Set game speed to maximum (speed 0) |
| `2` | Set game speed to fast (speed 8) |
| `3` | Set game speed to normal (speed 32) |
| `4` | Set game speed to slow (speed 128) |

---

## Project Structure

```
IkkriusBot/
├── bin/                        # Compiled executables and launch scripts
├── bin_linux/                  # Linux cross-compile output
├── BWEM/                       # BWEM library (map analysis)
├── src/
│   ├── bwapi/                  # BWAPI headers and client libraries
│   └── starterbot/
│       ├── main.cpp            # Entry point — BWAPI event loop
│       ├── StarterBot.h/.cpp   # Top-level bot class
│       ├── Tools.h/.cpp        # General BWAPI utility helpers
│       ├── MapTools.h/.cpp     # Tile walkability / buildability grids
│       ├── micro.h/.cpp        # Micromanagement routines
│       ├── Units.h/.cpp        # Unit tracking helpers
│       ├── ReplayParser.h/.cpp # Replay mode handler
│       ├── Grid.hpp            # Generic 2D grid template
│       ├── Factory/
│       │   └── BuildOrderFactory.h  # Factory that instantiates build orders
│       ├── buildorders/        # Concrete build order implementations
│       │   ├── 4Pool – 8Pool   # Zergling rush variants
│       │   ├── Overpool        # Safe expand-into-pool opener
│       │   ├── Genetic         # Adaptive/self-tuning build order
│       │   └── BuildOrderTools # Shared helpers for build orders
│       └── stats/              # Win-rate statistics
│           ├── stats.h/.cpp
│           └── data/           # Runtime CSV data (gitignored)
├── visualstudio/               # Visual Studio project files + shared source
│   ├── IkkriusBot.sln
│   ├── BasesTools.h/.cpp       # Base position tracking (BWEM integration)
│   └── src/starterbot/
│       └── BuildOrder.h        # Abstract base class for all build orders
└── starcraft/                  # StarCraft game files (gitignored)
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for a full module dependency diagram.

---

## Adding a New Build Order

See [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) for a step-by-step guide.

---

## Documentation

| Document | Description |
|----------|-------------|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | System architecture and module dependency diagram |
| [`docs/BUILD_ORDERS.md`](docs/BUILD_ORDERS.md) | Detailed breakdown of every build order strategy |
| [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) | How to add features, coding conventions, and project rules |
| [`RULES.md`](RULES.md) | Quick-reference rules for AI-assisted development |

---

## License

MIT — see [`LICENSE`](LICENSE).
