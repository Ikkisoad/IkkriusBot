#include "Tools.h"
#include "MatchLog.h"
#include <BWAPI.h>
#include <vector>
#include <algorithm>
#include <limits>
#include <map>
#include <sstream> // Include necessary header for stringstream
#include "micro.h"

BWAPI::Unit Tools::GetClosestUnitTo(BWAPI::Position p, const BWAPI::Unitset& units)
{
    BWAPI::Unit closestUnit = nullptr;

    for (auto& u : units)
    {
        if (!closestUnit || u->getDistance(p) < closestUnit->getDistance(p))
        {
            closestUnit = u;
        }
    }

    return closestUnit;
}

BWAPI::Unit Tools::GetClosestUnitTo(BWAPI::Unit unit, const BWAPI::Unitset& units)
{
    if (!unit) { return nullptr; }
    return GetClosestUnitTo(unit->getPosition(), units);
}

int Tools::CountUnitsOfType(BWAPI::UnitType type, const BWAPI::Unitset& units, const bool inProgress)
{
    int sum = 0;
    for (auto unit : units) {
        if (unit->getType() == type) {
            if (inProgress || unit->isCompleted()) ++sum;
        } else if (inProgress) {
            const auto command = unit->getLastCommand();
            const bool pending = unit->getBuildType() == type ||
                ((command.getType() == BWAPI::UnitCommandTypes::Build &&
                  (!unit->isIdle() || BWAPI::Broodwar->getFrameCount() - unit->getLastCommandFrame() <= BWAPI::Broodwar->getLatencyFrames())) ||
                 (command.getType() == BWAPI::UnitCommandTypes::Morph &&
                  BWAPI::Broodwar->getFrameCount() - unit->getLastCommandFrame() <= BWAPI::Broodwar->getLatencyFrames())) &&
                command.getUnitType() == type;
            if (pending) sum += type.isTwoUnitsInOneEgg() ? 2 : 1;
        }
    }

    return sum;
}

BWAPI::Unit Tools::GetUnitOfType(BWAPI::UnitType type)
{
    // For each unit that we own
    for (auto& unit : BWAPI::Broodwar->self()->getUnits())
    {
        // if the unit is of the correct type, and it actually has been constructed, return it
        if (unit->getType() == type && unit->isCompleted())
        {
            return unit;
        }
    }

    // If we didn't find a valid unit to return, make sure we return nullptr
    return nullptr;
}

int Tools::CountUnitOfType(BWAPI::UnitType type)
{
	int count = 0;
    // For each unit that we own
    for (auto& unit : BWAPI::Broodwar->self()->getUnits())
    {
        // if the unit is of the correct type, and it actually has been constructed, return it
        if (unit->getType() == type && unit->isCompleted())
        {
			count++;
        }
    }

    // If we didn't find a valid unit to return, make sure we return nullptr
    return count;
}

void Tools::Scout(BWAPI::Unit scout) {
    if (!scout) return;
    for (auto tile : BWAPI::Broodwar->getStartLocations()) {
        if (!BWAPI::Broodwar->isExplored(tile)) {
            BWAPI::Position pos(tile);
            auto command = scout->getLastCommand();
            if (command.getTargetPosition() == pos) return;
            scout->move(pos);
            return;
        }
    }
}

namespace {
constexpr int MineralBaseRadius = 320;

bool IsUsableDepot(BWAPI::Unit unit) {
    // A Hatchery morphing into Lair/Hive still accepts returned cargo.
    return unit && unit->exists() && unit->getType().isResourceDepot() &&
        (unit->isCompleted() || (unit->isMorphing() && unit->getType() != BWAPI::UnitTypes::Zerg_Hatchery));
}

BWAPI::Unitset UsableDepots() {
    BWAPI::Unitset depots;
    for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
        if (IsUsableDepot(unit)) depots.insert(unit);
    }
    return depots;
}

BWAPI::Unit MiningTarget(BWAPI::Unit worker) {
    const auto orderTarget = worker->getOrderTarget();
    if (orderTarget && orderTarget->getType().isMineralField()) return orderTarget;
    const auto commandTarget = worker->getLastCommand().getTarget();
    if (commandTarget && commandTarget->getType().isMineralField()) return commandTarget;
    return nullptr;
}

bool IsNearDepot(BWAPI::Unit mineral, const BWAPI::Unitset& depots) {
    for (auto depot : depots) {
        if (mineral->getDistance(depot) < MineralBaseRadius) return true;
    }
    return false;
}

constexpr int WorkersPerPatch = 2; // A third miner on a patch only waits in line.
}

