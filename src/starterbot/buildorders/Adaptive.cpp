#include "Adaptive.h"
#include "../Tools.h"
#include "../MatchLog.h"
#include "../CombatPolicy.h"
#include "../../../visualstudio/BasesTools.h"
#include "../micro.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using Learning::Army;
using Learning::Composition;
using Learning::Feature;
using Learning::Gene;

namespace {
    constexpr int FramesPerSecond = 24;
    constexpr int EvaluationInterval = FramesPerSecond * 10;
    constexpr int SpareBuilders = 2;      // Drones beyond full saturation, kept free to expand or build.
    constexpr int ExcessMinerals = 400;   // Banked minerals above this are dumped into supply, lings and structures.

    const BWAPI::UnitType armyTypes[Learning::ArmyCount] = {
        BWAPI::UnitTypes::Zerg_Zergling, BWAPI::UnitTypes::Zerg_Hydralisk, BWAPI::UnitTypes::Zerg_Mutalisk,
        BWAPI::UnitTypes::Zerg_Guardian, BWAPI::UnitTypes::Zerg_Devourer, BWAPI::UnitTypes::Zerg_Lurker,
        BWAPI::UnitTypes::Zerg_Ultralisk
    };

    BWAPI::UnitType ArmyType(Army army) { return armyTypes[static_cast<int>(army)]; }

    // Completed and not mid-morph (a Lair morphing into a Hive cannot build Hive tech yet).
    int Ready(BWAPI::UnitType type) {
        int count = 0;
        for (auto unit : BWAPI::Broodwar->self()->getUnits())
            if (unit->getType() == type && unit->isCompleted() && !unit->isMorphing()) ++count;
        return count;
    }

    int Started(BWAPI::UnitType type) {
        return Tools::CountUnitsOfType(type, BWAPI::Broodwar->self()->getUnits(), true);
    }

    bool MorphFirst(BWAPI::UnitType from, BWAPI::UnitType to) {
        for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
            if (unit->getType() == from && unit->isCompleted() && !unit->isMorphing() && Tools::MorphUnit(unit, to)) return true;
        }
        return false;
    }

    std::string LearningDirectory() {
        std::error_code error;
        auto directory = std::filesystem::current_path(error);
#ifdef _WIN32
        wchar_t modulePath[32768] = {};
        if (GetModuleFileNameW(nullptr, modulePath, 32768)) directory = std::filesystem::path(modulePath).parent_path();
#endif
        if (directory.filename() == "updated") directory = directory.parent_path();
        directory /= "learning";
        std::filesystem::create_directories(directory, error);
        return directory.string();
    }

    bool IsStaticDefense(BWAPI::UnitType type) {
        return type == BWAPI::UnitTypes::Protoss_Photon_Cannon || type == BWAPI::UnitTypes::Terran_Bunker ||
            type == BWAPI::UnitTypes::Terran_Missile_Turret || type == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
            type == BWAPI::UnitTypes::Zerg_Spore_Colony;
    }

    bool IsSplash(BWAPI::UnitType type) {
        return type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode || type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
            type == BWAPI::UnitTypes::Terran_Firebat || type == BWAPI::UnitTypes::Terran_Vulture ||
            type == BWAPI::UnitTypes::Protoss_Reaver || type == BWAPI::UnitTypes::Protoss_High_Templar ||
            type == BWAPI::UnitTypes::Protoss_Archon || type == BWAPI::UnitTypes::Zerg_Lurker ||
            type == BWAPI::UnitTypes::Zerg_Defiler;
    }

    bool IsAirSplash(BWAPI::UnitType type) {
        return type == BWAPI::UnitTypes::Protoss_Corsair || type == BWAPI::UnitTypes::Terran_Valkyrie ||
            type == BWAPI::UnitTypes::Protoss_Archon || type == BWAPI::UnitTypes::Protoss_High_Templar ||
            type == BWAPI::UnitTypes::Terran_Science_Vessel || type == BWAPI::UnitTypes::Zerg_Devourer;
    }

    // Minerals already committed by orders BWAPI has not deducted yet (they land after latency).
    int PendingOrderMinerals() {
        const int frame = BWAPI::Broodwar->getFrameCount();
        int minerals = 0;
        for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
            if (frame - unit->getLastCommandFrame() > BWAPI::Broodwar->getLatencyFrames()) continue;
            const auto command = unit->getLastCommand();
            const auto type = command.getType();
            if (type == BWAPI::UnitCommandTypes::Morph || type == BWAPI::UnitCommandTypes::Train)
                minerals += command.getUnitType().mineralPrice();
            else if (type == BWAPI::UnitCommandTypes::Research) minerals += command.getTechType().mineralPrice();
            else if (type == BWAPI::UnitCommandTypes::Upgrade) {
                const auto upgrade = command.getUpgradeType();
                minerals += upgrade.mineralPrice(BWAPI::Broodwar->self()->getUpgradeLevel(upgrade) + 1);
            }
        }
        return minerals;
    }

    bool IsCapitalAir(BWAPI::UnitType type) {
        return type == BWAPI::UnitTypes::Terran_Battlecruiser || type == BWAPI::UnitTypes::Protoss_Carrier ||
            type == BWAPI::UnitTypes::Protoss_Arbiter || type == BWAPI::UnitTypes::Zerg_Guardian;
    }
}

Adaptive& Adaptive::Instance() {
    static Adaptive instance;
    return instance;
}

