#include "HiveTech.h"
#include "../Tools.h"
#include "../MatchLog.h"
#include "../CombatPolicy.h"
#include "../../../visualstudio/BasesTools.h"
#include "../micro.h"
#include <random>
#include <algorithm>

HiveTech& HiveTech::Instance() {
    static HiveTech instance;
    return instance;
}

void HiveTech::onStart() {
    Micro::ResetCombatState();
    Micro::SetMode(Micro::MicroMode::Defensive);
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
    m_attacking = false;
    m_pressureWave.clear();
    m_reserveMinerals = m_reserveGas = 0;
    
    DetermineSubStrategy();
}

void HiveTech::DetermineSubStrategy() {
    currentSubStrategy = m_airBuild ? SubStrategy::Mutalisk : SubStrategy::HydraliskLurker;
    BWAPI::Broodwar->printf(m_airBuild ? "MutaHive: Mutalisk / Queen / Greater Spire" : "HiveTech: Hydra / Lurker / Queen");
}

void HiveTech::Execute() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    
    // Refresh observed state so cancelled or destroyed buildings can be retried.
    builtSpawningPool = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool, myUnits, true) > 0;
    builtExtractor = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Extractor, myUnits, true) > 0;
    builtLair = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Lair, myUnits, true) > 0;
    builtSpire = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Spire, myUnits, true) > 0;
    builtHydraliskDen = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Hydralisk_Den, myUnits, true) > 0;
    builtQueensNest = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Queens_Nest, myUnits, true) > 0;
    builtHive = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Hive, myUnits, true) > 0;
    builtGreaterSpire = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Greater_Spire, myUnits, true) > 0;
    int bases = 0;
    const int gasWorkers = BWAPI::Broodwar->self()->gas() > 600 &&
        BWAPI::Broodwar->self()->gas() > BWAPI::Broodwar->self()->minerals() ? 1 : 3;
    for (auto unit : myUnits) {
        if (unit->getType().isResourceDepot()) ++bases;
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Extractor && unit->isCompleted()) Tools::GatherGas(unit, gasWorkers);
    }
    const int miningSites = BasesTools::CountMiningSites();
    hasNatural = miningSites >= 2;
    BasesTools::SetOurBasePosition();

    const int drones = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Drone, myUnits, true);
    const int hydras = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk);
    const int mutas = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Mutalisk);
    const auto threats = Micro::GetBaseThreats();
    int armySupply = 0, threatSupply = 0;
    for (auto unit : myUnits) {
        if (unit->isCompleted() && !unit->getType().isWorker() && !unit->getType().isBuilding() && unit->getType().canAttack())
            armySupply += unit->getType().supplyRequired();
    }
    for (auto threat : threats) threatSupply += std::max(2, threat->getType().supplyRequired());
    const bool emergency = !threats.empty() && armySupply < threatSupply * 2 + 8;
    m_reserveMinerals = m_reserveGas = 0;
    std::string decision = emergency ? "emergency_defense" : "army_production";

    const int supplyBuffer = currentPhase == Phase::SafeOpener ? 4 : std::min(24, bases * 8);
    if (BWAPI::Broodwar->self()->supplyTotal() < 400 &&
        Tools::GetTotalSupply(true) - BWAPI::Broodwar->self()->supplyUsed() < supplyBuffer)
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Overlord);

    // Recover the production chain after losing tech instead of waiting forever.
    if (!builtSpawningPool) currentPhase = Phase::SafeOpener;
    else if (!m_airBuild && currentPhase != Phase::SafeOpener && !builtHydraliskDen)
        currentPhase = Phase::TechToLair;
    else if (currentPhase != Phase::SafeOpener && !builtLair && !builtHive)
        currentPhase = Phase::TechToLair;
    else if (m_airBuild && currentPhase != Phase::SafeOpener && !builtSpire && !builtGreaterSpire)
        currentPhase = Phase::TechToLair;

    if (!emergency || Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0) {
        switch (currentPhase) {
            case Phase::SafeOpener: ExecuteSafeOpener(); break;
            case Phase::TechToLair: if (m_airBuild) ExecuteAirTech(); else ExecuteTechToLair(); break;
            case Phase::LairHarass: if (m_airBuild) ExecuteAirHarass(); else ExecuteLairHarass(); break;
            case Phase::TechToHive: ExecuteTechToHive(); break;
            case Phase::HiveAssault: if (m_airBuild) ExecuteAirAssault(); else ExecuteHiveAssault(); break;
        }
    } else if (m_airBuild && !builtSpire && !builtGreaterSpire && drones >= 12) {
        ExecuteAirTech();
    } else if (!m_airBuild && !builtHydraliskDen && drones >= 12) {
        // Persistent air pressure must not lock us into producing only Zerglings.
        m_reserveMinerals = 100; m_reserveGas = 50;
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hydralisk_Den, 1, BWAPI::Broodwar->self()->getStartLocation());
    }

    // Support has its own reservation so army production cannot starve Queens/spells.
    if (currentPhase != Phase::SafeOpener) MaintainQueenSupport();

    // Add local gas and Sunken defenses as the economy can support them.
    if (drones >= (emergency ? 12 : 16)) {
        for (auto depot : myUnits) {
            if (!depot->getType().isResourceDepot() || !depot->isCompleted()) continue;
            if (drones >= (m_airBuild ? 16 : 20) && BWAPI::Broodwar->self()->minerals() >= m_reserveMinerals + 50)
                Tools::EnsureBaseGas(depot);
            bool localThreat = false;
            for (auto threat : threats) {
                if (!threat->isFlying() && depot->getDistance(threat) < 384) localThreat = true;
            }
            if (BWAPI::Broodwar->self()->minerals() >= m_reserveMinerals + 125 &&
                (localThreat || (bases >= 2 && (hydras >= 4 || m_airBuild))))
                Tools::EnsureGroundDefense(depot, localThreat ? 2 : 1);
        }
    }

    if (!emergency && currentPhase != Phase::SafeOpener) {
        const int targetDrones = std::min(54, miningSites * 16);
        if (drones < targetDrones && (drones < 16 || armySupply >= drones) &&
            BWAPI::Broodwar->self()->minerals() >= m_reserveMinerals + 50) {
            if (Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone)) decision = "grow_workers";
        }
        const auto reserved = Tools::GetConstructionReserve();
        const bool surplus = CombatPolicy::SurplusExpansion(BWAPI::Broodwar->self()->minerals() -
            std::max(m_reserveMinerals, reserved.first), drones, armySupply);
        if (threats.empty() && ((miningSites < 4 && drones >= miningSites * 14 && armySupply >= 32) || surplus) &&
            !Tools::IsQueued(BWAPI::UnitTypes::Zerg_Hatchery).isValid()) {
            const auto expansion = BasesTools::GetNextExpansionPosition();
            if (expansion.isValid()) {
                m_reserveMinerals = std::max(m_reserveMinerals, 300);
                if (Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hatchery, 1, expansion) && surplus)
                    MatchLog::Event("surplus_expansion", "mining_sites=" + std::to_string(miningSites));
                decision = surplus ? "surplus_expand" : "expand";
            }
        }
        if (bases >= 3 && bases < 7 && BWAPI::Broodwar->self()->minerals() >= 600 &&
            Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Larva) < bases &&
            BWAPI::Broodwar->self()->supplyUsed() < 380) {
            if (Tools::BuildMacroHatchery()) decision = "increase_production";
        }
        if (m_airBuild && mutas >= 8 && m_reserveGas == 0 && BWAPI::Broodwar->self()->minerals() >= 200) {
            Tools::ResearchUpgrade(BWAPI::UpgradeTypes::Zerg_Flyer_Attacks);
            Tools::ResearchUpgrade(BWAPI::UpgradeTypes::Zerg_Flyer_Carapace);
        }
        if (!m_airBuild && hydras >= 8 && m_reserveGas == 0 && BWAPI::Broodwar->self()->minerals() >= 200) {
            if (BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines) == 0)
                Tools::ResearchUpgrade(BWAPI::UpgradeTypes::Grooved_Spines);
            else if (BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) == 0)
                Tools::ResearchUpgrade(BWAPI::UpgradeTypes::Muscular_Augments);
            else {
                Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Evolution_Chamber, 1, BWAPI::Broodwar->self()->getStartLocation());
                Tools::ResearchUpgrade(BWAPI::UpgradeTypes::Zerg_Missile_Attacks);
                Tools::ResearchUpgrade(BWAPI::UpgradeTypes::Zerg_Carapace);
            }
        }
    }
    if (Tools::IsQueued(BWAPI::UnitTypes::Zerg_Hatchery).isValid() && !emergency)
        m_reserveMinerals = std::max(m_reserveMinerals, 300);
    int supportSupply = 0;
    if (!emergency && builtQueensNest) {
        const int plannedHydras = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Hydralisk, myUnits, true) + (m_airBuild ? 0 : 1);
        const int groups = std::max(static_cast<int>(Micro::GetHydraGroups(myUnits).size()),
            (plannedHydras + CombatPolicy::HydrasPerGroup - 1) / CombatPolicy::HydrasPerGroup);
        const int airUnits = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Mutalisk, myUnits, true) +
            Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Guardian, myUnits, true) +
            Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Devourer, myUnits, true) + (m_airBuild ? 1 : 0);
        const int missing = std::max(0, CombatPolicy::QueenTarget(groups, airUnits) - Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Queen, myUnits, true));
        supportSupply = missing * BWAPI::UnitTypes::Zerg_Queen.supplyRequired();
    }
    if (currentPhase != Phase::SafeOpener || emergency)
        SpendArmyBudget(m_reserveMinerals, m_reserveGas, supportSupply);

    UpdatePressureWave(threats.empty(), armySupply);
    if (!m_pressureWave.empty()) decision = "surplus_pressure";

    // Hysteresis: launch a formed army, regroup after heavy losses.
    if (!m_attacking && !emergency && ((!m_pressureWave.empty()) || ((m_airBuild ? mutas >= 6 : hydras >= 12) && armySupply >= (m_airBuild ? 24 : 32)))) {
        m_attacking = true;
        Micro::SetMode(Micro::MicroMode::Aggressive);
        MatchLog::Event("attack", "army_ready");
    } else if (m_attacking && (armySupply < 16 || emergency)) {
        m_attacking = false;
        Micro::SetMode(Micro::MicroMode::Defensive);
        MatchLog::Event("regroup", emergency ? "base_emergency" : "army_depleted");
    }
    const char* phaseNames[] = {"SafeOpener", "TechToLair", "LairHarass", "TechToHive", "HiveAssault"};
    BWAPI::Broodwar->drawTextScreen(10, 10, "%s: %s | %s", GetName().c_str(), phaseNames[int(currentPhase)], decision.c_str());
    if (decision == "army_production" && (m_reserveMinerals > 0 || m_reserveGas > 0)) decision = "reserve_next_tech";
    const auto constructionReserve = Tools::GetConstructionReserve();
    MatchLog::Snapshot(phaseNames[int(currentPhase)], decision,
        std::max(m_reserveMinerals, constructionReserve.first), std::max(m_reserveGas, constructionReserve.second));
    Tools::BalanceMineralWorkers();
    Micro::HiveTechMicroLoop(myUnits, m_pressureWave);
}

