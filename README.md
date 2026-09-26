# IkkriusBot

A **StarCraft: Brood War** Zerg AI bot built with [BWAPI 4.4.0](https://bwapi.github.io/). IkkriusBot plays aggressive Zerg early-game strategies against any opponent, tracks match statistics per opponent/map/strategy, and includes a self-tuning Genetic build-order engine.

> Forked from [STARTcraft](https://github.com/davechurchill/STARTcraft) — the BWAPI C++ starter template.

---

## Features

- **Adaptive compositions** — nine Zerg army compositions that switch mid-match on scouted enemy tech, with step timings evolved by a genetic algorithm and composition choice learned by reinforcement
- **Multiple build orders** — 4-Pool, 5-Pool, 6-Pool, 7-Pool, 8-Pool, Overpool, HiveTech, MutaHive, and Genetic
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

Download and unzip [Starcraft Broodwar 1.16.1 + BWAPI 4.4.0](https://davechurchill.ca/starcraft/files/startcraft/scbw_bwapi440.zip) into the `starcraft/` folder at the repo root.

### 2. Build the bot

**Windows (Visual Studio 2022)**

Open `visualstudio/IkkriusBot.sln` and build the `StarterBot` project in **Release | Win32**. The launcher uses the resulting `bin/IkkriusBot.exe`. Debug builds produce `bin/IkkriusBot_d.exe`.

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

This launches `IkkriusBot.exe` and then starts StarCraft with BWAPI injected. No Chaoslauncher required. The launcher works from any working directory and checks the required files before starting either program.

If it reports missing files, extract the **StarCraft + BWAPI 4.4.0 bundle** linked above directly into `starcraft/`. A plain StarCraft 1.16.1 installation is not enough: `RunStarcraftWithBWAPI.bat`, `injectory_x86.exe`, `WMode.dll`, and `bwapi-data/` must be present alongside `StarCraft.exe`.

---

### Automatic matches

BWAPI handles map selection and match startup. In `starcraft/bwapi-data/bwapi.ini`, set the existing keys under `[auto_menu]` to:

```ini
auto_menu = SINGLE_PLAYER
race = Zerg
map = maps/BroodWar/aiide/(?)*.sc?
mapiteration = RANDOM
enemy_count = 1
enemy_race = Random
auto_restart = ON
```

This starts a random bundled AIIDE map as Zerg against one computer opponent and starts another match when the game ends. Restart StarCraft after changing these settings. The downloaded bundle defaults to `auto_menu = OFF`, so configure this after a fresh extraction; the local `starcraft/` folder is gitignored.

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

## Match diagnostics and pressure build

The ground HiveTech strategy fields Hydras before Lair, researches Lurkers after
establishing its ranged army, and delays Hive until the economy can support it.
It adds gas at developed bases, Sunken defenses, Hydra range/speed and ground upgrades.
The army attacks once formed, returns to defend threatened bases, and regroups after
heavy losses. Match results are still needed to tune these heuristics.

Every match automatically writes `bin/logs/match-*.jsonl`, including when running
`bin/updated/IkkriusBot.exe`. On Windows the path is independent of the working directory.
Logs identify the build revision, map and opponent. Snapshots every 120 game frames
record resources and reserves, supply, workers, larvae, army, visible enemies, phase
and decision. Events record unit losses, production outcomes, attacks/regroups and
win/loss. Identical command outcomes are sampled once per 120 frames, so counts are
samples rather than exact failed-command totals. Records flush immediately; interrupted
matches are distinguished from losses.

From the repository root, summarize one match or the entire log directory:

```powershell
python tools/analyze_match.py
python tools/analyze_match.py bin/logs/match-<id>.jsonl
```

The summary includes floating resources, estimated supply-block duration, losses,
production errors and the last state. Raw supply values use BWAPI's doubled units;
summary army supply uses normal game units. Logs remain local and are gitignored.
Delete old logs manually when no longer needed.

The normal launcher installs `bin/updated/IkkriusBot.exe` if it is newer than the
installed executable. Close the old bot before restarting the launcher. An already
running bot cannot acquire the new code or logging.

Run focused checks in a Visual Studio developer PowerShell:

```powershell
python tests/production_regression.py
python tests/defense_regression.py
python tests/micro_regression.py
python tests/logging_regression.py
python tests/queen_air_regression.py
```

The build also adds macro Hatcheries when larvae limit production and lowers gas staffing
when gas is oversupplied. Resources committed to a walking builder are reserved centrally,
so unit production cannot consume a queued building's construction cost.
For a bounded automated run, `IkkriusBot.exe --once` exits after one completed match.

Live validation on September 24, 2026 found a queued-building resource bug in an initial
loss on Python. After the reservation fix, the bot won a test against a Terran computer
on Heartbreak Ridge. This is evidence of a working build, not a general win-rate claim;
continue comparing the per-match logs across opponents and maps.

## Queen support and air strategy

MutaHive and HiveTech are available through `IKKRIUS_STRATEGY` (normal matches use
[Adaptive](#adaptive-compositions-and-learning)). The strategies retain separate statistics keys. MutaHive takes the direct Lair/Spire
path, prioritizes Mutalisks, adds Queens, then commits to Hive at eight Mutalisks and
24 Drones on at least two Hatcheries. Greater Spire morphs produce Guardians for ground
siege and Devourers for air cover. The morph policy preserves at least eight completed
Mutalisks, normally fields one Devourer escort, and increases Devourers for visible
combat aircraft. Idle, healthy Mutalisks morph one at a time, with resources reserved
before spending on more production. Air attack and armor upgrades are included.

HiveTech groups nearby Hydras into squads of up to 12 and requests one Queen per squad.
It replaces lost Queens and holds supply for missing support. A detached Hydra group
also requests its own Queen. Queens catch up behind their assigned groups and cast
Ensnare on clusters of at least three unensnared combat units, or Spawn Broodlings on
valuable valid ground targets. Nearby Queens avoid duplicate casts. Parasite uses only
surplus energy. Support is a production target: groups can temporarily lack a Queen
while rebuilding losses or waiting for tech/resources.

Lurkers follow nearby Hydra groups, burrow to engage ground targets, hold deployment
through short gaps in contact and weapon cooldowns, then unburrow to rejoin the advance.
The Hive army scales Lurker production with Hydra count, up to 12.

To force either strategy for a reproducible test, launch from PowerShell in the repo:

```powershell
$env:IKKRIUS_STRATEGY = 'MutaHive' # or 'HiveTech'
.\bin\RunStarterBotAndStarcraft.bat
```

Remove that environment variable to return to the Adaptive build. The log summary now
includes accepted Queen spells, Hydra-group support, Lurker deployments and air morphs.
The `queen-air-v4` revision identifies this behavior; prior ground-build wins do not
validate the new air strategy.

## Adaptive compositions and learning

`Adaptive` is the default build. It can play nine compositions and switch between them
during a match:

| # | Composition | # | Composition |
|---|-------------|---|-------------|
| 1 | `ZerglingQueenRush` | 6 | `LingMutaQueen` |
| 2 | `HydraQueenRush` | 7 | `LingMutaGuardian` |
| 3 | `MutaQueenRush` | 8 | `LurkerQueenMuta` |
| 4 | `GuardianRush` | 9 | `LingQueenUltra` |
| 5 | `MassMutaDevourer` | | |

The bot remembers every enemy unit it has seen. It turns them into a tech profile: air,
anti-air, splash, air splash, heavy ground units, small units, static defense, capital
ships, detection, and game phase. Every 10 seconds it re-scores the compositions. It
switches only when another composition beats the current one by the learned margin and
the learned cooldown has passed. Tech buildings already owned make a switch cheaper, so
Ling/Muta/Queen tends to move to Ling/Muta/Guardian rather than start over. Tech and
units already built are kept after a switch.

Two learners decide when and what to build:

- **Genetic algorithm (step timing).** A genome of 27 genes sets when each step
  happens: pool/gas/expansion drone counts, Lair/Den/Spire/Queen's Nest/Hive/Greater
  Spire/Ultralisk Cavern timings, the Lurker research trigger, the rush drone cap,
  drone saturation, the army-to-drone ratio, Queen count, attack and retreat thresholds,
  upgrade timing, switching margin and cooldown, and how much to trust the counter
  table. Each enemy race has a population of 10 genomes. Each genome plays 2 matches.
  The best 3 then survive, and the rest are rebuilt by tournament selection, uniform
  crossover, and Gaussian mutation.
- **Reinforcement learning (composition choice).** A contextual bandit stores a value
  for each (race + enemy tech signature, composition) pair. At the end of a match, each
  composition that was used receives the match reward, weighted by how long it was
  active. The opening choice explores with UCB and a 10% random pick. Mid-match choices
  use the learned values plus the hand-written counter table.

The reward is mostly win or loss, adjusted by kill/loss score and survival time. Learning
state is saved to `bin/learning/adaptive-<race>.txt`, which is gitignored. Delete that
file to reset learning. Useful controls:

```powershell
$env:IKKRIUS_COMP = 'LingMutaGuardian' # lock a composition (name or 1-9)
$env:IKKRIUS_LEARNING = '0'            # play the best known genome, no exploration or saving
```

In game, type `comp` to show the current plan, `comp <name|1-9>` to lock a composition,
and `comp auto` to hand control back to the learners. The overlay shows the composition,
the current step, the genome generation and index, and the number of switches. Match logs
record `genome`, `composition`, and `learning` events.

### Surplus economy and pressure waves

HiveTech and MutaHive count mining sites separately from macro Hatcheries. With no base threats, at least 24 Drones, 16 army supply, and 1,200 minerals after reservations, they can expand beyond the normal four-site limit, one construction order at a time. Visible enemy attackers near a candidate expansion make that site ineligible.

At 195+ supply with a 200-supply capacity, 1,500 unreserved minerals, available larvae and working production, the bot can commit up to 20 supply (at most a quarter of the army) to a sustained attack. Zerglings come first, then Hydras and surplus Mutalisks. Replacement costs must fit the mineral and gas budgets; eight Mutalisks and all Queens, Lurkers, Guardians, Devourers, workers and Overlords are excluded. Selected attackers keep fighting instead of retreating or waiting for the army to regroup. Queens retain their spell and escort behavior.

Waves stop for base threats, low supply, insufficient replacement funds, lost production, or depletion of the selected attackers. New replacements are not automatically added to a running wave. Match logs record `surplus_expansion`, `pressure_start`, and `pressure_end`; `tools/analyze_match.py` summarizes them. Revision: `surplus-pressure-v5`.