BWAPI::Unit Tools::GetMineralForWorker(BWAPI::Unit worker) {
    // Only mine patches at one of our bases so return trips stay short.
    if (!worker) return nullptr;
    const auto depots = UsableDepots();
    std::map<BWAPI::Unit, int> miners;
    for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
        if (unit == worker || !unit->getType().isWorker()) continue;
        if (const auto target = MiningTarget(unit)) ++miners[target];
    }

    BWAPI::Unit bestDepot = nullptr;
    BWAPI::Unitset bestMinerals;
    bool bestSaturated = true;
    int bestDistance = std::numeric_limits<int>::max();
    for (auto depot : depots) {
        BWAPI::Unitset minerals;
        int assigned = 0;
        for (auto mineral : BWAPI::Broodwar->getMinerals()) {
            if (mineral->getResources() <= 0 || mineral->getDistance(depot) >= MineralBaseRadius) continue;
            minerals.insert(mineral);
            assigned += miners[mineral];
        }
        if (minerals.empty()) continue;
        // Prefer the nearest base that still has room; walking once beats mining far away forever.
        const bool saturated = assigned >= static_cast<int>(minerals.size()) * WorkersPerPatch;
        const int distance = worker->getDistance(depot);
        if (!bestDepot || (bestSaturated && !saturated) || (saturated == bestSaturated && distance < bestDistance)) {
            bestDepot = depot;
            bestMinerals = minerals;
            bestSaturated = saturated;
            bestDistance = distance;
        }
    }

    if (!bestDepot) {
        // No base has minerals left: use whichever patch is closest to a depot, else the worker.
        BWAPI::Unit fallback = nullptr;
        int fallbackDistance = std::numeric_limits<int>::max();
        for (auto mineral : BWAPI::Broodwar->getMinerals()) {
            if (mineral->getResources() <= 0) continue;
            const auto depot = GetClosestUnitTo(mineral, depots);
            const int distance = depot ? mineral->getDistance(depot) : mineral->getDistance(worker);
            if (distance < fallbackDistance) {
                fallback = mineral;
                fallbackDistance = distance;
            }
        }
        return fallback;
    }

    // Every base is full: leave the worker free to expand or build instead of oversaturating.
    if (bestSaturated) return nullptr;

    BWAPI::Unit best = nullptr;
    for (auto mineral : bestMinerals) {
        if (!best || miners[mineral] < miners[best] ||
            (miners[mineral] == miners[best] && mineral->getDistance(bestDepot) < best->getDistance(bestDepot))) best = mineral;
    }
    return best;
}

Tools::MiningCapacity Tools::GetMiningCapacity() {
    MiningCapacity capacity;
    const auto depots = UsableDepots();
    BWAPI::Unitset patches;
    for (auto mineral : BWAPI::Broodwar->getMinerals()) {
        if (mineral->getResources() > 0 && IsNearDepot(mineral, depots)) patches.insert(mineral);
    }
    capacity.mineralSlots = static_cast<int>(patches.size()) * WorkersPerPatch;
    for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
        if (!unit->getType().isWorker() || !unit->isCompleted()) continue;
        const auto target = MiningTarget(unit);
        if (target && patches.contains(target) && unit->isGatheringMinerals()) ++capacity.mineralWorkers;
        else if (unit->isIdle() && !HasPendingConstruction(unit)) ++capacity.idleWorkers;
    }
    return capacity;
}