void Adaptive::onStart() {
    Micro::ResetCombatState();
    Micro::SetMode(Micro::MicroMode::Defensive);
    BasesTools::SetOurBasePosition();

    m_enemyUnits.clear();
    m_attacking = m_rushLaunched = m_openerDone = m_locked = false;
    m_reserveMinerals = m_reserveGas = 0;
    m_lastSwitchFrame = m_lastEvaluationFrame = m_lastRecordFrame = 0;
    m_switches = 0;
    m_decision.clear();

    const auto enemy = BWAPI::Broodwar->enemy();
    m_enemyRace = enemy ? enemy->getRace().getName() : "Unknown";
    const char* learning = std::getenv("IKKRIUS_LEARNING");
    m_learning = !learning || std::string(learning) != "0";

    // One population per starting race so each race evolves its own timings.
    m_dataPath = LearningDirectory() + "/adaptive-" + m_enemyRace + ".txt";
    m_learner.Reset();
    if (!m_learner.Load(m_dataPath)) std::cout << "Adaptive: starting a new learning population at " << m_dataPath << "\n";
    m_genome = m_learner.BeginGame(m_learning);
    std::cout << "Adaptive: generation " << m_learner.GetPopulation().Generation() << " individual "
              << m_learner.CurrentIndividual() << " genome " << m_genome.Describe() << "\n";
    MatchLog::Event("genome", "generation=" + std::to_string(m_learner.GetPopulation().Generation()) +
        " individual=" + std::to_string(m_learner.CurrentIndividual()) + " " + m_genome.Describe());

    Composition forced;
    if (const char* requested = std::getenv("IKKRIUS_COMP"); requested && Learning::ParseComposition(requested, forced)) {
        m_locked = true;
        m_context = Learning::ContextKey(m_enemyRace, BuildProfile());
        SetComposition(forced, "forced");
    } else {
        EvaluateComposition(true);
    }
}

// ---------------------------------------------------------------------------------------------
// Scouting memory and enemy profile
// ---------------------------------------------------------------------------------------------

void Adaptive::RememberEnemy(BWAPI::Unit unit) {
    if (!unit || !BWAPI::Broodwar->self()->isEnemy(unit->getPlayer())) return;
    m_enemyUnits[unit->getID()] = unit->getType();
    const auto race = unit->getType().getRace();
    if (race == BWAPI::Races::Terran || race == BWAPI::Races::Protoss || race == BWAPI::Races::Zerg)
        m_enemyRace = race.getName();
}

void Adaptive::onUnitShow(BWAPI::Unit unit) { RememberEnemy(unit); }
void Adaptive::onUnitMorph(BWAPI::Unit unit) { RememberEnemy(unit); }
void Adaptive::onUnitDestroy(BWAPI::Unit unit) { if (unit) m_enemyUnits.erase(unit->getID()); }

Learning::EnemyProfile Adaptive::BuildProfile() const {
    Learning::EnemyProfile profile;
    double army = 0, staticDefense = 0, detection = 0;
    for (const auto& [id, type] : m_enemyUnits) {
        if (IsStaticDefense(type)) staticDefense += 1;
        if (type.isDetector() || type == BWAPI::UnitTypes::Terran_Comsat_Station)
            detection += type == BWAPI::UnitTypes::Zerg_Overlord ? 0.25 : 1.0;
        if (type.isBuilding() || type.isWorker() || type == BWAPI::UnitTypes::Zerg_Overlord ||
            type == BWAPI::UnitTypes::Zerg_Larva || type == BWAPI::UnitTypes::Zerg_Egg) continue;
        const bool combat = type.canAttack() || type.isSpellcaster() || type == BWAPI::UnitTypes::Protoss_Carrier ||
            type == BWAPI::UnitTypes::Protoss_Reaver;
        if (!combat) continue;
        const double supply = std::max(1, type.supplyRequired());
        army += supply;
        if (type.isFlyer()) profile[Feature::Air] += supply;
        if (type.airWeapon() != BWAPI::WeaponTypes::None || type == BWAPI::UnitTypes::Protoss_High_Templar ||
            type == BWAPI::UnitTypes::Protoss_Carrier) profile[Feature::AntiAir] += supply;
        if (IsSplash(type)) profile[Feature::Splash] += supply;
        if (IsAirSplash(type)) profile[Feature::AirSplash] += supply;
        if (!type.isFlyer() && type.size() == BWAPI::UnitSizeTypes::Large) profile[Feature::Heavy] += supply;
        if (!type.isFlyer() && type.size() == BWAPI::UnitSizeTypes::Small) profile[Feature::Small] += supply;
        if (IsCapitalAir(type)) profile[Feature::CapitalAir] += supply;
    }
    if (army > 0) {
        for (auto feature : { Feature::Air, Feature::AntiAir, Feature::Splash, Feature::AirSplash,
                              Feature::Heavy, Feature::Small, Feature::CapitalAir })
            profile[feature] /= army;
    }
    profile[Feature::StaticDefense] = std::min(1.0, staticDefense / 4.0);
    profile[Feature::Detection] = std::min(1.0, detection / 3.0);
    const double minutes = BWAPI::Broodwar->getFrameCount() / double(FramesPerSecond * 60);
    profile[Feature::Early] = std::clamp(1.0 - (minutes - 5.0) / 5.0, 0.0, 1.0);
    return profile;
}

// ---------------------------------------------------------------------------------------------
// Composition choice and switching
// ---------------------------------------------------------------------------------------------

bool Adaptive::Needs(Composition composition, Tech tech) {
    const auto& spec = Learning::Spec(composition);
    const bool air = spec.Share(Army::Mutalisk) + spec.Share(Army::Guardian) + spec.Share(Army::Devourer) > 0;
    const bool hive = spec.Share(Army::Guardian) + spec.Share(Army::Devourer) + spec.Share(Army::Ultralisk) > 0;
    switch (tech) {
        case Tech::Extractor: return true;
        case Tech::HydraliskDen: return spec.Share(Army::Hydralisk) + spec.Share(Army::Lurker) > 0;
        case Tech::Lair: return spec.queens || air || hive || spec.Share(Army::Lurker) > 0;
        case Tech::Spire: return air;
        case Tech::QueensNest: return spec.queens || hive;
        case Tech::Hive: return hive;
        case Tech::GreaterSpire: return spec.Share(Army::Guardian) + spec.Share(Army::Devourer) > 0;
        case Tech::UltraliskCavern: return spec.Share(Army::Ultralisk) > 0;
        default: return false;
    }
}

