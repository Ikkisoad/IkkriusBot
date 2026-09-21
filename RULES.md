# IkkriusBot — Project Rules

Quick-reference rules for development on this codebase, including AI-assisted work.

---

## Architecture Rules

1. **Never bypass the `BuildOrder` interface.** All game logic per-strategy must live inside a `BuildOrder` subclass. `StarterBot` only orchestrates; it does not contain strategy logic.

2. **All `BuildOrder` subclasses must be singletons.** Use the pattern:
   ```cpp
   static MyOrder& Instance() { static MyOrder instance; return instance; }
   ```

3. **All unit commands go through `Tools::` or `Micro::`.** Never call `unit->attack()`, `unit->train()`, `unit->morph()`, etc. directly from a build order. Use the helper namespaces.

4. **`BasesTools` is the authority on base positions.** Do not maintain separate lists of base or expansion positions. Always call `BasesTools::GetNextExpansionPosition()`, `GetMainBasePosition()`, etc.

5. **`BWEM` is accessed only through `BasesTools`.** Do not call BWEM APIs directly from build orders or micro.

---

## Build Order Rules

6. **Track build state with `bool` flags.** Avoid frame-based state machines that can desync after lag spikes. Use simple boolean flags like `builtSpawningPool`, `builtExtractor`.

7. **Call `Tools::TryBuildBuilding()` with `limitAmount = 1`.** Never issue duplicate build commands; the `limitAmount` parameter prevents this.

8. **Call `Micro::BasicAttackAndScoutLoop()` at the end of `Execute()`** unless the build order has fully custom unit control for every unit type.

9. **Never use the BWAPI unit queue.** Train units one at a time. Queuing wastes resources.

10. **Each build order must implement `GetName()`** returning a unique string that exactly matches the key used in the stats CSV.

---

## Micro Rules

11. **Check `unit->getLastCommand()` before repeating a command.** BWAPI penalizes issuing the same command every frame. Always guard with a last-command check.

12. **Never micro workers** in routines not explicitly named for it (e.g. `GatherMinerals`). Filter workers out with `unit->getType().isWorker()`.

13. **`MicroMode` is global state.** Only set it in `onStart()` or in response to specific in-game events. Do not toggle it every frame.

---

## Stats Rules

14. **Stats files are gitignored.** `src/starterbot/stats/data/*.csv` is never committed. Seed the file locally if needed for testing.

15. **The stats key is `opponent + race + mapHash + strategyName`.** Any change to `GetName()` in a build order breaks existing CSV rows for that strategy.

---

## Visual Studio / Build Rules

16. **The project file is `visualstudio/IkkriusBot.sln`.** Always build and run from here on Windows. Do not use the Linux Makefile on Windows.

17. **Shared source headers live in `visualstudio/`.** `BasesTools.h/.cpp` and `BuildOrder.h` are in `visualstudio/` — include them with their established relative paths (`../../../visualstudio/BasesTools.h`, etc.). Do not move them.

18. **When adding a new `.cpp` file**, add it to `visualstudio/StarterBot.vcxproj` as well as the source directory. The Linux Makefile uses `find` and picks it up automatically.

---

## General Code Style

19. **`PascalCase`** for classes and public methods. **`camelCase`** for local variables. **`m_camelCase`** for private members.

20. **Use `#pragma once`**, not `#ifndef` include guards.

21. **Use `std::cout` for terminal output, `BWAPI::Broodwar->printf()` for in-game overlay messages.**

22. **Leave existing comments intact** unless the code they describe has changed.
