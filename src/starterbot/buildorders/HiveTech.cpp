#include "HiveTech.h"
#include "../Tools.h"
#include "../../../visualstudio/BasesTools.h"
#include "../micro.h"
#include <random>

HiveTech& HiveTech::Instance() {
    static HiveTech instance;
    return instance;
}

void HiveTech::onStart() {
    Micro::SetMode(Micro::MicroMode::Neutral);
    BasesTools::SetOurBasePosition();
    
    builtSpawningPool = false;
    builtExtractor = false;
    builtLair = false;
    builtSpire = false;
    builtHydraliskDen = false;
    builtQueensNest = false;
    builtHive = false;
    builtGreaterSpire = false;
    hasNatural = false;
    currentPhase = Phase::SafeOpener;
    
    DetermineSubStrategy();
}

void HiveTech::DetermineSubStrategy() {
    // 70% Mutalisk, 30% Hydra/Lurker
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);
    if (dist(rng) <= 70) {
        currentSubStrategy = SubStrategy::Mutalisk;
        BWAPI::Broodwar->printf("HiveTech: Going Mutalisk tech path");
    } else {
        currentSubStrategy = SubStrategy::HydraliskLurker;
        BWAPI::Broodwar->printf("HiveTech: Going Hydra/Lurker tech path");
    }
}

void HiveTech::Execute() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    
    // UI drawing for current state
    std::string phaseName = "";
    switch (currentPhase) {
        case Phase::SafeOpener: phaseName = "SafeOpener"; break;
        case Phase::TechToLair: phaseName = "TechToLair"; break;
        case Phase::LairHarass: phaseName = "LairHarass"; break;
        case Phase::TechToHive: phaseName = "TechToHive"; break;
        case Phase::HiveAssault: phaseName = "HiveAssault"; break;
    }
    std::string strategyName = (currentSubStrategy == SubStrategy::Mutalisk) ? "Mutalisk" : "Hydra/Lurker";
    
    BWAPI::Broodwar->drawTextScreen(10, 10, "Build Order: HiveTech");
    BWAPI::Broodwar->drawTextScreen(10, 20, "SubStrategy: %s", strategyName.c_str());
    BWAPI::Broodwar->drawTextScreen(10, 30, "Phase: %s", phaseName.c_str());

    switch (currentPhase) {
        case Phase::SafeOpener:   ExecuteSafeOpener(); break;
        case Phase::TechToLair:   ExecuteTechToLair(); break;
        case Phase::LairHarass:   ExecuteLairHarass(); break;
        case Phase::TechToHive:   ExecuteTechToHive(); break;
        case Phase::HiveAssault:  ExecuteHiveAssault(); break;
    }
    
    // Call the custom HiveTech micro loop instead of the basic one
    Micro::HiveTechMicroLoop(myUnits);
}

void HiveTech::ExecuteSafeOpener() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    int supply = BWAPI::Broodwar->self()->supplyUsed();
    
    // Standard 12 Hatch / 11 Pool safe opening
    if (!hasNatural) {
        if (supply < 24) { // 12 drones
            Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
        } else if (BWAPI::Broodwar->self()->minerals() >= 300) {
            hasNatural = Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hatchery, 0, BasesTools::GetNextExpansionPosition());
        }
    } else if (hasNatural && !builtSpawningPool) {
        if (BWAPI::Broodwar->self()->minerals() >= 200) {
            builtSpawningPool = Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spawning_Pool, 1, BWAPI::Broodwar->self()->getStartLocation());
        } else {
            Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
        }
    } else if (builtSpawningPool && !builtExtractor) {
        if (BWAPI::Broodwar->self()->minerals() >= 50) {
            builtExtractor = Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Extractor, 1, BWAPI::Broodwar->self()->getStartLocation());
        }
    } else if (builtSpawningPool && Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool, myUnits, true) > 0) {
        // Build defensive zerglings
        int lingCount = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Zergling, myUnits, true);
        if (lingCount < zerglingsTarget) {
            Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Zergling);
        } else {
            // Move to tech phase once we have basic defense
            currentPhase = Phase::TechToLair;
            BWAPI::Broodwar->printf("Phase: TechToLair");
        }
    }
}

void HiveTech::ExecuteTechToLair() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    
    // Maintain minimum defense
    int lingCount = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Zergling, myUnits, true);
    if (lingCount < zerglingsTarget) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Zergling);
    }
    
    // Need gas
    if (BWAPI::Broodwar->self()->gas() >= 150 && BWAPI::Broodwar->self()->minerals() >= 150) {
        if (!builtLair && Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) == 0) {
            for (auto unit : myUnits) {
                if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery) {
                    unit->morph(BWAPI::UnitTypes::Zerg_Lair);
                    builtLair = true;
                    break;
                }
            }
        }
    }
    
    // Build Drones while waiting for Lair
    if (builtLair && Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) == 0) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
    }
    
    // Once Lair is done, start teching based on substrategy
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) > 0) {
        if (currentSubStrategy == SubStrategy::Mutalisk) {
            if (!builtSpire) {
                builtSpire = Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spire, 1, BWAPI::Broodwar->self()->getStartLocation());
            } else if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spire) > 0) {
                currentPhase = Phase::LairHarass;
                BWAPI::Broodwar->printf("Phase: LairHarass (Mutalisk)");
            }
        } else {
            if (!builtHydraliskDen) {
                builtHydraliskDen = Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hydralisk_Den, 1, BWAPI::Broodwar->self()->getStartLocation());
            } else if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0) {
                // Research lurker aspect
                for(auto den : myUnits) {
                    if (den->getType() == BWAPI::UnitTypes::Zerg_Hydralisk_Den && den->isCompleted()) {
                        if (!BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect) && !den->isResearching()) {
                            den->research(BWAPI::TechTypes::Lurker_Aspect);
                        }
                    }
                }
                if (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect)) {
                    currentPhase = Phase::LairHarass;
                    BWAPI::Broodwar->printf("Phase: LairHarass (Hydra/Lurker)");
                }
            }
        }
    }
}