bool Adaptive::HasTech(Tech tech, bool completed) const {
    const auto has = [completed](BWAPI::UnitType type) { return (completed ? Ready(type) : Started(type)) > 0; };
    switch (tech) {
        case Tech::Extractor: return has(BWAPI::UnitTypes::Zerg_Extractor);
        case Tech::HydraliskDen: return has(BWAPI::UnitTypes::Zerg_Hydralisk_Den);
        case Tech::Lair: return has(BWAPI::UnitTypes::Zerg_Lair) || has(BWAPI::UnitTypes::Zerg_Hive) ||
            (completed && Started(BWAPI::UnitTypes::Zerg_Hive) > 0);
        case Tech::Spire: return has(BWAPI::UnitTypes::Zerg_Spire) || has(BWAPI::UnitTypes::Zerg_Greater_Spire) ||
            (completed && Started(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0);
        case Tech::QueensNest: return has(BWAPI::UnitTypes::Zerg_Queens_Nest);
        case Tech::Hive: return has(BWAPI::UnitTypes::Zerg_Hive);
        case Tech::GreaterSpire: return has(BWAPI::UnitTypes::Zerg_Greater_Spire);
        case Tech::UltraliskCavern: return has(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern);
        case Tech::EvolutionChamber: return has(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
        default: return false;
    }
}

std::array<double, Learning::CompositionCount> Adaptive::OwnedTech() const {
    std::array<bool, static_cast<int>(Tech::Count)> owned{};
    for (int t = 0; t < static_cast<int>(Tech::Count); ++t) owned[t] = HasTech(static_cast<Tech>(t), false);
    std::array<double, Learning::CompositionCount> result{};
    for (int c = 0; c < Learning::CompositionCount; ++c) {
        int needed = 0, have = 0;
        for (int t = 0; t < static_cast<int>(Tech::Count); ++t) {
            if (!Needs(static_cast<Composition>(c), static_cast<Tech>(t))) continue;
            ++needed;
            if (owned[t]) ++have;
        }
        result[c] = needed ? double(have) / needed : 1.0;
    }
    return result;
}

void Adaptive::RecordActiveTime() {
    const int frame = BWAPI::Broodwar->getFrameCount();
    m_learner.RecordActive(m_context, m_composition, (frame - m_lastRecordFrame) / double(FramesPerSecond));
    m_lastRecordFrame = frame;
}

void Adaptive::SetComposition(Composition composition, const std::string& reason) {
    const bool changed = composition != m_composition || m_lastSwitchFrame == 0;
    m_composition = composition;
    m_lastSwitchFrame = std::max(1, BWAPI::Broodwar->getFrameCount());
    if (!changed) return;
    ++m_switches;
    BWAPI::Broodwar->printf("Adaptive: %s (%s)", Learning::CompositionName(composition), reason.c_str());
    std::cout << "Adaptive composition " << Learning::CompositionName(composition) << " (" << reason << ")\n";
    MatchLog::Event("composition", std::string(Learning::CompositionName(composition)) + " reason=" + reason +
        " context=" + std::to_string(m_context));
}

void Adaptive::EvaluateComposition(bool opening) {
    for (auto unit : BWAPI::Broodwar->getAllUnits()) if (unit->isVisible()) RememberEnemy(unit);
    const auto profile = BuildProfile();
    // Credit time spent to the context it was spent in before the context changes.
    if (!opening) RecordActiveTime();
    m_context = Learning::ContextKey(m_enemyRace, profile);
    m_lastEvaluationFrame = BWAPI::Broodwar->getFrameCount();
    if (m_locked) return;
    const double sinceSwitch = (BWAPI::Broodwar->getFrameCount() - m_lastSwitchFrame) / double(FramesPerSecond);
    const auto decision = m_learner.Choose(m_context, profile, OwnedTech(), !opening, m_composition, sinceSwitch, opening);
    if (opening || decision.switched) SetComposition(decision.composition, opening ? "opening" : "enemy_tech");
}

bool Adaptive::RushPending() const {
    return Learning::Spec(m_composition).rush && !m_rushLaunched;
}

// Rush compositions compress every economic gate under the rush drone cap.
int Adaptive::Gate(Gene gene) const {
    const int value = m_genome.GetInt(gene);
    return RushPending() ? std::min(value, m_genome.GetInt(Gene::RushDroneCap)) : value;
}

// ---------------------------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------------------------

Adaptive::Counts Adaptive::Count() const {
    Counts counts;
    const auto units = BWAPI::Broodwar->self()->getUnits();
    counts.drones = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Drone, units, true);
    counts.queens = Tools::CountUnitsOfType(BWAPI::UnitTypes::Zerg_Queen, units, true);
    counts.miningSites = BasesTools::CountMiningSites();
    for (int a = 0; a < Learning::ArmyCount; ++a) counts.army[a] = Tools::CountUnitsOfType(armyTypes[a], units, true);
    for (auto unit : units) {
        const auto type = unit->getType();
        if (type.isResourceDepot()) ++counts.bases;
        if (type == BWAPI::UnitTypes::Zerg_Larva) ++counts.larva;
        if (unit->isCompleted() && !type.isWorker() && !type.isBuilding() && type.canAttack())
            counts.armySupply += type.supplyRequired();
    }
    return counts;
}

void Adaptive::Execute() {
    const auto myUnits = BWAPI::Broodwar->self()->getUnits();
    const int frame = BWAPI::Broodwar->getFrameCount();
    if (frame - m_lastEvaluationFrame >= EvaluationInterval) EvaluateComposition(false);

    const auto counts = Count();
    const int gasWorkers = BWAPI::Broodwar->self()->gas() > 600 &&
        BWAPI::Broodwar->self()->gas() > BWAPI::Broodwar->self()->minerals() ? 1 : 3;
    for (auto unit : myUnits)
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Extractor && unit->isCompleted()) Tools::GatherGas(unit, gasWorkers);
    BasesTools::SetOurBasePosition();

    const auto threats = Micro::GetBaseThreats();
    int threatSupply = 0;
    for (auto threat : threats) threatSupply += std::max(2, threat->getType().supplyRequired());
    const bool emergency = !threats.empty() && counts.armySupply < threatSupply * 2 + 8;
    m_reserveMinerals = m_reserveGas = 0;
    m_decision = emergency ? "emergency_defense" : "army_production";

    const bool poolStarted = Started(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
    const int supplyBuffer = !poolStarted ? 4 : std::min(24, std::max(1, counts.bases) * 8);
    if (BWAPI::Broodwar->self()->supplyTotal() < 400 &&
        Tools::GetTotalSupply(true) - BWAPI::Broodwar->self()->supplyUsed() < supplyBuffer)
        Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Overlord);

    Opener(counts);
    if (poolStarted) {
        if (!emergency) AdvanceTech(counts);
        Economy(counts, emergency);
        if (!emergency) QueenSupport(counts);
        if (!emergency) Upgrades(counts);
        MorphAdvancedUnits(counts);
        SpendArmyBudget(counts);
        SpendExcessMinerals(counts, emergency);

        // Sunkens only where ground threats are actually hitting a base.
        for (auto depot : myUnits) {
            if (!depot->getType().isResourceDepot() || !depot->isCompleted() || counts.drones < 10) continue;
            for (auto threat : threats) {
                if (!threat->isFlying() && depot->getDistance(threat) < 384 &&
                    BWAPI::Broodwar->self()->minerals() >= m_reserveMinerals + 125) {
                    Tools::EnsureGroundDefense(depot, 2);
                    break;
                }
            }
        }
    }
    ManageAttack(counts, emergency);

    const auto& spec = Learning::Spec(m_composition);
    BWAPI::Broodwar->drawTextScreen(10, 10, "%s: %s%s | %s | gen %d #%d | switches %d", GetName().c_str(),
        Learning::CompositionName(m_composition), spec.rush ? (m_rushLaunched ? " (rush sent)" : " (rush)") : "",
        m_decision.c_str(), m_learner.GetPopulation().Generation(), m_learner.CurrentIndividual(), m_switches - 1);
    const auto constructionReserve = Tools::GetConstructionReserve();
    MatchLog::Snapshot(Learning::CompositionName(m_composition), m_decision,
        std::max(m_reserveMinerals, constructionReserve.first), std::max(m_reserveGas, constructionReserve.second));
    Tools::BalanceMineralWorkers();
    Micro::HiveTechMicroLoop(myUnits);
}