void HiveTech::UpdatePressureWave(bool safe, int armySupply) {
    const auto units = BWAPI::Broodwar->self()->getUnits();
    const auto reserve = Tools::GetConstructionReserve();
    const int minerals = BWAPI::Broodwar->self()->minerals() - std::max(m_reserveMinerals, reserve.first);
    const bool production = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hatchery) +
        Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) + Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive) > 0;
    const auto canReplace = [](BWAPI::UnitType type) {
        return type == BWAPI::UnitTypes::Zerg_Zergling ? Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0 :
            type == BWAPI::UnitTypes::Zerg_Hydralisk ? Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0 :
            Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spire) + Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
    };
    if (!m_pressureWave.empty()) {
        BWAPI::Unitset surviving;
        for (auto unit : m_pressureWave)
            if (units.contains(unit) && unit->exists() && !unit->isMorphing()) surviving.insert(unit);
        int replacementMinerals = 0, replacementGas = 0;
        bool replacementTech = true;
        for (auto unit : surviving) {
            replacementTech = replacementTech && canReplace(unit->getType());
            replacementMinerals += unit->getType().mineralPrice();
            replacementGas += unit->getType().gasPrice();
        }
        const bool affordable = replacementTech && minerals >= replacementMinerals &&
            BWAPI::Broodwar->self()->gas() - std::max(m_reserveGas, reserve.second) >= replacementGas;
        const bool allowed = affordable && CombatPolicy::PressureAllowed(true, BWAPI::Broodwar->self()->supplyUsed(), minerals, safe, production);
        if (!allowed || surviving.empty()) {
            MatchLog::Event("pressure_end", !safe ? "base_threat" : surviving.empty() ? "wave_spent" : "rebuild_or_low_bank");
            m_pressureWave.clear();
        } else m_pressureWave = surviving;
        return;
    }
    if (BWAPI::Broodwar->self()->supplyTotal() < 400 || !CombatPolicy::PressureAllowed(false, BWAPI::Broodwar->self()->supplyUsed(), minerals, safe, production) ||
        Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Larva) == 0) return;
    int mineralBudget = minerals - 600;
    int gasBudget = std::max(0, BWAPI::Broodwar->self()->gas() - std::max(m_reserveGas, reserve.second) - 200);
    int supplyBudget = std::min(40, armySupply / 4);
    const int initialSupply = supplyBudget;
    int mutasRemaining = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Mutalisk);
    // Cheap attackers first; keep specialist units and a core Mutalisk flock.
    for (auto type : {BWAPI::UnitTypes::Zerg_Zergling, BWAPI::UnitTypes::Zerg_Hydralisk, BWAPI::UnitTypes::Zerg_Mutalisk}) {
        if (!canReplace(type)) continue;
        for (auto unit : units) {
            if (unit->getType() != type || !unit->isCompleted() || unit->isMorphing() || unit->isLoaded()) continue;
            if (type == BWAPI::UnitTypes::Zerg_Mutalisk && mutasRemaining <= 8) break;
            if (!CombatPolicy::BuyReplacement(type.mineralPrice(), type.gasPrice(), type.supplyRequired(),
                mineralBudget, gasBudget, supplyBudget)) continue;
            m_pressureWave.insert(unit);
            if (type == BWAPI::UnitTypes::Zerg_Mutalisk) --mutasRemaining;
        }
    }
    if (!m_pressureWave.empty()) MatchLog::Event("pressure_start", "units=" + std::to_string(m_pressureWave.size()) +
        " supply=" + std::to_string((initialSupply - supplyBudget) / 2) + " minerals=" + std::to_string(minerals));
}