void HiveTech::ExecuteLairHarass() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    
    // Maintain economy
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Drone, myUnits, true) < 30) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
    }
    
    // Produce army
    if (currentSubStrategy == SubStrategy::Mutalisk) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Mutalisk);
        
        // Transition to Hive when we have a good flock
        if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Mutalisk, myUnits, true) >= 9) {
            currentPhase = Phase::TechToHive;
            BWAPI::Broodwar->printf("Phase: TechToHive");
        }
    } else {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Hydralisk);
        // Morph Lurkers if possible
        if (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect)) {
            for (auto u : myUnits) {
                if (u->getType() == BWAPI::UnitTypes::Zerg_Hydralisk && !u->isMorphing() && BWAPI::Broodwar->self()->gas() >= 125) {
                    u->morph(BWAPI::UnitTypes::Zerg_Lurker);
                }
            }
        }
        
        // Transition to Hive
        if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Lurker, myUnits, true) >= 4) {
            currentPhase = Phase::TechToHive;
            BWAPI::Broodwar->printf("Phase: TechToHive");
        }
    }
}

void HiveTech::ExecuteTechToHive() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    
    // Keep producing army
    if (currentSubStrategy == SubStrategy::Mutalisk) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Mutalisk);
    } else {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Hydralisk);
    }
    
    // Build Queen's Nest
    if (!builtQueensNest) {
        builtQueensNest = Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Queens_Nest, 1, BWAPI::Broodwar->self()->getStartLocation());
    }
    
    // Morph Hive
    if (builtQueensNest && Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0) {
        if (!builtHive && Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive) == 0) {
            for (auto unit : myUnits) {
                if (unit->getType() == BWAPI::UnitTypes::Zerg_Lair && unit->isCompleted()) {
                    unit->morph(BWAPI::UnitTypes::Zerg_Hive);
                    builtHive = true;
                    break;
                }
            }
        }
    }
    
    // Transition to Assault when Hive is done
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive) > 0) {
        currentPhase = Phase::HiveAssault;
        BWAPI::Broodwar->printf("Phase: HiveAssault");
    }
}

void HiveTech::ExecuteHiveAssault() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    
    // 1. Build Greater Spire
    if (!builtGreaterSpire) {
        for (auto unit : myUnits) {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Spire && unit->isCompleted()) {
                unit->morph(BWAPI::UnitTypes::Zerg_Greater_Spire);
                builtGreaterSpire = true;
                break;
            }
        }
    }
    
    // 2. Produce Queens (up to 3)
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Queen, myUnits, true) < 3) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Queen);
    }
    
    // 3. Research Queen spells
    for (auto nest : myUnits) {
        if (nest->getType() == BWAPI::UnitTypes::Zerg_Queens_Nest && nest->isCompleted()) {
            if (!BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Ensnare) && !nest->isResearching()) {
                nest->research(BWAPI::TechTypes::Ensnare);
            } else if (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Ensnare) && 
                       !BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Spawn_Broodlings) && 
                       !nest->isResearching()) {
                nest->research(BWAPI::TechTypes::Spawn_Broodlings);
            }
        }
    }
    
    // 4. End-game army composition
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0) {
        // Keep Mutalisks pumping to feed morphs
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Mutalisk);
        
        int guardians = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Guardian, myUnits, true);
        int devourers = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Devourer, myUnits, true);
        
        // Matchup based morphing ratios
        std::string opponentRace = "Unknown";
        for (auto p : BWAPI::Broodwar->getPlayers()) {
            if (p != BWAPI::Broodwar->self() && p != BWAPI::Broodwar->neutral()) {
                opponentRace = p->getRace().getName();
                break;
            }
        }
        
        int targetDevourers = (opponentRace == "Zerg") ? 5 : 2; // More devourers vs Zerg
        int targetGuardians = (opponentRace == "Terran") ? 8 : 6;
        
        for (auto u : myUnits) {
            if (u->getType() == BWAPI::UnitTypes::Zerg_Mutalisk && !u->isMorphing() && u->isCompleted()) {
                if (devourers < targetDevourers && BWAPI::Broodwar->self()->minerals() >= 150 && BWAPI::Broodwar->self()->gas() >= 50) {
                    u->morph(BWAPI::UnitTypes::Zerg_Devourer);
                    devourers++;
                } else if (guardians < targetGuardians && BWAPI::Broodwar->self()->minerals() >= 50 && BWAPI::Broodwar->self()->gas() >= 100) {
                    u->morph(BWAPI::UnitTypes::Zerg_Guardian);
                    guardians++;
                }
            }
        }
    } else if (currentSubStrategy == SubStrategy::HydraliskLurker) {
        // If we didn't get Spire, just pump Hydra/Lurker
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Hydralisk);
        if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Lurker, myUnits, true) < 8) {
            for (auto u : myUnits) {
                if (u->getType() == BWAPI::UnitTypes::Zerg_Hydralisk && !u->isMorphing() && BWAPI::Broodwar->self()->gas() >= 125) {
                    u->morph(BWAPI::UnitTypes::Zerg_Lurker);
                }
            }
        }
    }
}

void HiveTech::OnUnitCreate(BWAPI::Unit unit) {}
void HiveTech::onUnitComplete(BWAPI::Unit unit) {}