void Adaptive::Opener(const Counts& counts) {
    const auto start = BWAPI::Broodwar->self()->getStartLocation();
    if (Started(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0) {
        if (counts.drones < m_genome.GetInt(Gene::PoolDrones)) Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone);
        else {
            m_reserveMinerals = 200;
            m_decision = "pool";
            Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spawning_Pool, 1, start);
        }
        return;
    }
    if (m_openerDone || Ready(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0) return;
    if (counts.army[static_cast<int>(Army::Zergling)] >= m_genome.GetInt(Gene::OpeningLings)) {
        m_openerDone = true;
        return;
    }
    m_decision = "opening_lings";
    Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Zergling);
}

void Adaptive::AdvanceTech(const Counts& counts) {
    const auto start = BWAPI::Broodwar->self()->getStartLocation();
    const auto& spec = Learning::Spec(m_composition);
    const bool poolReady = Ready(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
    const auto self = BWAPI::Broodwar->self();
    const auto want = [this](Tech tech) { return Needs(m_composition, tech) && !HasTech(tech, false); };
    const auto reserve = [this](int minerals, int gas, const char* step) {
        m_reserveMinerals = std::max(m_reserveMinerals, minerals);
        m_reserveGas = std::max(m_reserveGas, gas);
        m_decision = step;
    };
    const auto speed = [&]() {
        const auto upgrade = BWAPI::UpgradeTypes::Metabolic_Boost;
        if (spec.Share(Army::Zergling) <= 0 || self->getUpgradeLevel(upgrade) > 0 || self->isUpgrading(upgrade) ||
            !poolReady || !HasTech(Tech::Extractor, true)) return false;
        reserve(100, 100, "tech_ling_speed");
        Tools::ResearchUpgrade(upgrade);
        return true;
    };

    // One tech step at a time, in dependency order; the first due step reserves its cost.
    if (want(Tech::Extractor) && counts.drones >= Gate(Gene::GasDrones)) {
        reserve(50, 0, "tech_extractor");
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Extractor, 1, start);
        return;
    }
    if (spec.Share(Army::Zergling) >= 0.5 && speed()) return;
    if (want(Tech::HydraliskDen) && poolReady && counts.drones >= Gate(Gene::DenDrones)) {
        reserve(100, 50, "tech_hydra_den");
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hydralisk_Den, 1, start);
        return;
    }
    if (want(Tech::Lair) && poolReady && counts.drones >= Gate(Gene::LairDrones)) {
        reserve(150, 100, "tech_lair");
        MorphFirst(BWAPI::UnitTypes::Zerg_Hatchery, BWAPI::UnitTypes::Zerg_Lair);
        return;
    }
    const bool lairReady = HasTech(Tech::Lair, true);
    if (want(Tech::Spire) && lairReady && counts.drones >= Gate(Gene::SpireDrones)) {
        reserve(200, 150, "tech_spire");
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Spire, 1, start);
        return;
    }
    if (speed()) return;
    const auto lurker = BWAPI::TechTypes::Lurker_Aspect;
    if (spec.Share(Army::Lurker) > 0 && lairReady && HasTech(Tech::HydraliskDen, true) &&
        !self->hasResearched(lurker) && !self->isResearching(lurker) &&
        counts.army[static_cast<int>(Army::Hydralisk)] >= m_genome.GetInt(Gene::LurkerHydras)) {
        reserve(200, 200, "tech_lurker");
        Tools::ResearchTech(lurker);
        return;
    }
    if (want(Tech::QueensNest) && lairReady && counts.drones >= Gate(Gene::QueensNestDrones)) {
        reserve(150, 100, "tech_queens_nest");
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Queens_Nest, 1, start);
        return;
    }
    if (want(Tech::Hive) && lairReady && HasTech(Tech::QueensNest, true) && counts.drones >= Gate(Gene::HiveDrones)) {
        reserve(200, 150, "tech_hive");
        MorphFirst(BWAPI::UnitTypes::Zerg_Lair, BWAPI::UnitTypes::Zerg_Hive);
        return;
    }
    const bool hiveReady = HasTech(Tech::Hive, true);
    if (want(Tech::GreaterSpire) && hiveReady && HasTech(Tech::Spire, true) &&
        counts.army[static_cast<int>(Army::Mutalisk)] >= m_genome.GetInt(Gene::GreaterSpireMutas)) {
        reserve(100, 150, "tech_greater_spire");
        MorphFirst(BWAPI::UnitTypes::Zerg_Spire, BWAPI::UnitTypes::Zerg_Greater_Spire);
        return;
    }
    if (want(Tech::UltraliskCavern) && hiveReady && counts.drones >= Gate(Gene::UltraCavernDrones)) {
        reserve(150, 200, "tech_ultralisk_cavern");
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern, 1, start);
        return;
    }
    const bool groundArmy = spec.Share(Army::Zergling) + spec.Share(Army::Hydralisk) +
        spec.Share(Army::Lurker) + spec.Share(Army::Ultralisk) > 0;
    if (groundArmy && !HasTech(Tech::EvolutionChamber, false) && counts.drones >= Gate(Gene::EvoDrones) &&
        counts.armySupply / 2 >= m_genome.GetInt(Gene::UpgradeArmySupply)) {
        reserve(75, 0, "tech_evolution_chamber");
        Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Evolution_Chamber, 1, start);
    }
}