void HiveTech::MaintainQueenSupport() {
    const auto units = BWAPI::Broodwar->self()->getUnits();
    const int airUnits = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Mutalisk, units, true) +
        Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Guardian, units, true) +
        Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Devourer, units, true);
    const int groups = static_cast<int>(Micro::GetHydraGroups(units).size());
    const int target = CombatPolicy::QueenTarget(groups, airUnits);
    if (target == 0 || (!builtLair && !builtHive)) return;
    if (!builtQueensNest) {
        m_reserveMinerals = std::max(m_reserveMinerals, 150);
        m_reserveGas = std::max(m_reserveGas, 100);
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Queens_Nest, 1, BWAPI::Broodwar->self()->getStartLocation());
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Queens_Nest) == 0) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Queen, units, true) < target) {
        m_reserveMinerals = std::max(m_reserveMinerals, 100);
        m_reserveGas = std::max(m_reserveGas, 100);
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Queen);
    }
    for (auto tech : {BWAPI::TechTypes::Ensnare, BWAPI::TechTypes::Spawn_Broodlings}) {
        if (BWAPI::Broodwar->self()->hasResearched(tech)) continue;
        if (!BWAPI::Broodwar->self()->isResearching(tech)) {
            m_reserveMinerals = std::max(m_reserveMinerals, tech.mineralPrice());
            m_reserveGas = std::max(m_reserveGas, tech.gasPrice());
            Tools::ResearchTech(tech);
        }
        break;
    }
}

