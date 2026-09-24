#include "BasesTools.h"
#include <vector>  
#include <algorithm>
#include <BWAPI.h>  
#include "../BWEM/bwem.h"  
#include "../src/starterbot/Tools.h"
#include <random>
#include "../BWEM/include/base.h"

auto& bwem = BWEM::Map::Instance();  

BWAPI::TilePosition mainBasePosition;
//std::vector<BWAPI::Position> mainBasePosition;
//TODO remove destroyed bases
// Store all known enemy base positions
std::vector<BWAPI::Position> enemyBasePositions;
std::vector<BWAPI::Position> ourBasePositions;

// Store all base positions for easy reference
static std::vector<BWAPI::Position> allBasePositions;

namespace BasesTools {  
    void BasesTools::Initialize() {    
        bwem.Initialize(BWAPI::BroodwarPtr);
        bwem.EnableAutomaticPathAnalysis();
        bwem.FindBasesForStartingLocations();
        int baseCount = bwem.BaseCount();
        Tools::print("Base count vvv");
        Tools::print(std::to_string(baseCount));
        enemyBasePositions.clear();
        mainBasePosition = BWAPI::TilePositions::None;
        CacheBWEMBases();
        ourBasePositions.clear();
    }
    // Adds a new enemy base position if not already present
    void SetEnemyBasePosition(const BWAPI::Position& pos) {
        if (pos == BWAPI::Positions::None) return;
        auto it = std::find(enemyBasePositions.begin(), enemyBasePositions.end(), pos);
        if (it == enemyBasePositions.end()) {
            enemyBasePositions.push_back(pos);
        }
    }

    void SetOurBasePosition() {
        for (const auto& basePos : allBasePositions) {
            BWAPI::TilePosition tilePos(basePos);
            bool foundDepot = false;
            for (auto& unit : BWAPI::Broodwar->getUnitsOnTile(tilePos)) {
                if (unit->getPlayer() == BWAPI::Broodwar->self() &&
                    unit->getType().isResourceDepot() &&
                    unit->exists()) {
                    foundDepot = true;
                    break;
                }
            }
            if (foundDepot) {
                // Only add if not already present
                if (std::find(ourBasePositions.begin(), ourBasePositions.end(), basePos) == ourBasePositions.end()) {
                    ourBasePositions.push_back(basePos);
                }
            }
        }
    }

    const std::vector<BWAPI::Position>& BasesTools::GetAllOurBasePositions() {
        return ourBasePositions;
    }

    // Not working properly
    void BasesTools::VerifyEnemyBases() {
		bool breakLoop = false;
        const auto knownPositions = enemyBasePositions;
        for (auto base : knownPositions) {
            breakLoop = false;
			BWAPI::TilePosition tilePosition = BWAPI::TilePosition(base);
			if (!BWAPI::Broodwar->isVisible(tilePosition)) continue;
            for (auto unit : BWAPI::Broodwar->getUnitsOnTile(tilePosition.x, tilePosition.y)) {
                if (unit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser) break;
                if (unit->getType().isBuilding() && unit->exists()) {
                    if (unit->getPlayer() != BWAPI::Broodwar->self() && BWAPI::Broodwar->neutral() != unit->getPlayer()) {
						// If the building is an enemy building, do nothing
						breakLoop = true;
						break;
					}
                }
            }
            if (breakLoop) continue;
            RemoveEnemyBasePosition(base);
        }
    }

    // Returns the first known enemy base position, or BWAPI::Positions::None if none exist
    BWAPI::Position GetEnemyBasePosition() {
        if (!enemyBasePositions.empty()) {
            return enemyBasePositions.front();
        }
        return BWAPI::Positions::None;
    }

    // Removes a specific enemy base position if it exists
    void RemoveEnemyBasePosition(const BWAPI::Position pos) {
        auto it = std::remove(enemyBasePositions.begin(), enemyBasePositions.end(), pos);
        if (it != enemyBasePositions.end()) {
            enemyBasePositions.erase(it, enemyBasePositions.end());
        }
    }

    // Returns all known enemy base positions
    const std::vector<BWAPI::Position>& GetAllEnemyBasePositions() {
        return enemyBasePositions;
    }