void Adaptive::Economy(const Counts& counts, bool emergency) {
    const auto self = BWAPI::Broodwar->self();
    const auto threats = Micro::GetBaseThreats();
    if (!emergency && counts.drones >= m_genome.GetInt(Gene::SecondGasDrones)) {
        for (auto depot : self->getUnits()) {
            if (depot->getType().isResourceDepot() && depot->isCompleted() && self->minerals() >= m_reserveMinerals + 50)
                Tools::EnsureBaseGas(depot);
        }
    }

    const int perBase = m_genome.GetInt(Gene::DronesPerBase);
    const int gasSlots = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Extractor) * 3;
    int targetDrones = std::min(m_genome.GetInt(Gene::MaxDrones), std::max(1, counts.miningSites) * perBase + gasSlots);
    // Never breed drones past two per mineral patch (plus gas and a couple of builders).
    const auto mining = Tools::GetMiningCapacity();
    targetDrones = std::min(targetDrones, mining.mineralSlots + gasSlots + SpareBuilders);
    if (RushPending()) targetDrones = std::min(targetDrones, m_genome.GetInt(Gene::RushDroneCap));
    const double armyNeeded = m_genome.Get(Gene::ArmyPerDrone) * std::max(0, counts.drones - 12);
    if (!emergency && counts.drones < targetDrones && (m_openerDone || Ready(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0) &&
        counts.armySupply / 2.0 >= armyNeeded && self->minerals() >= m_reserveMinerals + 50) {
        if (Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Drone)) m_decision = "grow_workers";
    }

    if (emergency || !threats.empty() || Tools::IsQueued(BWAPI::UnitTypes::Zerg_Hatchery).isValid()) return;
    const auto reserved = Tools::GetConstructionReserve();
    const bool surplus = CombatPolicy::SurplusExpansion(self->minerals() - std::max(m_reserveMinerals, reserved.first),
        counts.drones, counts.armySupply);
    const bool natural = counts.miningSites < 2 && counts.drones >= Gate(Gene::ExpandDrones);
    const bool later = counts.miningSites >= 2 && counts.miningSites < 6 &&
        counts.drones >= m_genome.GetInt(Gene::ThirdBaseDrones) + (counts.miningSites - 2) * perBase &&
        counts.drones >= counts.miningSites * perBase - 4 && !RushPending();
    // Every patch already has two miners: the spare drones go and take a new base.
    const bool saturated = mining.mineralSlots > 0 && counts.miningSites < 8 && !RushPending() &&
        mining.mineralWorkers + mining.idleWorkers >= mining.mineralSlots && mining.idleWorkers > 0;
    if (natural || later || surplus || saturated) {
        const auto expansion = BasesTools::GetNextExpansionPosition();
        if (expansion.isValid()) {
            m_reserveMinerals = std::max(m_reserveMinerals, 300);
            m_decision = surplus ? "surplus_expand" : saturated ? "saturated_expand" : "expand";
            Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Hatchery, 1, expansion);
            return;
        }
    }
}