bool Tools::GatherNearestBaseMinerals(BWAPI::Unit worker) {
    if (!worker || worker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return false;
    const auto mineral = GetMineralForWorker(worker);
    if (!mineral) return false;
    if (MiningTarget(worker) == mineral && !worker->isIdle()) return false;
    return worker->gather(mineral);
}

void Tools::FixLongDistanceMining() {
    // Pull miners back from patches that are not at one of our bases (lost hatchery, strays after scouting or defending).
    if (BWAPI::Broodwar->getFrameCount() % 24 != 12) return;
    const auto depots = UsableDepots();
    if (depots.empty()) return;
    for (auto worker : BWAPI::Broodwar->self()->getUnits()) {
        if (!worker->getType().isWorker() || !worker->isCompleted() || !worker->isGatheringMinerals() ||
            worker->isCarryingMinerals() || HasPendingConstruction(worker)) continue;
        const auto target = MiningTarget(worker);
        if (!target || IsNearDepot(target, depots)) continue;
        const auto replacement = GetMineralForWorker(worker);
        if (replacement && replacement != target && IsNearDepot(replacement, depots)) worker->gather(replacement);
        // No room at home: stop and wait as a free builder rather than long-distance mining.
        else if (!replacement) worker->stop();
    }
}

void Tools::GatherGas(BWAPI::Unit extractor, int targetWorkers) {
    if (!extractor || !extractor->isCompleted()) return;
    int count = 0;
    for (auto worker : BWAPI::Broodwar->self()->getUnits()) {
        if (!worker->getType().isWorker()) continue;
        if (worker->getLastCommand().getTarget() == extractor &&
            (!worker->isIdle() || worker->getLastCommandFrame() == BWAPI::Broodwar->getFrameCount())) ++count;
    }
    for (auto worker : BWAPI::Broodwar->self()->getUnits()) {
        if (count <= targetWorkers) break;
        if (!worker->getType().isWorker() || worker->getLastCommand().getTarget() != extractor ||
            worker->isCarryingGas() || worker->isCarryingMinerals() || HasPendingConstruction(worker) ||
            worker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) continue;
        auto mineral = GetMineralForWorker(worker);
        if (mineral && worker->gather(mineral)) --count;
    }
    for (auto worker : BWAPI::Broodwar->self()->getUnits()) {
        if (count >= targetWorkers) break;
        if (!worker->getType().isWorker() || HasPendingConstruction(worker) || !worker->isCompleted() || worker->isConstructing() ||
            worker->isCarryingMinerals() || worker->isCarryingGas() || worker->isGatheringGas() ||
            (!worker->isIdle() && !worker->isGatheringMinerals()) ||
            worker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) continue;
        const auto command = worker->getLastCommand();
        if ((command.getType() == BWAPI::UnitCommandTypes::Build && !worker->isIdle()) ||
            (command.getTarget() && command.getTarget()->getType().isRefinery() && !worker->isIdle())) continue;
        if (worker->gather(extractor)) ++count;
    }
}

static BWAPI::TilePosition FindClearBuildTile(BWAPI::UnitType type, BWAPI::TilePosition startTile,
                                              int maxBuildRange, BWAPI::Unit builder, bool buildingOnCreep);

bool Tools::BuildMacroHatchery() {
    if (IsQueued(BWAPI::UnitTypes::Zerg_Hatchery).isValid()) return false;
    const auto type = BWAPI::UnitTypes::Zerg_Hatchery;
    if (BWAPI::Broodwar->self()->minerals() - GetConstructionReserve().first < type.mineralPrice()) return false;
    // Same clear-path search as other buildings: a macro Hatchery must not land in a mineral line.
    BWAPI::Unit builder = nullptr;
    for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
        if (unit->getType().isWorker() && unit->isCompleted() && !HasPendingConstruction(unit) &&
            (!builder || (unit->isIdle() && !builder->isIdle()))) builder = unit;
    }
    if (!builder) return false;
    const auto position = FindClearBuildTile(type, BWAPI::Broodwar->self()->getStartLocation(), 14, builder, false);
    if (!position.isValid()) return false;
    return TryBuildBuilding(type, 1, position);
}

bool Tools::EnsureBaseGas(BWAPI::Unit depot) {
    if (!depot || !depot->isCompleted()) return false;
    const auto units = BWAPI::Broodwar->self()->getUnits();
    for (auto unit : units) {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Extractor && unit->getDistance(depot) < 320) return true;
    }
    if (Tools::IsQueued(BWAPI::UnitTypes::Zerg_Extractor).isValid()) return false;
    if (BWAPI::Broodwar->self()->minerals() < 50) return false;
    for (auto geyser : BWAPI::Broodwar->getGeysers()) {
        if (geyser->getDistance(depot) >= 320 || geyser->getResources() <= 0) continue;
        const bool accepted = BuildBuildingOptimal(BWAPI::UnitTypes::Zerg_Extractor, geyser->getTilePosition());
        MatchLog::Command("base_gas", "Zerg Extractor", accepted);
        return accepted;
    }
    return false;
}

bool Tools::EnsureGroundDefense(BWAPI::Unit depot, int target) {
    return EnsureStaticDefense(depot, target, BWAPI::UnitTypes::Zerg_Sunken_Colony);
}

bool Tools::EnsureStaticDefense(BWAPI::Unit depot, int target, BWAPI::UnitType finalType) {
    if (!depot || !depot->isCompleted() || CountUnitOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0) return false;
    // Spores need an Evolution Chamber; fall back to a Sunken rather than leaving a bare colony.
    if (finalType == BWAPI::UnitTypes::Zerg_Spore_Colony && CountUnitOfType(BWAPI::UnitTypes::Zerg_Evolution_Chamber) == 0)
        finalType = BWAPI::UnitTypes::Zerg_Sunken_Colony;
    int colonies = 0;
    for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
        if (unit->getDistance(depot) > 256) continue;
        const auto type = unit->getType();
        if (type == BWAPI::UnitTypes::Zerg_Creep_Colony) {
            ++colonies;
            if (unit->isCompleted()) return MorphUnit(unit, finalType);
        } else if (type == BWAPI::UnitTypes::Zerg_Sunken_Colony || type == BWAPI::UnitTypes::Zerg_Spore_Colony) ++colonies;
    }
    if (colonies >= target || IsQueued(BWAPI::UnitTypes::Zerg_Creep_Colony).isValid() ||
        BWAPI::Broodwar->self()->minerals() < 75 + finalType.mineralPrice()) return false;
    const bool accepted = BuildBuildingOptimal(BWAPI::UnitTypes::Zerg_Creep_Colony, depot->getTilePosition());
    MatchLog::Command("base_defense", "Zerg Creep Colony", accepted);
    return accepted;
}

