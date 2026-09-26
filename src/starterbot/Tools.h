#pragma once

#include <BWAPI.h>
#include <utility>

namespace Tools
{
    BWAPI::Unit GetClosestUnitTo(BWAPI::Position p, const BWAPI::Unitset& units);
    BWAPI::Unit GetClosestUnitTo(BWAPI::Unit unit, const BWAPI::Unitset& units);

    int CountUnitsOfType(BWAPI::UnitType type, const BWAPI::Unitset& units, const bool inProgress);

    BWAPI::Unit GetUnitOfType(BWAPI::UnitType type);
    int CountUnitOfType(BWAPI::UnitType type);
    void Scout(BWAPI::Unit scout);
    void BalanceMineralWorkers();
    // Returns nullptr when every base already has two miners per patch.
    BWAPI::Unit GetMineralForWorker(BWAPI::Unit worker);
    struct MiningCapacity { int mineralSlots = 0, mineralWorkers = 0, idleWorkers = 0; };
    MiningCapacity GetMiningCapacity();
    bool GatherNearestBaseMinerals(BWAPI::Unit worker);
    void FixLongDistanceMining();
    void GatherGas(BWAPI::Unit extractor, int targetWorkers = 3);
    bool BuildMacroHatchery();
    bool EnsureBaseGas(BWAPI::Unit depot);
    bool EnsureGroundDefense(BWAPI::Unit depot, int target);
    // Creep Colonies at a base, morphed into `finalType` (Sunken or Spore) once complete.
    bool EnsureStaticDefense(BWAPI::Unit depot, int target, BWAPI::UnitType finalType);
    BWAPI::Unit GetDepot();

    bool TryBuildBuilding(BWAPI::UnitType building, int limitAmount, BWAPI::TilePosition desiredPos);
    bool TrainUnit(BWAPI::UnitType unit);
    bool HasPendingConstruction(BWAPI::Unit unit);
    std::pair<int, int> GetConstructionReserve();
    bool MorphUnit(BWAPI::Unit source, BWAPI::UnitType type);
    bool MorphLarva(BWAPI::UnitType unit);
    bool ResearchUpgrade(BWAPI::UpgradeType upgrade);
    bool ResearchTech(BWAPI::TechType upgrade);
    bool BuildBuilding(BWAPI::UnitType type, BWAPI::TilePosition desiredPos);

    void DrawUnitBoundingBoxes();
    std::string PositionToString(const BWAPI::Position& position);
    std::string TilePositionToString(const BWAPI::TilePosition& position);
    bool BuildBuildingOptimal(BWAPI::UnitType type, BWAPI::TilePosition desiredPos);
    void DrawUnitCommands();

    void SmartRightClick(BWAPI::Unit unit, BWAPI::Unit target);

    int GetTotalSupply(bool inProgress = false);
    BWAPI::TilePosition IsQueued(BWAPI::UnitType unit);

    bool IsReady(BWAPI::UnitType unit);

    void DrawUnitHealthBars();
    void DrawHealthBar(BWAPI::Unit unit, double ratio, BWAPI::Color color, int yOffset);
    void print(std::string stringToPrint);
}