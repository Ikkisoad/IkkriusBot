#include "Tools.h"
#include <BWAPI.h>
#include <vector>
#include <algorithm>
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
    for (auto& unit : units)
    {
        //Count units that are being produced
        if (unit->getType() == type || inProgress && (unit->getBuildType() == type || unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Build && unit->getLastCommand().getUnitType() == type))
        {
            sum++;
            if (type == BWAPI::UnitTypes::Zerg_Zergling || type == BWAPI::UnitTypes::Zerg_Scourge) sum++;
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

void Tools::GatherGas(BWAPI::Unit extractor) {
    int count = 0;
    // For each unit that we own
    for (auto& unit : BWAPI::Broodwar->self()->getUnits()) {
        // if the unit is of the correct type, and it actually has been constructed, return it
        if (unit->getType().isWorker()) {
            unit->gather(extractor);
            count++;
            if (count >= 3) return;
        }
    }
}

BWAPI::Unit Tools::GetDepot()
{
    const BWAPI::UnitType depot = BWAPI::Broodwar->self()->getRace().getResourceDepot();
    return GetUnitOfType(depot);
}

bool Tools::TryBuildBuilding(BWAPI::UnitType building, int limitAmount = 0, BWAPI::TilePosition desiredPos = BWAPI::Broodwar->self()->getStartLocation()) {
    if (Tools::IsQueued(building) == desiredPos || Tools::IsReady(building) && limitAmount != 0) {
        return true;
    }

    if (BWAPI::Broodwar->self()->minerals() <= building.mineralPrice()) {
        return false;
    }

    if (limitAmount != 0 && Tools::CountUnitsOfType(building, BWAPI::Broodwar->self()->getUnits(), true) >= limitAmount) {
        return false;
    }

    return Tools::BuildBuildingOptimal(building, desiredPos);
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

bool Tools::MorphLarva(BWAPI::UnitType unit) {
    for (auto& larva : BWAPI::Broodwar->self()->getUnits()) {
        if (larva->getType() == BWAPI::UnitTypes::Zerg_Larva && larva->isCompleted()) {
            if (larva->getLastCommandFrame() < BWAPI::Broodwar->getFrameCount() && !larva->isMorphing()) {
                if (larva->train(unit)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool Tools::ResearchUpgrade(BWAPI::UpgradeType upgrade) {
    for (auto u : BWAPI::Broodwar->self()->getUnits()) {
        // if the unit is a hatchery, lair or hive, and it has enough minerals and gas
        if (u->canResearch(upgrade) && BWAPI::Broodwar->self()->minerals() >= upgrade.mineralPrice() && BWAPI::Broodwar->self()->gas() >= upgrade.gasPrice()) {
            return u->upgrade(upgrade);
        }
	}
}

bool Tools::ResearchTech(BWAPI::TechType upgrade) {
    for (auto u : BWAPI::Broodwar->self()->getUnits()) {
        // if the unit is a hatchery, lair or hive, and it has enough minerals and gas
        if (u->canResearch(upgrade) && BWAPI::Broodwar->self()->minerals() >= upgrade.mineralPrice() && BWAPI::Broodwar->self()->gas() >= upgrade.gasPrice()) {
            return u->research(upgrade);
        }
    }
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

// Helper: Checks if a tile is near any mineral patch or hatchery
static bool IsNearMiningPath(const BWAPI::TilePosition& tile, int buffer = 2) {
    for (auto& mineral : BWAPI::Broodwar->getMinerals()) {
        if (!mineral->exists()) continue;
        BWAPI::TilePosition mineralTile = mineral->getTilePosition();
        if (BWAPI::Position(tile).getApproxDistance(BWAPI::Position(mineralTile)) < buffer * 32)
            return true;
    }
    for (auto& unit : BWAPI::Broodwar->self()->getUnits()) {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery ||
            unit->getType() == BWAPI::UnitTypes::Zerg_Lair ||
            unit->getType() == BWAPI::UnitTypes::Zerg_Hive) {
            BWAPI::TilePosition hatchTile = unit->getTilePosition();
            if (BWAPI::Position(tile).getApproxDistance(BWAPI::Position(hatchTile)) < buffer * 32)
                return true;
        }
    }
    return false;
}

bool Tools::BuildBuildingOptimal(BWAPI::UnitType type, BWAPI::TilePosition desiredPos) {
    // Get the type of unit that is required to build the desired building
    BWAPI::UnitType builderType = type.whatBuilds().first;
    BWAPI::Unit builder = nullptr;
    // Find the closest available builder to the desired position
    int minDist = std::numeric_limits<int>::max();
    for (auto& unit : BWAPI::Broodwar->self()->getUnits()) {
        if (unit->getType() == builderType && unit->isCompleted() && !unit->isConstructing()) {
            int dist = unit->getDistance(BWAPI::Position(desiredPos));
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
            builder->morph(type);
            return true;
        }

        int maxBuildRange = 16; // Tight range for fast buildings like Spawning Pool
        BWAPI::TilePosition startTile = builder->getTilePosition();

        // Search for a valid build location near the builder, avoiding mining paths
        BWAPI::TilePosition bestPos = BWAPI::TilePositions::Invalid;
        int bestDist = std::numeric_limits<int>::max();

        for (int dx = -maxBuildRange; dx <= maxBuildRange; ++dx) {
            for (int dy = -maxBuildRange; dy <= maxBuildRange; ++dy) {
                BWAPI::TilePosition candidate = startTile + BWAPI::TilePosition(dx, dy);
                if (!candidate.isValid()) continue;
                if (!BWAPI::Broodwar->canBuildHere(candidate, type, builder, buildingOnCreep)) continue;
                if (IsNearMiningPath(candidate, 2)) continue; // Avoid mining path

                int dist = builder->getDistance(BWAPI::Position(candidate));
                if (dist < bestDist) {
                    bestDist = dist;
                    bestPos = candidate;
                }
            }
        }

        if (bestPos.isValid()) {
            return builder->build(type, bestPos);
        }
    } else if (!BWAPI::Broodwar->isExplored(desiredPos) || !builder->build(type, desiredPos)) {
        Micro::SmartScoutMove(builder, BWAPI::Position(desiredPos));
        return true;
    }

    // Fallback: use BWAPI's default search if no optimal found
    const BWAPI::TilePosition fallback = BWAPI::Broodwar->getBuildLocation(type, desiredPos, 64, buildingOnCreep); // Mark fallback as const to fix C26496
    if (fallback.isValid()) {
        return builder->build(type, fallback);
    }

    return false;
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
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg && unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord) {
            totalSupply += BWAPI::UnitTypes::Zerg_Overlord.supplyProvided();
        }
    }

    // one last tricky case: if a unit is currently on its way to build a supply provider, add it
    for (auto& unit : BWAPI::Broodwar->self()->getUnits())
    {
        // get the last command given to the unit
        const BWAPI::UnitCommand& command = unit->getLastCommand();

        // if it's not a build command we can ignore it
        if (command.getType() != BWAPI::UnitCommandTypes::Build) { continue; }

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
        if (command.getType() != BWAPI::UnitCommandTypes::Build || command.getUnitType() != unit) { continue; }

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