void Tools::BalanceMineralWorkers() {
    // Transfer one available miner at a time; preserve gas, cargo, and construction orders.
    if (BWAPI::Broodwar->getFrameCount() % 24 != 0) return;
    const auto units = BWAPI::Broodwar->self()->getUnits();
    BWAPI::Unitset depots;
    for (auto unit : units) {
        if (unit->getType().isResourceDepot() && unit->isCompleted()) depots.insert(unit);
    }
    for (auto depot : depots) {
        BWAPI::Unitset minerals;
        for (auto mineral : BWAPI::Broodwar->getMinerals()) {
            if (mineral->getDistance(depot) < 320 && mineral->getResources() > 0) minerals.insert(mineral);
        }
        if (minerals.empty()) continue;
        int assigned = 0;
        for (auto worker : units) {
            if (!worker->getType().isWorker() || !worker->isGatheringMinerals()) continue;
            const auto target = worker->getLastCommand().getTarget();
            if (target && minerals.count(target)) ++assigned;
        }
        if (assigned >= static_cast<int>(minerals.size()) * WorkersPerPatch) continue;
        for (auto worker : units) {
            if (!worker->getType().isWorker() || !worker->isGatheringMinerals() ||
                worker->isCarryingMinerals() || worker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) continue;
            const auto command = worker->getLastCommand();
            if (command.getType() == BWAPI::UnitCommandTypes::Build || !command.getTarget() ||
                !command.getTarget()->getType().isMineralField()) continue;
            const auto source = GetClosestUnitTo(command.getTarget(), depots);
            if (!source || source == depot) continue;
            int sourceWorkers = 0;
            for (auto other : units) {
                const auto target = other->getLastCommand().getTarget();
                if (other->getType().isWorker() && other->isGatheringMinerals() && target &&
                    GetClosestUnitTo(target, depots) == source) ++sourceWorkers;
            }
            if (sourceWorkers <= assigned + 1) continue;
            const auto target = GetClosestUnitTo(depot, minerals);
            if (worker->gather(target)) return;
        }
    }
}

BWAPI::Unit Tools::GetDepot()
{
    const BWAPI::UnitType depot = BWAPI::Broodwar->self()->getRace().getResourceDepot();
    return GetUnitOfType(depot);
}

bool Tools::TryBuildBuilding(BWAPI::UnitType building, int limitAmount = 0, BWAPI::TilePosition desiredPos = BWAPI::Broodwar->self()->getStartLocation()) {
    if (!desiredPos.isValid()) return false;
    // Depots are limited per expansion site; tech buildings are limited globally.
    if (building == BWAPI::UnitTypes::Zerg_Hatchery) {
        for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
            if (unit->getType().isResourceDepot() && unit->getTilePosition() == desiredPos) return true;
        }
        if (Tools::IsQueued(building) == desiredPos) return true;
    } else if (limitAmount > 0 &&
        CountUnitsOfType(building, BWAPI::Broodwar->self()->getUnits(), true) >= limitAmount) {
        return true;
    }
    if (BWAPI::Broodwar->self()->minerals() < building.mineralPrice() ||
        BWAPI::Broodwar->self()->gas() < building.gasPrice()) return false;
    const bool accepted = Tools::BuildBuildingOptimal(building, desiredPos);
    MatchLog::Command("build", building.getName(), accepted);
    return accepted;
}

bool Tools::TrainUnit(BWAPI::UnitType unit) {
    const BWAPI::Unit myDepot = Tools::GetDepot();

    // if we have a valid depot unit and it's currently not training something, train a worker
    // there is no reason for a bot to ever use the unit queueing system, it just wastes resources
    if (myDepot) {
        return myDepot->train(unit);
    }
    return false;
}

bool Tools::HasPendingConstruction(BWAPI::Unit unit) {
    if (!unit || !unit->getType().isWorker()) return false;
    return unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Build &&
        (!unit->isIdle() || BWAPI::Broodwar->getFrameCount() - unit->getLastCommandFrame() <= BWAPI::Broodwar->getLatencyFrames());
}