void HiveTech::ExecuteAirTech() {
    const auto units = BWAPI::Broodwar->self()->getUnits();
    const auto start = BWAPI::Broodwar->self()->getStartLocation();
    if (!builtExtractor) {
        m_reserveMinerals = 50;
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Extractor, 1, start);
        return;
    }
    if (!builtLair && !builtHive) {
        m_reserveMinerals = 150; m_reserveGas = 100;
        for (auto depot : units) {
            if (depot->getType() == BWAPI::UnitTypes::Zerg_Hatchery && Tools::MorphUnit(depot, BWAPI::UnitTypes::Zerg_Lair)) break;
        }
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) == 0 && Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive) == 0) return;
    if (!builtSpire && !builtGreaterSpire) {
        m_reserveMinerals = 200; m_reserveGas = 150;
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spire, 1, start);
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spire) > 0 || Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0)
        currentPhase = builtHive ? Phase::HiveAssault : Phase::LairHarass;
}

void HiveTech::ExecuteAirHarass() {
    // Commit to Hive while the initial Mutalisk flock provides map pressure.
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Mutalisk) >= 8 &&
        Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Drone) >= 24 && hasNatural)
        currentPhase = Phase::TechToHive;
}

void HiveTech::ExecuteAirAssault() {
    const auto units = BWAPI::Broodwar->self()->getUnits();
    if (!builtGreaterSpire) {
        m_reserveMinerals = 100; m_reserveGas = 150;
        for (auto unit : units) {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Spire && Tools::MorphUnit(unit, BWAPI::UnitTypes::Zerg_Greater_Spire)) break;
        }
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0) return;
    int enemyAirSupply = 0;
    for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
        if (enemy->exists() && enemy->isVisible() && BWAPI::Broodwar->self()->isEnemy(enemy->getPlayer()) &&
            enemy->isFlying() && !enemy->getType().isBuilding() && enemy->getType().canAttack())
            enemyAirSupply += std::max(2, enemy->getType().supplyRequired());
    }
    const auto choice = CombatPolicy::NextAirMorph(Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Mutalisk),
        Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Guardian, units, true),
        Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Devourer, units, true), enemyAirSupply);
    if (choice == CombatPolicy::AirMorph::None) return;
    const auto type = choice == CombatPolicy::AirMorph::Guardian ? BWAPI::UnitTypes::Zerg_Guardian : BWAPI::UnitTypes::Zerg_Devourer;
    for (auto unit : units) {
        if (unit->getType() != BWAPI::UnitTypes::Zerg_Mutalisk || !unit->isCompleted() || unit->isMorphing() ||
            unit->isUnderAttack() || unit->getHitPoints() < unit->getType().maxHitPoints() / 2) continue;
        m_reserveMinerals = type.mineralPrice(); m_reserveGas = type.gasPrice();
        if (Tools::MorphUnit(unit, type)) MatchLog::Event("air_morph", type.getName());
        break;
    }
}