void Adaptive::QueenSupport(const Counts& counts) {
    const auto self = BWAPI::Broodwar->self();
    if (!Learning::Spec(m_composition).queens || !HasTech(Tech::QueensNest, true)) return;
    if (counts.queens < m_genome.GetInt(Gene::QueenCount)) {
        m_reserveMinerals = std::max(m_reserveMinerals, 100);
        m_reserveGas = std::max(m_reserveGas, 100);
        if (Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Queen)) m_decision = "queen";
    }
    if (counts.queens == 0) return;
    for (auto tech : { BWAPI::TechTypes::Ensnare, BWAPI::TechTypes::Spawn_Broodlings }) {
        if (self->hasResearched(tech)) continue;
        if (!self->isResearching(tech)) {
            m_reserveMinerals = std::max(m_reserveMinerals, tech.mineralPrice());
            m_reserveGas = std::max(m_reserveGas, tech.gasPrice());
            Tools::ResearchTech(tech);
        }
        break;
    }
}

void Adaptive::Upgrades(const Counts& counts) {
    const auto self = BWAPI::Broodwar->self();
    const auto& spec = Learning::Spec(m_composition);
    if (counts.armySupply / 2 < m_genome.GetInt(Gene::UpgradeArmySupply) || m_reserveGas > 0 ||
        self->minerals() < 200) return;
    const auto research = [self](BWAPI::UpgradeType upgrade) {
        if (self->getUpgradeLevel(upgrade) >= self->getMaxUpgradeLevel(upgrade) || self->isUpgrading(upgrade)) return;
        Tools::ResearchUpgrade(upgrade);
    };
    const bool air = spec.Share(Army::Mutalisk) + spec.Share(Army::Guardian) + spec.Share(Army::Devourer) > 0;
    const bool melee = spec.Share(Army::Zergling) + spec.Share(Army::Ultralisk) > 0;
    const bool ranged = spec.Share(Army::Hydralisk) + spec.Share(Army::Lurker) > 0;
    if (spec.Share(Army::Zergling) > 0) research(BWAPI::UpgradeTypes::Adrenal_Glands);
    if (ranged) { research(BWAPI::UpgradeTypes::Muscular_Augments); research(BWAPI::UpgradeTypes::Grooved_Spines); }
    if (spec.Share(Army::Ultralisk) > 0) { research(BWAPI::UpgradeTypes::Chitinous_Plating); research(BWAPI::UpgradeTypes::Anabolic_Synthesis); }
    if (air) { research(BWAPI::UpgradeTypes::Zerg_Flyer_Attacks); research(BWAPI::UpgradeTypes::Zerg_Flyer_Carapace); }
    if (melee) research(BWAPI::UpgradeTypes::Zerg_Melee_Attacks);
    if (ranged) research(BWAPI::UpgradeTypes::Zerg_Missile_Attacks);
    if (melee || ranged) research(BWAPI::UpgradeTypes::Zerg_Carapace);
}

void Adaptive::MorphAdvancedUnits(const Counts& counts) {
    const auto& spec = Learning::Spec(m_composition);
    int total = 0;
    for (int a = 0; a < Learning::ArmyCount; ++a) total += counts.army[a] * armyTypes[a].supplyRequired();
    const auto deficit = [&](Army army) {
        return spec.Share(army) * total - counts.army[static_cast<int>(army)] * ArmyType(army).supplyRequired();
    };
    // Guardians and Devourers are Mutalisk morphs; Lurkers are Hydralisk morphs.
    // Devourers are support, kept at one per five Mutalisks whatever the composition.
    if (HasTech(Tech::GreaterSpire, true)) {
        const int mutalisks = counts.army[static_cast<int>(Army::Mutalisk)];
        const int devourers = counts.army[static_cast<int>(Army::Devourer)];
        Army target = Army::Count;
        if (CombatPolicy::WantDevourer(mutalisks, devourers)) target = Army::Devourer;
        else if (spec.Share(Army::Guardian) > 0 && deficit(Army::Guardian) >= ArmyType(Army::Guardian).supplyRequired() &&
                 deficit(Army::Mutalisk) <= deficit(Army::Guardian)) target = Army::Guardian;
        if (target != Army::Count) {
            for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
                if (unit->getType() != BWAPI::UnitTypes::Zerg_Mutalisk || !unit->isCompleted() || unit->isMorphing() ||
                    unit->isUnderAttack() || unit->getHitPoints() < unit->getType().maxHitPoints() / 2) continue;
                m_reserveMinerals = std::max(m_reserveMinerals, ArmyType(target).mineralPrice());
                m_reserveGas = std::max(m_reserveGas, ArmyType(target).gasPrice());
                if (Tools::MorphUnit(unit, ArmyType(target))) MatchLog::Event("air_morph", ArmyType(target).getName());
                break;
            }
        }
    }
    if (spec.Share(Army::Lurker) > 0 && BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect) &&
        deficit(Army::Lurker) >= 2 && deficit(Army::Hydralisk) <= deficit(Army::Lurker)) {
        for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
            if (unit->getType() != BWAPI::UnitTypes::Zerg_Hydralisk || !unit->isCompleted() || unit->isMorphing() ||
                unit->isUnderAttack()) continue;
            m_reserveMinerals = std::max(m_reserveMinerals, 50);
            m_reserveGas = std::max(m_reserveGas, 100);
            Tools::MorphUnit(unit, BWAPI::UnitTypes::Zerg_Lurker);
            break;
        }
    }
}