std::pair<int, int> Tools::GetConstructionReserve() {
    int minerals = 0, gas = 0;
    for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
        if (!HasPendingConstruction(unit)) continue;
        const auto type = unit->getLastCommand().getUnitType();
        minerals += type.mineralPrice();
        gas += type.gasPrice();
    }
    return {minerals, gas};
}

bool Tools::MorphUnit(BWAPI::Unit source, BWAPI::UnitType type) {
    if (!source || !source->isCompleted() || source->isMorphing() ||
        source->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return false;
    const auto command = source->getLastCommand();
    // A different morph must not overwrite a command still awaiting BWAPI latency.
    if (command.getType() == BWAPI::UnitCommandTypes::Morph &&
        BWAPI::Broodwar->getFrameCount() - source->getLastCommandFrame() <= BWAPI::Broodwar->getLatencyFrames()) return false;
    const auto reserve = GetConstructionReserve();
    if (BWAPI::Broodwar->self()->minerals() - reserve.first < type.mineralPrice() ||
        BWAPI::Broodwar->self()->gas() - reserve.second < type.gasPrice()) return false;
    const bool accepted = source->canMorph(type) && source->morph(type);
    MatchLog::Command("morph", type.getName(), accepted);
    return accepted;
}

bool Tools::MorphLarva(BWAPI::UnitType unit) {
    for (auto larva : BWAPI::Broodwar->self()->getUnits()) {
        if (larva->getType() == BWAPI::UnitTypes::Zerg_Larva && MorphUnit(larva, unit)) return true;
    }
    return false;
}

bool Tools::ResearchUpgrade(BWAPI::UpgradeType upgrade) {
    if (BWAPI::Broodwar->self()->isUpgrading(upgrade)) return false;
    const auto reserve = GetConstructionReserve();
    const int level = BWAPI::Broodwar->self()->getUpgradeLevel(upgrade) + 1;
    if (BWAPI::Broodwar->self()->minerals() - reserve.first < upgrade.mineralPrice(level) ||
        BWAPI::Broodwar->self()->gas() - reserve.second < upgrade.gasPrice(level)) return false;
    for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
        if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || !unit->canUpgrade(upgrade)) continue;
        const bool accepted = unit->upgrade(upgrade);
        MatchLog::Command("upgrade", upgrade.getName(), accepted);
        return accepted;
    }
    return false;
}

bool Tools::ResearchTech(BWAPI::TechType upgrade) {
    const auto reserve = GetConstructionReserve();
    if (BWAPI::Broodwar->self()->minerals() - reserve.first < upgrade.mineralPrice() ||
        BWAPI::Broodwar->self()->gas() - reserve.second < upgrade.gasPrice()) return false;
    for (auto u : BWAPI::Broodwar->self()->getUnits()) {
        // if the unit is a hatchery, lair or hive, and it has enough minerals and gas
        if (u->getLastCommandFrame() < BWAPI::Broodwar->getFrameCount() && u->canResearch(upgrade) && BWAPI::Broodwar->self()->minerals() >= upgrade.mineralPrice() && BWAPI::Broodwar->self()->gas() >= upgrade.gasPrice()) {
            const bool accepted = u->research(upgrade);
            MatchLog::Command("research", upgrade.getName(), accepted);
            return accepted;
        }
    }
    return false;
}

// Attempt to construct a building of a given type 
bool Tools::BuildBuilding(BWAPI::UnitType type, BWAPI::TilePosition desiredPos = BWAPI::Broodwar->self()->getStartLocation())
{
    // Get the type of unit that is required to build the desired building
    BWAPI::UnitType builderType = type.whatBuilds().first;

    // Get a unit that we own that is of the given type so it can build
    // If we can't find a valid builder unit, then we have to cancel the building
    BWAPI::Unit builder = Tools::GetUnitOfType(builderType);
    if (!builder) { return false; }

    // Ask BWAPI for a building location near the desired position for the type
    int maxBuildRange = 64;
    bool buildingOnCreep = type.requiresCreep();
    BWAPI::TilePosition buildPos = BWAPI::Broodwar->getBuildLocation(type, desiredPos, maxBuildRange, buildingOnCreep);
    return builder->build(type, buildPos);
}