void HiveTech::SpendArmyBudget(int reserveMinerals, int reserveGas, int reserveSupply) {
    int supply = BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed() - reserveSupply;
    int minerals = BWAPI::Broodwar->self()->minerals() - reserveMinerals;
    int gas = BWAPI::Broodwar->self()->gas() - reserveGas;
    const auto myUnits = BWAPI::Broodwar->self()->getUnits();
    const bool canMakeMutas = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
        Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
    const bool canMakeHydras = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0;
    const bool canMakeLings = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
    int lings = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Zergling, myUnits, true);
    for (auto larva : myUnits) {
        if (larva->getType() != BWAPI::UnitTypes::Zerg_Larva) continue;
        BWAPI::UnitType type = BWAPI::UnitTypes::None;
        if (canMakeMutas && minerals >= 100 && gas >= 100)
            type = BWAPI::UnitTypes::Zerg_Mutalisk;
        else if (canMakeHydras && minerals >= 75 && gas >= 25)
            type = BWAPI::UnitTypes::Zerg_Hydralisk;
        else if (canMakeLings && minerals >= 50 && (!m_airBuild || lings < (canMakeMutas ? 12 : 24)))
            type = BWAPI::UnitTypes::Zerg_Zergling;
        const int requiredSupply = type.supplyRequired() * (type.isTwoUnitsInOneEgg() ? 2 : 1);
        if (type != BWAPI::UnitTypes::None && supply >= requiredSupply && Tools::MorphUnit(larva, type)) {
            supply -= requiredSupply;
            if (type == BWAPI::UnitTypes::Zerg_Zergling) lings += 2;
            minerals -= type.mineralPrice();
            gas -= type.gasPrice();
        }
    }
}

void HiveTech::ExecuteSafeOpener() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    const auto start = BWAPI::Broodwar->self()->getStartLocation();
    // Pool first: establish defense before committing a drone to the natural.
    if (!builtSpawningPool) {
        if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Drone, myUnits, true) < 9) {
            Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
        } else {
            Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spawning_Pool, 1, start);
        }
        return;
    }
    if (!builtExtractor) {
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Extractor, 1, start);
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0) {
        if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Drone, myUnits, true) < 12)
            Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
        return;
    }
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Zergling, myUnits, true) < zerglingsTarget) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Zergling);
        return;
    }
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Drone, myUnits, true) < 12) {
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
        return;
    }
    if (!hasNatural) {
        const auto expansion = BasesTools::GetNextExpansionPosition();
        if (expansion.isValid()) {
            Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hatchery, 1, expansion);
            return;
        }
        // Maps without an accessible expansion must still be able to tech.
    }
    currentPhase = Phase::TechToLair;
    BWAPI::Broodwar->printf("Phase: TechToLair");
}

