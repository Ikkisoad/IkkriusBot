#include "8Pool.h"
#include "../Tools.h"
#include "../../../visualstudio/BasesTools.h"
#include "../micro.h"
#include "BuildOrderTools.h"

EightPool& EightPool::Instance() {
    static EightPool instance;
    return instance;
}

void EightPool::onStart() {
    builtEightDrones = false;
    builtExtractor = false;
    builtSpawningPool = false;
}

void EightPool::Execute() {
    const BWAPI::Unitset& myUnits = BWAPI::Broodwar->self()->getUnits();

    int hatchCount = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hatchery) + Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) + Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive);
    // Build up to 8 drones before pool
    if (!builtEightDrones) {
        const int droneCount = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Drone, myUnits, true);
        if (droneCount < 8 && BWAPI::Broodwar->self()->minerals() >= 50) {
            Tools::TrainUnit(BWAPI::UnitTypes::Zerg_Drone);
            return;
        }
        if (droneCount >= 8) {
            builtEightDrones = true;
        }
    }
    else {

        if (BWAPI::Broodwar->self()->gas() >= 100 && hatchCount <= 1) {
            for (auto unit : myUnits) {
                if (unit->getType() == BWAPI::UnitTypes::Zerg_Spawning_Pool) {
                    unit->upgrade(BWAPI::UpgradeTypes::Metabolic_Boost);
                }
                else if (unit->getType().isWorker() && unit->isGatheringGas()) {
					Tools::GatherNearestBaseMinerals(unit);
                }
			}
        }
        else if(BWAPI::Broodwar->self()->gas() >= 150){
            for (auto unit : myUnits) {
                if ((unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery || unit->getType() == BWAPI::UnitTypes::Zerg_Lair) && (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) < 1 || Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive) < 1)) {
                    if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery) {
                        unit->morph(BWAPI::UnitTypes::Zerg_Lair);
                    }
                    else {
                        unit->morph(BWAPI::UnitTypes::Zerg_Hive);
                    }
                }
            }
        }

    }
    if (BWAPI::Broodwar->self()->minerals() >= 190) {
        if (!Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spawning_Pool, 1, BWAPI::Broodwar->self()->getStartLocation())) {
            builtSpawningPool = Tools::BuildBuilding(BWAPI::UnitTypes::Zerg_Spawning_Pool, BWAPI::Broodwar->self()->getStartLocation());
        }
    }

    if (builtSpawningPool) {
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Extractor, 1, BWAPI::Broodwar->self()->getStartLocation());
        if (BWAPI::Broodwar->self()->supplyTotal() == BWAPI::Broodwar->self()->supplyUsed()) Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Overlord);
    }

    if (hatchCount > 1) {
        Micro::SetMode(Micro::MicroMode::Neutral);
        if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Zergling) / BWAPI::Broodwar->self()->supplyUsed() <= 0.2) {
            //Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Zergling);
            for (auto b : BasesTools::GetAllOurBasePositions()) {
                Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Creep_Colony, 0, BWAPI::TilePosition(b));
            }
        }
        if (hatchCount * 20 > Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Drone)) Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
    }
    else {
        Micro::SetMode(Micro::MicroMode::Aggressive);
    }

    // Train zerglings when pool is done
    if (Tools::GetUnitOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool)) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Zergling);

        if (builtExtractor && BWAPI::Broodwar->self()->minerals() >= 350) {
            Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hatchery, 0, BasesTools::GetNextExpansionPosition());
        }
    }
    Micro::BasicAttackAndScoutLoop(myUnits);
}

void EightPool::OnUnitCreate(BWAPI::Unit unit) {
    if (unit->getType() == BWAPI::UnitTypes::Zerg_Spawning_Pool) {
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Extractor, 1, BWAPI::Broodwar->self()->getStartLocation());
	}
}

void EightPool::onUnitComplete(BWAPI::Unit unit) {
    if (unit->getType() == BWAPI::UnitTypes::Zerg_Extractor) {
        builtExtractor = true;
        int count = 0;
        for (auto drone : BWAPI::Broodwar->self()->getUnits()) {
            if (drone->getType() == BWAPI::UnitTypes::Zerg_Drone && count < 3) {
                drone->gather(unit);
                count++;
            }
        }
    }
    if (unit->getType() == BWAPI::UnitTypes::Zerg_Spawning_Pool) {
        builtSpawningPool = true;
    }
    if (unit->getType() == BWAPI::UnitTypes::Zerg_Creep_Colony) {
        unit->morph(BWAPI::UnitTypes::Zerg_Sunken_Colony);
    }
    if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery) {
        BasesTools::SetOurBasePosition();
    }
}