// Helper: Checks if a building footprint would sit between one of our resource depots and its
// minerals or geysers. Workers walk that corridor, so anything placed there slows gathering.
static bool BlocksResourceGathering(const BWAPI::TilePosition& tile, BWAPI::UnitType type) {
    if (type.isRefinery()) return false; // Extractors must go on the geyser itself.
    const int left = tile.x, top = tile.y;
    const int right = left + type.tileWidth(), bottom = top + type.tileHeight();
    const int maxCorridor = 12; // Resources farther than this belong to another base.
    const int padding = 1;      // Keep the tiles around the corridor free too, so drones can path around the edges.

    auto blocks = [&](const BWAPI::TilePosition& depotTile, BWAPI::UnitType depotType,
                      const BWAPI::TilePosition& resourceTile, BWAPI::UnitType resourceType) {
        const int boxLeft = std::min(depotTile.x, resourceTile.x);
        const int boxTop = std::min(depotTile.y, resourceTile.y);
        const int boxRight = std::max(depotTile.x + depotType.tileWidth(), resourceTile.x + resourceType.tileWidth());
        const int boxBottom = std::max(depotTile.y + depotType.tileHeight(), resourceTile.y + resourceType.tileHeight());
        if (boxRight - boxLeft > maxCorridor || boxBottom - boxTop > maxCorridor) return false;
        return left < boxRight + padding && right > boxLeft - padding && top < boxBottom + padding && bottom > boxTop - padding;
    };

    for (auto& depot : BWAPI::Broodwar->self()->getUnits()) {
        if (!depot->getType().isResourceDepot()) continue;
        for (auto& mineral : BWAPI::Broodwar->getStaticMinerals()) {
            if (blocks(depot->getTilePosition(), depot->getType(), mineral->getInitialTilePosition(), mineral->getInitialType())) return true;
        }
        for (auto& geyser : BWAPI::Broodwar->getStaticGeysers()) {
            if (blocks(depot->getTilePosition(), depot->getType(), geyser->getInitialTilePosition(), geyser->getInitialType())) return true;
        }
    }
    return false;
}

// Helper: Finds the buildable tile closest to the builder that keeps mining paths clear
static BWAPI::TilePosition FindClearBuildTile(BWAPI::UnitType type, BWAPI::TilePosition startTile,
                                              int maxBuildRange, BWAPI::Unit builder, bool buildingOnCreep) {
    BWAPI::TilePosition bestPos = BWAPI::TilePositions::Invalid;
    int bestDist = std::numeric_limits<int>::max();

    for (int dx = -maxBuildRange; dx <= maxBuildRange; ++dx) {
        for (int dy = -maxBuildRange; dy <= maxBuildRange; ++dy) {
            BWAPI::TilePosition candidate = startTile + BWAPI::TilePosition(dx, dy);
            if (!candidate.isValid()) continue;
            if (!BWAPI::Broodwar->canBuildHere(candidate, type, builder, buildingOnCreep)) continue;
            if (BlocksResourceGathering(candidate, type)) continue; // Avoid mining path

            int dist = builder->getDistance(BWAPI::Position(candidate));
            if (dist < bestDist) {
                bestDist = dist;
                bestPos = candidate;
            }
        }
    }
    return bestPos;
}

bool Tools::BuildBuildingOptimal(BWAPI::UnitType type, BWAPI::TilePosition desiredPos) {
    if (!desiredPos.isValid()) return false;
    const auto reserve = GetConstructionReserve();
    if (BWAPI::Broodwar->self()->minerals() - reserve.first < type.mineralPrice() ||
        BWAPI::Broodwar->self()->gas() - reserve.second < type.gasPrice()) return false;
    // Get the type of unit that is required to build the desired building
    BWAPI::UnitType builderType = type.whatBuilds().first;
    BWAPI::Unit builder = nullptr;
    // Find the closest available builder to the desired position. Idle workers (the surplus
    // left over once every patch is saturated) go first so builds do not pull active miners.
    int minDist = std::numeric_limits<int>::max();
    for (auto& unit : BWAPI::Broodwar->self()->getUnits()) {
        if (unit->getType() == builderType && unit->isCompleted() && !unit->isConstructing() &&
            !unit->isMorphing() && unit->getLastCommandFrame() < BWAPI::Broodwar->getFrameCount() &&
            !HasPendingConstruction(unit)) {
            const bool spare = unit->getType().isWorker() && unit->isIdle();
            int dist = unit->getDistance(BWAPI::Position(desiredPos)) + (spare ? 0 : 100000);
            if (dist < minDist) {
                minDist = dist;
                builder = unit;
            }
        }
    }
    if (!builder) return false;
    bool buildingOnCreep = type.requiresCreep();

    if (type != BWAPI::UnitTypes::Zerg_Hatchery) {

        if (type == BWAPI::UnitTypes::Zerg_Hive || type == BWAPI::UnitTypes::Zerg_Lair || type == BWAPI::UnitTypes::Zerg_Sunken_Colony || type == BWAPI::UnitTypes::Zerg_Spore_Colony) {
            // Special case for Hive and Lair, they can only be built at the main base
            return Tools::MorphUnit(builder, type);
        }

        // Colonies must stay within the base they defend; other buildings may spread out.
        const bool colony = type == BWAPI::UnitTypes::Zerg_Creep_Colony;
        // Search around the requested base, avoiding mining paths. Widen the search before
        // giving up so a crowded base does not push the building into the mineral line.
        const std::vector<int> ranges = colony ? std::vector<int>{ 6, 8 } : std::vector<int>{ 16, 24, 32 };
        BWAPI::TilePosition bestPos = BWAPI::TilePositions::Invalid;
        for (int range : ranges) {
            bestPos = FindClearBuildTile(type, desiredPos, range, builder, buildingOnCreep);
            if (bestPos.isValid()) break;
        }
        // No clear tile: skip this build rather than blocking a mineral line.
        return bestPos.isValid() && builder->build(type, bestPos);
    } else {
        // A scouting move is not a successful construction order. Retry on later frames.
        if (!BWAPI::Broodwar->isExplored(desiredPos)) {
            Micro::SmartMove(builder, BWAPI::Position(desiredPos));
            return false;
        }
        return builder->build(type, desiredPos);
    }
}