void HiveTech::ExecuteTechToLair() {
    const auto myUnits = BWAPI::Broodwar->self()->getUnits();
    const auto start = BWAPI::Broodwar->self()->getStartLocation();
    if (!builtExtractor) {
        m_reserveMinerals = 50;
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Extractor, 1, start);
        return;
    }
    // Hydras are available before Lair: field six before paying for higher tech.
    if (!builtHydraliskDen) {
        m_reserveMinerals = 100; m_reserveGas = 50;
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hydralisk_Den, 1, start);
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk) < 6) return;
    if (!builtLair && !builtHive) {
        m_reserveMinerals = 150; m_reserveGas = 100;
        for (auto depot : myUnits) {
            if (depot->getType() == BWAPI::UnitTypes::Zerg_Hatchery && Tools::MorphUnit(depot, BWAPI::UnitTypes::Zerg_Lair)) break;
        }
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Lair) > 0 || Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive) > 0)
        currentPhase = Phase::LairHarass;
}

void HiveTech::ExecuteLairHarass() {
    const auto myUnits = BWAPI::Broodwar->self()->getUnits();
    const int hydras = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk);
    const int lurkers = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Lurker, myUnits, true);
    if (hydras >= 8 && !BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect) &&
        !BWAPI::Broodwar->self()->isResearching(BWAPI::TechTypes::Lurker_Aspect)) {
        m_reserveMinerals = 200; m_reserveGas = 200;
        Tools::ResearchTech(BWAPI::TechTypes::Lurker_Aspect);
    } else if (hydras >= 8 && lurkers < 4 && BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect)) {
        m_reserveMinerals = 50; m_reserveGas = 100;
        for (auto unit : myUnits) {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Hydralisk && !unit->isUnderAttack() &&
                Tools::MorphUnit(unit, BWAPI::UnitTypes::Zerg_Lurker)) break;
        }
    }
    int bases = 0;
    for (auto unit : myUnits) if (unit->getType().isResourceDepot() && unit->isCompleted()) ++bases;
    // Hive is a late upgrade, not a prerequisite for attacking.
    if (bases >= 3 && Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Drone) >= 36 && hydras >= 16 && lurkers >= 4)
        currentPhase = Phase::TechToHive;
}

void HiveTech::ExecuteTechToHive() {
    auto myUnits = BWAPI::Broodwar->self()->getUnits();
    
    // Reserve resources for the tech chain before producing more army.
    if (!builtQueensNest) {
        m_reserveMinerals = 150; m_reserveGas = 100;
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Queens_Nest, 1, BWAPI::Broodwar->self()->getStartLocation());
        return;
    }
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Queens_Nest) == 0) return;
    if (!builtHive) {
        m_reserveMinerals = 200; m_reserveGas = 150;
        for (auto unit : myUnits) {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Lair && Tools::MorphUnit(unit, BWAPI::UnitTypes::Zerg_Hive)) break;
        }
        return;
    }

    // Transition to Assault when Hive is done
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hive) > 0) {
        currentPhase = Phase::HiveAssault;
        BWAPI::Broodwar->printf("Phase: HiveAssault");
    }
}

void HiveTech::ExecuteHiveAssault() {
    // Sustain the proven ground composition and let the upgrade loop use Hive tech.
    const auto myUnits = BWAPI::Broodwar->self()->getUnits();
    if (Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk) < 12 ||
        Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Lurker, myUnits, true) >=
            std::clamp((Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Hydralisk) + 5) / 6, 4, 12) ||
        !BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect)) return;
    m_reserveMinerals = 50; m_reserveGas = 100;
    for (auto unit : myUnits) {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Hydralisk && !unit->isUnderAttack() &&
            Tools::MorphUnit(unit, BWAPI::UnitTypes::Zerg_Lurker)) break;
    }
}

void HiveTech::OnUnitCreate(BWAPI::Unit unit) {}
void HiveTech::onUnitComplete(BWAPI::Unit unit) {}