void Adaptive::SpendArmyBudget(const Counts& counts) {
    const auto self = BWAPI::Broodwar->self();
    const auto& spec = Learning::Spec(m_composition);
    const int reserveSupply = spec.queens && HasTech(Tech::QueensNest, true) ?
        std::max(0, m_genome.GetInt(Gene::QueenCount) - counts.queens) * BWAPI::UnitTypes::Zerg_Queen.supplyRequired() : 0;
    int supply = self->supplyTotal() - self->supplyUsed() - reserveSupply;
    int minerals = self->minerals() - m_reserveMinerals;
    int gas = self->gas() - m_reserveGas;

    std::array<int, Learning::ArmyCount> army = counts.army;
    const bool canMake[] = {
        Ready(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0,
        HasTech(Tech::HydraliskDen, true),
        HasTech(Tech::Spire, true),
        false, false, false,
        HasTech(Tech::UltraliskCavern, true),
    };
    // Larva demand: morph targets pull demand into their source unit.
    const auto demand = [&](Army kind) {
        switch (kind) {
            case Army::Hydralisk: return spec.Share(Army::Hydralisk) + spec.Share(Army::Lurker);
            case Army::Mutalisk: return spec.Share(Army::Mutalisk) + (HasTech(Tech::GreaterSpire, false) || HasTech(Tech::Hive, false) ?
                spec.Share(Army::Guardian) + spec.Share(Army::Devourer) : 0.0);
            default: return spec.Share(kind);
        }
    };
    const auto sourceSupply = [&](Army source) {
        int value = army[static_cast<int>(source)] * ArmyType(source).supplyRequired();
        if (source == Army::Hydralisk) value += army[static_cast<int>(Army::Lurker)] * BWAPI::UnitTypes::Zerg_Lurker.supplyRequired();
        if (source == Army::Mutalisk)
            value += (army[static_cast<int>(Army::Guardian)] + army[static_cast<int>(Army::Devourer)]) * 2;
        return value;
    };

    for (auto larva : self->getUnits()) {
        if (larva->getType() != BWAPI::UnitTypes::Zerg_Larva) continue;
        int total = 0;
        for (int a = 0; a < Learning::ArmyCount; ++a) total += army[a] * armyTypes[a].supplyRequired();
        BWAPI::UnitType choice = BWAPI::UnitTypes::None;
        double bestDeficit = -1e9;
        for (auto source : { Army::Zergling, Army::Hydralisk, Army::Mutalisk, Army::Ultralisk }) {
            const auto type = ArmyType(source);
            if (!canMake[static_cast<int>(source)] || demand(source) <= 0 ||
                minerals < type.mineralPrice() || gas < type.gasPrice()) continue;
            const double deficit = demand(source) * (total + type.supplyRequired()) - sourceSupply(source);
            if (deficit > bestDeficit) { bestDeficit = deficit; choice = type; }
        }
        // Gas-starved or pre-tech: Zerglings turn floating minerals into defense.
        const int lingCap = spec.Share(Army::Zergling) > 0 ? 400 : 24;
        if (choice == BWAPI::UnitTypes::None && canMake[0] && army[0] < lingCap &&
            minerals >= 50 + (spec.Share(Army::Zergling) > 0 ? 0 : 200))
            choice = BWAPI::UnitTypes::Zerg_Zergling;
        if (choice == BWAPI::UnitTypes::None) break;
        const int required = choice.supplyRequired() * (choice.isTwoUnitsInOneEgg() ? 2 : 1);
        if (supply < required || !Tools::MorphUnit(larva, choice)) continue;
        supply -= required;
        minerals -= choice.mineralPrice();
        gas -= choice.gasPrice();
        for (int a = 0; a < Learning::ArmyCount; ++a)
            if (armyTypes[a] == choice) army[a] += choice.isTwoUnitsInOneEgg() ? 2 : 1;
    }
}

// Gas income trails mineral income, so banked minerals go into supply, Zerglings and structures
// instead of piling up. Runs after the planned army so it only uses what that left over.
void Adaptive::SpendExcessMinerals(const Counts& counts, bool emergency) {
    const auto self = BWAPI::Broodwar->self();
    // BWAPI only deducts costs once orders execute, so count this frame's orders ourselves.
    int bank = self->minerals() - std::max(m_reserveMinerals, Tools::GetConstructionReserve().first) - PendingOrderMinerals();
    if (bank < ExcessMinerals) return;

    // Supply first, so spare larvae never wait on Overlords.
    const int freeSupply = Tools::GetTotalSupply(true) - self->supplyUsed();
    const int supplyBuffer = std::min(40, 16 + std::max(1, counts.bases) * 8);
    if (self->supplyTotal() < 400 && freeSupply < supplyBuffer && Tools::MorphLarva(BWAPI::UnitTypes::Zerg_Overlord)) {
        m_decision = "excess_overlord";
        return;
    }

    // Zerglings on the remaining larvae.
    const bool lingComposition = Learning::Spec(m_composition).Share(Army::Zergling) > 0;
    int lings = counts.army[static_cast<int>(Army::Zergling)];
    int supply = self->supplyTotal() - self->supplyUsed();
    if (Ready(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0) {
        for (auto larva : self->getUnits()) {
            if (larva->getType() != BWAPI::UnitTypes::Zerg_Larva || supply < 2 || bank < ExcessMinerals - 150 ||
                (!lingComposition && lings >= 48)) continue;
            if (!Tools::MorphUnit(larva, BWAPI::UnitTypes::Zerg_Zergling)) continue;
            supply -= 2;
            lings += 2;
            bank -= BWAPI::UnitTypes::Zerg_Zergling.mineralPrice();
            m_decision = "excess_zerglings";
        }
    }
    if (emergency || bank < ExcessMinerals - 100 || !Ready(BWAPI::UnitTypes::Zerg_Spawning_Pool)) return;

    // Out of larvae: more Hatcheries (one at a time), kept out of the mineral lines.
    bool hatcheryInProgress = Tools::IsQueued(BWAPI::UnitTypes::Zerg_Hatchery).isValid();
    for (auto unit : self->getUnits())
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && !unit->isCompleted()) hatcheryInProgress = true;
    if (!hatcheryInProgress && counts.larva <= 1 && counts.bases < 10 && self->supplyUsed() < 380 &&
        Tools::BuildMacroHatchery()) {
        m_decision = "excess_hatchery";
        return;
    }

    // An Evolution Chamber unlocks Spore Colonies and ground upgrades.
    if (!HasTech(Tech::EvolutionChamber, false)) {
        if (Tools::TryBuildBuilding(BWAPI::UnitTypes::Zerg_Evolution_Chamber, 1, self->getStartLocation()))
            m_decision = "excess_evolution_chamber";
        return;
    }

    // Static defense at every mining base: Spores once enemy air has been seen, otherwise Sunkens.
    bool enemyAir = false;
    for (const auto& [id, type] : m_enemyUnits)
        if (type.isFlyer() && (type.canAttack() || type == BWAPI::UnitTypes::Protoss_Carrier)) { enemyAir = true; break; }
    const int perBase = bank >= 2 * ExcessMinerals ? 2 : 1;
    for (auto depot : self->getUnits()) {
        if (!depot->getType().isResourceDepot() || !depot->isCompleted()) continue;
        bool mining = false;
        for (auto mineral : BWAPI::Broodwar->getMinerals())
            if (mineral->getResources() > 0 && mineral->getDistance(depot) < 320) { mining = true; break; }
        if (!mining) continue;
        if (Tools::EnsureStaticDefense(depot, perBase, enemyAir ? BWAPI::UnitTypes::Zerg_Spore_Colony : BWAPI::UnitTypes::Zerg_Sunken_Colony)) {
            m_decision = "excess_static_defense";
            return;
        }
    }
}

// Scouted enemy army in BWAPI supply units, with static defense counted as a few units' worth.
int Adaptive::KnownEnemyArmySupply() const {
    int supply = 0;
    for (const auto& [id, type] : m_enemyUnits) {
        if (IsStaticDefense(type)) { supply += 6; continue; }
        if (type.isBuilding() || type.isWorker() || type == BWAPI::UnitTypes::Zerg_Overlord ||
            type == BWAPI::UnitTypes::Zerg_Larva || type == BWAPI::UnitTypes::Zerg_Egg) continue;
        if (!type.canAttack() && !type.isSpellcaster() && type != BWAPI::UnitTypes::Protoss_Carrier &&
            type != BWAPI::UnitTypes::Protoss_Reaver) continue;
        supply += std::max(1, type.supplyRequired());
    }
    return supply;
}

void Adaptive::ManageAttack(const Counts& counts, bool emergency) {
    const int threshold = RushPending() ? m_genome.GetInt(Gene::RushAttackSupply) : m_genome.GetInt(Gene::AttackSupply);
    const int army = counts.armySupply / 2;
    const bool maxed = BWAPI::Broodwar->self()->supplyUsed() >= 380;
    // Attack only when the scouted enemy army is clearly beatable; a small share of time
    // windows also accept close odds. A maxed army still refuses a clearly lost fight.
    const int enemyArmy = KnownEnemyArmySupply();
    const bool gamble = CombatPolicy::TakeCloseFight(BWAPI::Broodwar->getFrameCount());
    const bool winnable = maxed ? !CombatPolicy::AttackLost(counts.armySupply, enemyArmy)
                                : CombatPolicy::AttackWinnable(counts.armySupply, enemyArmy, gamble);
    if (!m_attacking && !emergency && (army >= threshold || maxed)) {
        if (!winnable) {
            m_decision = "hold_outmatched";
            return;
        }
        m_attacking = true;
        if (Learning::Spec(m_composition).rush) m_rushLaunched = true;
        Micro::SetMode(Micro::MicroMode::Aggressive);
        MatchLog::Event("attack", std::string(Learning::CompositionName(m_composition)) + " army=" + std::to_string(army) +
            " enemy=" + std::to_string(enemyArmy / 2) + (gamble ? " close_fight" : ""));
    } else if (m_attacking && (emergency || army < threshold * m_genome.Get(Gene::RetreatFraction) ||
                               CombatPolicy::AttackLost(counts.armySupply, enemyArmy))) {
        m_attacking = false;
        Micro::SetMode(Micro::MicroMode::Defensive);
        MatchLog::Event("regroup", emergency ? "base_emergency" :
            army < threshold * m_genome.Get(Gene::RetreatFraction) ? "army_depleted" : "outmatched");
    }
}

void Adaptive::onEnd(bool isWinner) {
    RecordActiveTime();
    const auto self = BWAPI::Broodwar->self();
    const auto enemy = BWAPI::Broodwar->enemy();
    const double minutes = BWAPI::Broodwar->getFrameCount() / double(FramesPerSecond * 60);
    const double reward = Learning::Reward(isWinner, self->getKillScore(), enemy ? enemy->getKillScore() : 0, minutes);
    MatchLog::Event("learning", "reward=" + std::to_string(reward) + " composition=" +
        Learning::CompositionName(m_composition) + " switches=" + std::to_string(std::max(0, m_switches - 1)));
    if (!m_learning) return;
    m_learner.EndGame(reward);
    if (!m_learner.Save(m_dataPath)) std::cout << "Adaptive: could not save learning data to " << m_dataPath << "\n";
    else std::cout << "Adaptive: reward " << reward << " saved to " << m_dataPath << "\n";
}

void Adaptive::onSendText(std::string text) {
    // "comp" prints the plan, "comp <name|1-9>" locks a composition, "comp auto" resumes learning control.
    if (text.rfind("comp", 0) != 0) return;
    const auto argument = text.size() > 5 ? text.substr(5) : std::string();
    Composition requested;
    if (argument == "auto") {
        m_locked = false;
        BWAPI::Broodwar->printf("Adaptive: composition switching unlocked");
    } else if (Learning::ParseComposition(argument, requested)) {
        RecordActiveTime();
        m_locked = true;
        SetComposition(requested, "manual");
    } else {
        BWAPI::Broodwar->printf("Adaptive: %s (context %d, %s)", Learning::CompositionName(m_composition), m_context,
            m_locked ? "locked" : "adaptive");
    }
}