    // Checks a square area of tiles around the given position for enemy buildings.
    // Range is in tiles (default 3), so a 3x3 area centered on the position is checked.
    bool BasesTools::IsAreaEnemyBase(BWAPI::Position position, int range) {
        auto area = bwem.GetArea(BWAPI::TilePosition(position));
        if (!area) return false;

        BWAPI::TilePosition centerTile(position);
        int minX = std::max(0, centerTile.x - range);
        int maxX = std::min(BWAPI::Broodwar->mapWidth() - 1, centerTile.x + range);
        int minY = std::max(0, centerTile.y - range);
        int maxY = std::min(BWAPI::Broodwar->mapHeight() - 1, centerTile.y + range);

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                BWAPI::TilePosition tile(x, y);
                for (auto& unit : BWAPI::Broodwar->getUnitsOnTile(tile)) {
                    if (unit->getPlayer()->isEnemy(BWAPI::Broodwar->self()) &&
                        unit->getType().isBuilding() && unit->exists()) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Checks a square area of tiles around the given position for enemy buildings.
    // Range is in tiles (default 3), so a 3x3 area centered on the position is checked.
    bool BasesTools::IsAreaOurBase(BWAPI::Position position, int range) {
        auto area = bwem.GetArea(BWAPI::TilePosition(position));
        if (!area) return false;

        BWAPI::TilePosition centerTile(position);
        int minX = std::max(0, centerTile.x - range);
        int maxX = std::min(BWAPI::Broodwar->mapWidth() - 1, centerTile.x + range);
        int minY = std::max(0, centerTile.y - range);
        int maxY = std::min(BWAPI::Broodwar->mapHeight() - 1, centerTile.y + range);

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                BWAPI::TilePosition tile(x, y);
                for (auto& unit : BWAPI::Broodwar->getUnitsOnTile(tile)) {
                    if (unit->getPlayer() == BWAPI::Broodwar->self() &&
                        unit->getType().isBuilding() && unit->exists()) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Fix for the reported errors
    void BasesTools::CacheBWEMBases() {
        allBasePositions.clear();
        for (auto& area : bwem.Areas()) { // Ensure "area" is correctly defined
            for (const BWEM::Base& base : area.Bases()) { // Use BWEM::Base explicitly to avoid identifier issues
                allBasePositions.push_back(BWAPI::Position(base.Location()));
            }
        }
    }

    const std::vector<BWAPI::Position>& BasesTools::GetAllBasePositions() {
        return allBasePositions;
    }

    void BasesTools::OnUnitDestroyed(BWAPI::Unit unit) {
        if (unit->getType().isSpecialBuilding()) bwem.OnStaticBuildingDestroyed(unit);
        if (unit->getType().isMineralField()) bwem.OnMineralDestroyed(unit);
    }

    void BasesTools::FindExpansionsV1() {
        auto bases = bwem.GetNearestArea(BWAPI::Broodwar->self()->getStartLocation())->AccessibleNeighbours();
        for (auto& base : bases) {
            for (auto& unit : base->Bases()) {
                BWAPI::Position pos(unit.Location());
                BWAPI::Broodwar->drawCircleMap(pos, 16, BWAPI::Colors::Green, true);
            }
        }
    }

    void BasesTools::FindExpansions() {
        BWAPI::Position mainBase(BWAPI::Broodwar->self()->getStartLocation());
        mainBasePosition = BWAPI::TilePosition(mainBase);
		Tools::print("Main Base Position: " + std::to_string(mainBasePosition.x) + ", " + std::to_string(mainBasePosition.y));
    }

    void BasesTools::DrawExpansions() {
        BWAPI::Broodwar->drawCircleMap(BWAPI::Position(mainBasePosition), 32, BWAPI::Colors::Red, true);
    }

    BWAPI::Position BasesTools::ReturnBasePosition(BWEM::Area area) {
        if (area.Bases().size() <= 0) {
            return BWAPI::Positions::None;
        }
        for (auto& base : area.Bases()) {
            BWAPI::Position pos(base.Location());
            return pos;
        }
	}

    std::vector<const BWEM::Base*> BasesTools::GetAccessibleNeighborBases(const BWAPI::TilePosition& tilePos) {
        std::vector<const BWEM::Base*> result;  
        auto area = BWEM::Map::Instance().GetNearestArea(tilePos);  
        if (!area) return result;  
        for (auto neighbor : area->AccessibleNeighbours()) {  
            for (const auto& base : neighbor->Bases()) {  
                result.push_back(&base);  
            }  
        }  
        return result;  
    }  

    void BasesTools::DrawBases(const std::vector<const BWEM::Base*>& bases, BWAPI::Color color) {
        for (const auto* base : bases) {  
            BWAPI::Position pos(base->Location());  
            BWAPI::Broodwar->drawCircleMap(pos, 32, color, true);  
        }  
    }

    BWAPI::Position BasesTools::FindUnexploredStarterPosition() {
        BWAPI::Position pos = BWAPI::Position(0, 0);
        for (const auto& tile : BWAPI::Broodwar->getStartLocations()) {
            if (tile != BWAPI::Broodwar->self()->getStartLocation() && !BWAPI::Broodwar->isExplored(tile)) {
                if (pos == BWAPI::Position(0, 0)) {
                    pos = BWAPI::Position(tile);
                    continue;
                } 
                auto testedTile = BWAPI::Position(tile);
                BWAPI::Position mainBase = BWAPI::Position(mainBasePosition);
                int testedDist = testedTile.getApproxDistance(mainBase);
                int posDist = pos.getApproxDistance(mainBase);
                if (testedDist < posDist) {
                    pos = testedTile;
                }
            }
        }
        // Return the first known enemy base position if no unexplored tile is found
        return (pos != BWAPI::Position(0, 0)) ? pos : GetEnemyBasePosition();
    }

    BWAPI::TilePosition BasesTools::GetMainBasePosition() {
        return mainBasePosition;
	}

    std::vector<BWAPI::Position> BasesTools::GetBWEMBases() {
        std::vector<BWAPI::Position> basePositions;
        for (auto area : BWEM::Map::Instance().Areas()) {
            for (auto& base : area.Bases()) {
                BWAPI::Position pos(base.Location());
                basePositions.push_back(pos);
            }
        }
        return basePositions;
    }

    BWAPI::Position BasesTools::GetNearestBasePosition(const BWAPI::Position& position) {
        auto bases = GetBWEMBases();
        BWAPI::Position nearestBase = BWAPI::Positions::None;
        int minDistance = std::numeric_limits<int>::max();
        for (const auto& base : bases) {
            int distance = position.getApproxDistance(base);
            if (distance < minDistance) {
                minDistance = distance;
                nearestBase = base;
            }
        }
        return nearestBase;
    }

    void BasesTools::DrawAllBases(BWAPI::Color color) {
        for (const auto& pos : allBasePositions) {
            BWAPI::Broodwar->drawCircleMap(pos, 10, color, true);
            // Offset: left by 20 pixels, down by 14 pixels (well aligned below the circle)
            BWAPI::Broodwar->drawTextMap(pos.x - 20, pos.y + 14, "%s", Tools::TilePositionToString(BWAPI::TilePosition(pos)).c_str());
        }
    }

    void BasesTools::DrawEnemyBases(BWAPI::Color color) {
        for (const auto& pos : enemyBasePositions) {
            BWAPI::Broodwar->drawCircleMap(pos, 5, color, true);
            BWAPI::Broodwar->drawTextMap(pos, "Enemy Base\n %i %i", pos.x, pos.y);
        }
    }

    int BasesTools::CountMiningSites() {
        int count = 0;
        for (auto position : allBasePositions) {
            for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
                if (unit->exists() && unit->getType().isResourceDepot() &&
                    unit->getTilePosition() == BWAPI::TilePosition(position)) { ++count; break; }
            }
        }
        return count;
    }

    BWAPI::TilePosition BasesTools::GetNextExpansionPosition() {
        BWAPI::TilePosition bestPos = BWAPI::TilePositions::None;
        int minDist = std::numeric_limits<int>::max();
        BWAPI::TilePosition myMain = mainBasePosition;
        const auto mainArea = bwem.GetNearestArea(myMain);
        for (auto b : allBasePositions) {
            const auto area = bwem.GetNearestArea(BWAPI::TilePosition(b));
            if (!mainArea || !area || !area->AccessibleFrom(mainArea)) continue;
            // Skip if area is already occupied by us or enemy
            if (IsAreaOurBase(b, 3) || IsAreaEnemyBase(b, 3))
                continue;
            bool threatened = false;
            for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
                if (enemy->exists() && enemy->isVisible() && BWAPI::Broodwar->self()->isEnemy(enemy->getPlayer()) &&
                    enemy->getType().canAttack() && enemy->getDistance(b) < 384) { threatened = true; break; }
            }
            if (threatened) continue;
            int dist = BWAPI::Position(myMain).getApproxDistance(b);
            if (dist < minDist) {
                minDist = dist;
                bestPos = BWAPI::TilePosition(b);
            }
        }
        return bestPos;
    }
}