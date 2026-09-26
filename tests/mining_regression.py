"""Keep drones mining at our own bases instead of long-distance mining.

Run from a VS 2022 developer prompt: python tests/mining_regression.py
The fake BWAPI world exercises the real mineral-selection helpers without StarCraft.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / 'src/starterbot/Tools.cpp').read_text()

def block(signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

source = r'''
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <vector>
namespace BWAPI {
struct UnitType {
    int id = 0;
    bool isWorker() const { return id == 1; }
    bool isResourceDepot() const { return id == 2 || id == 3; }
    bool isMineralField() const { return id == 4; }
    bool operator==(UnitType other) const { return id == other.id; }
    bool operator!=(UnitType other) const { return id != other.id; }
};
namespace UnitTypes { const UnitType Zerg_Drone{1}, Zerg_Hatchery{2}, Zerg_Lair{3}, Mineral_Field{4}; }
struct FakeUnit;
using Unit = FakeUnit*;
using Unitset = std::set<Unit>;
struct Command { Unit target = nullptr; Unit getTarget() const { return target; } };
struct FakeUnit {
    UnitType type; int x = 0; int resources = 1500;
    bool completed = true, morphing = false, gathering = false, carrying = false, idle = false;
    Unit orderTarget = nullptr; Command last; int lastFrame = -1;
    UnitType getType() const { return type; }
    bool exists() const { return true; }
    bool isCompleted() const { return completed; }
    bool isMorphing() const { return morphing; }
    bool isIdle() const { return idle; }
    bool isGatheringMinerals() const { return gathering; }
    bool isCarryingMinerals() const { return carrying; }
    int getResources() const { return resources; }
    int getDistance(Unit other) const { return std::abs(x - other->x); }
    Unit getOrderTarget() const { return orderTarget; }
    Command getLastCommand() const { return last; }
    int getLastCommandFrame() const { return lastFrame; }
    bool gather(Unit target);
    bool stop() { orderTarget = nullptr; last.target = nullptr; gathering = false; idle = true; return true; }
};
struct Player { Unitset units; Unitset getUnits() const { return units; } };
struct Game {
    Player player; Unitset minerals; int frame = 12;
    Player* self() { return &player; }
    Unitset getMinerals() const { return minerals; }
    int getFrameCount() const { return frame; }
} game, *Broodwar = &game;
bool FakeUnit::gather(Unit target) {
    orderTarget = target; last.target = target; lastFrame = Broodwar->getFrameCount(); gathering = true; idle = false;
    return true;
}
}
namespace Tools {
BWAPI::Unit GetClosestUnitTo(BWAPI::Unit unit, const BWAPI::Unitset& units) {
    BWAPI::Unit best = nullptr;
    for (auto u : units) if (!best || unit->getDistance(u) < unit->getDistance(best)) best = u;
    return best;
}
bool HasPendingConstruction(BWAPI::Unit) { return false; }
BWAPI::Unit GetMineralForWorker(BWAPI::Unit worker);
bool GatherNearestBaseMinerals(BWAPI::Unit worker);
void FixLongDistanceMining();
}
using namespace Tools;
''' + block('namespace {') + '\n' + block('BWAPI::Unit Tools::GetMineralForWorker') + '\n' + \
    block('bool Tools::GatherNearestBaseMinerals') + '\n' + block('void Tools::FixLongDistanceMining') + r'''
using namespace BWAPI;
FakeUnit* mineral(int x) { auto m = new FakeUnit{UnitTypes::Mineral_Field, x}; game.minerals.insert(m); return m; }
FakeUnit* own(UnitType type, int x) { auto u = new FakeUnit{type, x}; game.player.units.insert(u); return u; }
int main() {
    auto main = own(UnitTypes::Zerg_Hatchery, 0);
    auto mainPatch = mineral(200);
    auto farPatch = mineral(5000); // Enemy natural or an unclaimed expansion.
    // A drone that went idle near a distant base must walk home instead of mining there.
    auto stray = own(UnitTypes::Zerg_Drone, 4900);
    stray->idle = true;
    assert(GetMineralForWorker(stray) == mainPatch);
    assert(GatherNearestBaseMinerals(stray) && stray->orderTarget == mainPatch);

    // A miner already long-distance mining gets pulled back once its cargo is returned.
    auto longDistance = own(UnitTypes::Zerg_Drone, 2500);
    longDistance->gathering = true; longDistance->orderTarget = farPatch; longDistance->last.target = farPatch;
    longDistance->carrying = true;
    FixLongDistanceMining();
    assert(longDistance->orderTarget == farPatch); // Keep the cargo trip.
    longDistance->carrying = false;
    FixLongDistanceMining();
    assert(longDistance->orderTarget == mainPatch);

    // Oversaturated base sends new drones to the other base, not to a far patch.
    auto natural = own(UnitTypes::Zerg_Hatchery, 1500);
    auto naturalPatch = mineral(1700);
    for (int i = 0; i < 2; ++i) {
        auto miner = own(UnitTypes::Zerg_Drone, 100);
        miner->gathering = true; miner->orderTarget = mainPatch; miner->last.target = mainPatch;
    }
    auto fresh = own(UnitTypes::Zerg_Drone, 50);
    assert(GetMineralForWorker(fresh) == naturalPatch);

    // A Hatchery morphing into Lair still counts as a base.
    natural->type = UnitTypes::Zerg_Lair; natural->completed = false; natural->morphing = true;
    assert(GetMineralForWorker(fresh) == naturalPatch);

    // Losing the natural pulls its miners to the main while it has room...
    game.player.units.erase(natural);
    int onMain = 0; // Leave room for exactly one more miner at the main.
    for (auto unit : game.player.units) if (unit->orderTarget == mainPatch && ++onMain > 1) unit->stop();
    auto orphan = own(UnitTypes::Zerg_Drone, 1600);
    orphan->gathering = true; orphan->orderTarget = naturalPatch; orphan->last.target = naturalPatch;
    FixLongDistanceMining();
    assert(orphan->orderTarget == mainPatch);

    // ...but never oversaturates: with two miners on every patch a spare drone stays free to build.
    assert(GetMineralForWorker(fresh) == nullptr);
    assert(!GatherNearestBaseMinerals(fresh));
    auto stranded = own(UnitTypes::Zerg_Drone, 1600);
    stranded->gathering = true; stranded->orderTarget = naturalPatch; stranded->last.target = naturalPatch;
    FixLongDistanceMining();
    assert(stranded->orderTarget == nullptr && stranded->idle); // Stop long-distance mining; wait as a builder.
    (void)main;
    std::cout << "Mining regressions passed.\n";
}
'''

with tempfile.TemporaryDirectory(prefix='ikkrius-mining-') as directory:
    directory = Path(directory)
    (directory / 'mining.cpp').write_text(source)
    subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++20', 'mining.cpp', '/Fe:mining.exe'], cwd=directory, check=True)
    subprocess.run([str(directory / 'mining.exe')], check=True)
