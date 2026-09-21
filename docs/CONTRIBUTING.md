# Contributing to IkkriusBot

## Table of Contents

- [Adding a New Build Order](#adding-a-new-build-order)
- [Extending Micromanagement](#extending-micromanagement)
- [Working with Statistics](#working-with-statistics)
- [Coding Conventions](#coding-conventions)
- [Project Rules](#project-rules)

---

## Adding a New Build Order

Follow these steps to create a new strategy (e.g. `MutaliskOpener`):

### 1. Create the header file

Create `src/starterbot/buildorders/MutaliskOpener.h`:

```cpp
#pragma once
#include "../../src/starterbot/BuildOrder.h"
#include <BWAPI.h>

class MutaliskOpener : public BuildOrder {
public:
    static MutaliskOpener& Instance();
    void Execute() override;
    void onStart() override;
    void onEnd(bool isWinner) override;
    void OnUnitCreate(BWAPI::Unit unit) override;
    void onUnitComplete(BWAPI::Unit unit) override;
    std::string GetName() const override { return "MutaliskOpener"; }
private:
    MutaliskOpener() = default;
    // track build state with bool flags
    bool builtSpawningPool = false;
    bool builtSpire = false;
};
```

**Rules:**
- Inherit from `BuildOrder`
- Use the **singleton pattern** (`static Instance()` returning a local static)
- Use `bool` flags to track build state — do not use counters that could desync
- Override only the events your strategy needs; the base class provides empty defaults

### 2. Create the implementation file

Create `src/starterbot/buildorders/MutaliskOpener.cpp`:

```cpp
#include "MutaliskOpener.h"
#include "../Tools.h"
#include "../../../visualstudio/BasesTools.h"
#include "../micro.h"
#include "BuildOrderTools.h"

MutaliskOpener& MutaliskOpener::Instance() {
    static MutaliskOpener instance;
    return instance;
}

void MutaliskOpener::onStart() {
    Micro::SetMode(Micro::MicroMode::Neutral);
}

void MutaliskOpener::Execute() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();

    // Example: build pool at 9 supply
    if (!builtSpawningPool) {
        if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0) {
            builtSpawningPool = true;
        } else if (BWAPI::Broodwar->self()->supplyUsed() >= 18) {
            Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spawning_Pool, 1,
                BWAPI::Broodwar->self()->getStartLocation());
        }
    }
    // ... continue build logic

    Micro::BasicAttackAndScoutLoop(myUnits);
}

void MutaliskOpener::onEnd(bool isWinner) {
    // Optional: log results or reset state
}
```

**Rules:**
- Call `Micro::BasicAttackAndScoutLoop(myUnits)` at the end of `Execute()` unless you have fully custom unit control
- Use `Tools::` helpers for building, morphing, and training — never issue BWAPI commands directly from a build order
- Use `Tools::TryBuildBuilding()` with a `limitAmount` of `1` to prevent duplicate buildings

### 3. Register in the factory

Add to `src/starterbot/Factory/BuildOrderFactory.h`:

```cpp
#include "../buildorders/MutaliskOpener.h"
```

Add to the `BuildOrderType` enum:

```cpp
enum class BuildOrderType { FourPool, ..., MutaliskOpener };
```

Add a case to `BuildOrderFactory::Create()`:

```cpp
case BuildOrderType::MutaliskOpener: return &MutaliskOpener::Instance();
```

### 4. Add to Visual Studio project

Open `visualstudio/IkkriusBot.sln`, right-click the `StarterBot` project → **Add → Existing Item**, and add both `MutaliskOpener.h` and `MutaliskOpener.cpp`.

### 5. Select the strategy

In `src/starterbot/StarterBot.cpp`, change the factory call:

```cpp
currentBuildOrder = BuildOrderFactory::Create(BuildOrderType::MutaliskOpener);
```

---

## Extending Micromanagement

All micro routines live in `src/starterbot/micro.h` / `micro.cpp` in the `Micro` namespace.

### Adding a new routine

```cpp
// In micro.h
namespace Micro {
    void SmartHoldPosition(BWAPI::Unit unit);
}

// In micro.cpp
void Micro::SmartHoldPosition(BWAPI::Unit unit) {
    if (unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Hold_Position) return;
    unit->holdPosition();
}
```

**Rules:**
- Check `unit->getLastCommand()` before issuing a repeat command — BWAPI penalizes redundant commands
- Never call micro functions on workers unless the function name explicitly states it handles workers
- Keep routines stateless where possible; use `MicroMode` for global state

### Switching micro mode mid-game

`Micro::SetMode(Micro::MicroMode::Aggressive)` affects the behaviour of all routines that query `Micro::GetMode()`. Build orders can switch mode at any point (e.g. after a certain supply count or on a unit event).

---

## Working with Statistics

Win-rate data is stored in `src/starterbot/stats/data/strats-v1.csv`:

```
opponent,opponentRace,mapHash,strategy,games,wins
```

This file is **gitignored** (it's generated at runtime). Seed it manually to bootstrap strategy selection:

```
SomeBot,Zerg,a1b2c3d4,Overpool,5,3
SomeBot,Zerg,a1b2c3d4,EightPool,3,1
```

The `mapHash` is the BWAPI map hash string (`BWAPI::Broodwar->mapHash()`).

### Wiring adaptive strategy selection

Currently `StarterBot::onStart()` hardcodes `BuildOrderType::Overpool`. To enable data-driven selection:

```cpp
// In StarterBot.cpp, onStart():
auto strategyName = Stats::readStrategy(opponentName, opponentRace, BWAPI::Broodwar->mapHash());

// Map strategy name to enum:
static const std::unordered_map<std::string, BuildOrderType> stratMap = {
    {"FourPool",    BuildOrderType::FourPool},
    {"Overpool",    BuildOrderType::Overpool},
    // ... add all strategies
};
auto it = stratMap.find(strategyName);
BuildOrderType selected = (it != stratMap.end()) ? it->second : BuildOrderType::Overpool;
currentBuildOrder = BuildOrderFactory::Create(selected);
```

---

## Coding Conventions

| Topic | Convention |
|-------|-----------|
| **Naming** | `PascalCase` for classes and methods, `camelCase` for local variables, `m_camelCase` for private members |
| **Headers** | Use `#pragma once` (not include guards) |
| **Singletons** | All `BuildOrder` subclasses use `static T& Instance()` with a local static |
| **BWAPI access** | Always go through `BWAPI::Broodwar->` — never cache the pointer |
| **Build state** | Track build progress with `bool` flags, not frame counters |
| **Includes** | Build orders include `../Tools.h`, `../micro.h`, `../../../visualstudio/BasesTools.h` — use these exact relative paths |
| **Debug output** | Use `BWAPI::Broodwar->printf(...)` for in-game messages, `std::cout` for terminal output |
| **No queuing** | Never use BWAPI's unit queue system — train units one at a time to save resources |

---

## Project Rules

See [`RULES.md`](../RULES.md) for the condensed quick-reference rule set.