void Tools::DrawUnitCommands()
{
    for (auto& unit : BWAPI::Broodwar->self()->getUnits())
    {
        const BWAPI::UnitCommand & command = unit->getLastCommand();

        // If the previous command had a ground position target, draw it in red
        // Example: move to location on the map
        if (command.getTargetPosition() != BWAPI::Positions::None)
        {
            BWAPI::Broodwar->drawLineMap(unit->getPosition(), command.getTargetPosition(), BWAPI::Colors::Red);
        }

        // If the previous command had a tile position target, draw it in red
        // Example: build at given tile position location
        if (command.getTargetTilePosition() != BWAPI::TilePositions::None)
        {
            BWAPI::Broodwar->drawLineMap(unit->getPosition(), BWAPI::Position(command.getTargetTilePosition()), BWAPI::Colors::Green);
        }

        // If the previous command had a unit target, draw it in red
        // Example: attack unit, mine mineral, etc
        if (command.getTarget() != nullptr)
        {
            BWAPI::Broodwar->drawLineMap(unit->getPosition(), command.getTarget()->getPosition(), BWAPI::Colors::White);
        }
    }
}

void Tools::DrawUnitBoundingBoxes()
{
    for (auto& unit : BWAPI::Broodwar->getAllUnits())
    {
        BWAPI::Position topLeft(unit->getLeft(), unit->getTop());
        BWAPI::Position bottomRight(unit->getRight(), unit->getBottom());

        // Fix the problematic line by converting Position to a string using BWAPI::Text::Enum::Default formatting  
        if (unit->getType().isBuilding()) {
            BWAPI::Broodwar->drawTextMap(unit->getPosition(), "%s", PositionToString(unit->getPosition()).c_str());
        }
        else {
            BWAPI::Broodwar->drawTextMap(unit->getPosition(), "%i", unit->getID());
        }
        BWAPI::Broodwar->drawBoxMap(topLeft, bottomRight, BWAPI::Colors::White);
    }
}

std::string Tools::PositionToString(const BWAPI::Position& position) {
    std::ostringstream oss;
    oss << "(" << position.x << ", " << position.y << ")";
    return oss.str();
}

std::string Tools::TilePositionToString(const BWAPI::TilePosition& position) {
    std::ostringstream oss;
    oss << "(" << position.x << ", " << position.y << ")";
    return oss.str();
}

void Tools::SmartRightClick(BWAPI::Unit unit, BWAPI::Unit target)
{
    // if there's no valid unit, ignore the command
    if (!unit || !target) { return; }

    // Don't issue a 2nd command to the unit on the same frame
    if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) { return; }

    // If we are issuing the same type of command with the same arguments, we can ignore it
    // Issuing multiple identical commands on successive frames can lead to bugs
    if (unit->getLastCommand().getTarget() == target) { return; }
    
    // If there's nothing left to stop us, right click!
    unit->rightClick(target);
}

