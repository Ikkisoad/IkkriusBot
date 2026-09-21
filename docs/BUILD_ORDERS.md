# Build Orders

All build orders inherit from the [`BuildOrder`](../visualstudio/src/starterbot/BuildOrder.h) abstract base class and are instantiated as singletons via [`BuildOrderFactory`](../src/starterbot/Factory/BuildOrderFactory.h).

The active build order is selected in `StarterBot::onStart()` and its `Execute()` method is called every game frame.

---

## Build Order Base Class

```cpp
class BuildOrder {
public:
    virtual void Execute() = 0;          // called every frame
    virtual std::string GetName() = 0;   // strategy identifier
    virtual void onStart() {}
    virtual void onEnd(bool isWinner) {}
    // + unit event callbacks ...
};
```

---

## Pool Rush Variants

All pool rushes share the same core idea: build a Spawning Pool at an unusually early supply count to produce Zerglings before the opponent has defenses. Earlier pool = more aggressive, riskier; later pool = safer, more workers at pool time.

### 4-Pool — [`src/starterbot/buildorders/4Pool.h`](../src/starterbot/buildorders/4Pool.h)

**The most aggressive rush.** Builds the Spawning Pool immediately at 4 supply (start of game). Produces Zerglings as fast as possible and attacks with almost no economic development.

- **Risk**: Extremely vulnerable if blocked or defended
- **Reward**: Can kill unprepared opponents before any defenses exist
- **Micro**: Custom `attack()` and `isLethalTo()` logic for maximizing damage

### 5-Pool — [`src/starterbot/buildorders/5Pool.h`](../src/starterbot/buildorders/5Pool.h)

Spawning Pool at 5 supply. Slightly more economic than 4-Pool while remaining very early.

### 6-Pool — [`src/starterbot/buildorders/6Pool.h`](../src/starterbot/buildorders/6Pool.h)

Spawning Pool at 6 supply. A popular competitive timing that hits before most opponents have a Gateway/Barracks/Pool of their own.

### 7-Pool — [`src/starterbot/buildorders/7Pool.h`](../src/starterbot/buildorders/7Pool.h)

Spawning Pool at 7 supply. Allows one extra Drone before committing.

### 8-Pool — [`src/starterbot/buildorders/8Pool.h`](../src/starterbot/buildorders/8Pool.h)

**The most economical rush variant.** Spawning Pool at 8 supply. Features an optional **Extractor trick** (build and cancel an Extractor to temporarily exceed the drone cap and gain an extra drone).

Additional state tracked:
- `builtEightDrones` — ensures 8 Drones before pool
- `builtExtractor` — tracks Extractor trick
- `builtSpawningPool` — prevents duplicate pool builds

---

## Overpool — [`src/starterbot/buildorders/Overpool.h`](../src/starterbot/buildorders/Overpool.h)

**Safe economic opener.** Builds an Overlord first (to 18 supply), then the Spawning Pool. Focuses on Drone production and early expansion before transitioning to Zerglings.

**Execution sequence:**
1. Produce Drones up to supply 18
2. Morph an Overlord if at supply cap
3. Build Spawning Pool when supply is sufficient
4. Post-pool: continue Drone production up to supply 22
5. Produce 6 Zerglings
6. Expand with a second Hatchery at the nearest base

**Micro:** Uses `Micro::BasicAttackAndScoutLoop()` — scouts and attacks with available units.

> [!TIP]
> This is the **currently active** strategy — `StarterBot::onStart()` hardcodes `BuildOrderType::Overpool`. Change the factory call there to switch strategies.

---

## Genetic — [`src/starterbot/buildorders/Genetic.h`](../src/starterbot/buildorders/Genetic.h)

**Adaptive self-tuning strategy.** Instead of a fixed build order, Genetic encodes actions as a sequence of `BuildAction` structs and mutates the sequence between games based on results.

### BuildAction Structure

```cpp
struct BuildAction {
    int supply;                  // supply count that triggers this action
    int frame;                   // frame trigger (optional, -1 if unused)
    ActionType actionType;       // Unit | Building | Upgrade | Tech | MicroMode
    BWAPI::UnitType unitType;
    BWAPI::UpgradeType upgradeType;
    BWAPI::TechType techType;
    Micro::MicroMode microMode;  // only used when actionType == MicroMode
};
```

### ActionType Values

| Type | Description |
|------|-------------|
| `Unit` | Morph/train a unit |
| `Building` | Construct a building |
| `Upgrade` | Research an upgrade |
| `Tech` | Research a tech |
| `MicroMode` | Switch the global micro mode mid-game |

### Lifecycle

| Method | Purpose |
|--------|---------|
| `onStart()` | Loads the build sequence from file (if available) |
| `Execute()` | Steps through `buildOrder` actions in order, triggering each when supply/frame conditions are met |
| `onEnd(bool)` | Calls `AnalyzeBuildOrderResults()` to score the run |
| `MutateBuildOrder(actions)` | Randomly mutates the action list for the next generation |
| `AnalyzeBuildOrderResults()` | Evaluates the game outcome and logs to CSV |
| `LogEvent(event)` | Appends a timestamped event string to `bin/genetic_build_log.txt` |

### Output Files *(gitignored)*

| File | Content |
|------|---------|
| `bin/genetic_build_log.csv` | Per-generation performance data |
| `bin/genetic_build_log.txt` | Human-readable event log |
| `bin/best_build_orders.txt` | Best build sequence found so far |

---

## Strategy Selection

`Stats::readStrategy()` reads `src/starterbot/stats/data/strats-v1.csv` and returns the strategy name with the highest win rate for the current opponent + map combination.

Selection priority:
1. Best win rate against this **specific opponent** on this **map**
2. Best win rate against this **opponent's race** on this **map**
3. `"Default"` if no data exists

> [!NOTE]
> The result of `readStrategy()` is currently not wired to `BuildOrderFactory::Create()`. The call is hardcoded to `Overpool`. To enable adaptive selection, map the returned string to a `BuildOrderType` enum value and pass it to the factory.

---

## Adding a New Build Order

See [`docs/CONTRIBUTING.md`](CONTRIBUTING.md#adding-a-new-build-order) for the full step-by-step guide.