int Tools::GetTotalSupply(bool inProgress)
{
    // start the calculation by looking at our current completed supplyt
    int totalSupply = BWAPI::Broodwar->self()->supplyTotal();

    // if we don't want to calculate the supply in progress, just return that value
    if (!inProgress) { return totalSupply; }

    // if we do care about supply in progress, check all the currently constructing units if they will add supply
    for (auto& unit : BWAPI::Broodwar->self()->getUnits())
    {
        const auto command = unit->getLastCommand();
        const bool overlordEgg = unit->getType() == BWAPI::UnitTypes::Zerg_Egg &&
            unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord;
        const bool pendingOverlord = unit->getType() == BWAPI::UnitTypes::Zerg_Larva &&
            command.getType() == BWAPI::UnitCommandTypes::Morph && command.getUnitType() == BWAPI::UnitTypes::Zerg_Overlord &&
            BWAPI::Broodwar->getFrameCount() - unit->getLastCommandFrame() <= BWAPI::Broodwar->getLatencyFrames();
        if (overlordEgg || pendingOverlord) totalSupply += BWAPI::UnitTypes::Zerg_Overlord.supplyProvided();
    }

    // one last tricky case: if a unit is currently on its way to build a supply provider, add it
    for (auto& unit : BWAPI::Broodwar->self()->getUnits())
    {
        // get the last command given to the unit
        const BWAPI::UnitCommand& command = unit->getLastCommand();

        // if it's not a build command we can ignore it
        if (!HasPendingConstruction(unit)) { continue; }

        // add the supply amount of the unit that it's trying to build
        totalSupply += command.getUnitType().supplyProvided();
    }

    return totalSupply;
}

BWAPI::TilePosition Tools::IsQueued(BWAPI::UnitType unit) {
    // one last tricky case: if a unit is currently on its way to build a supply provider, add it
    for (auto& readyUnit : BWAPI::Broodwar->self()->getUnits()) {
        // get the last command given to the unit
        const BWAPI::UnitCommand& command = readyUnit->getLastCommand();

        // if it's not a build command we can ignore it
        if (command.getType() != BWAPI::UnitCommandTypes::Build || command.getUnitType() != unit ||
            (readyUnit->isIdle() && BWAPI::Broodwar->getFrameCount() - readyUnit->getLastCommandFrame() > BWAPI::Broodwar->getLatencyFrames())) { continue; }

        return command.getTargetTilePosition();
    }
    return BWAPI::TilePositions::None;
}

bool Tools::IsReady(BWAPI::UnitType unit) {
    // one last tricky case: if a unit is currently on its way to build a supply provider, add it
    for (auto& readyUnit : BWAPI::Broodwar->self()->getUnits()) {

        // if it's not a build command we can ignore it
        if (unit != readyUnit->getType()) { continue; }

        return true;
    }
    return false;
}

void Tools::DrawUnitHealthBars()
{
    // how far up from the unit to draw the health bar
    int verticalOffset = -10;

    // draw a health bar for each unit on the map
    for (auto& unit : BWAPI::Broodwar->getAllUnits())
    {
        // determine the position and dimensions of the unit
        const BWAPI::Position& pos = unit->getPosition();
        int left = pos.x - unit->getType().dimensionLeft();
        int right = pos.x + unit->getType().dimensionRight();
        int top = pos.y - unit->getType().dimensionUp();
        int bottom = pos.y + unit->getType().dimensionDown();

        // if it's a resource, draw the resources remaining
        if (unit->getType().isResourceContainer() && unit->getInitialResources() > 0)
        {
            double mineralRatio = (double)unit->getResources() / (double)unit->getInitialResources();
            DrawHealthBar(unit, mineralRatio, BWAPI::Colors::Cyan, 0);
        }
        // otherwise if it's a unit, draw the hp 
        else if (unit->getType().maxHitPoints() > 0)
        {
            double hpRatio = (double)unit->getHitPoints() / (double)unit->getType().maxHitPoints();
            BWAPI::Color hpColor = BWAPI::Colors::Green;
            if (hpRatio < 0.66) hpColor = BWAPI::Colors::Orange;
            if (hpRatio < 0.33) hpColor = BWAPI::Colors::Red;
            DrawHealthBar(unit, hpRatio, hpColor, 0);
            
            // if it has shields, draw those too
            if (unit->getType().maxShields() > 0)
            {
                double shieldRatio = (double)unit->getShields() / (double)unit->getType().maxShields();
                DrawHealthBar(unit, shieldRatio, BWAPI::Colors::Blue, -3);
            }
        }
    }
}

void Tools::DrawHealthBar(BWAPI::Unit unit, double ratio, BWAPI::Color color, int yOffset)
{
    int verticalOffset = -10;
    const BWAPI::Position& pos = unit->getPosition();

    int left = pos.x - unit->getType().dimensionLeft();
    int right = pos.x + unit->getType().dimensionRight();
    int top = pos.y - unit->getType().dimensionUp();
    int bottom = pos.y + unit->getType().dimensionDown();

    int ratioRight = left + (int)((right - left) * ratio);
    int hpTop = top + yOffset + verticalOffset;
    int hpBottom = top + 4 + yOffset + verticalOffset;

    BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
    BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), color, true);
    BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

    int ticWidth = 3;

    for (int i(left); i < right - 1; i += ticWidth)
    {
        BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
    }
}

void Tools::print(std::string stringToPrint) {
    BWAPI::Broodwar->printf("%s", stringToPrint.c_str());
}