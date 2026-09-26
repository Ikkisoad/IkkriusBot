#pragma once
#include "../../../visualstudio/src/starterbot/BuildOrder.h"
#include "../learning/Learning.h"
#include <BWAPI.h>
#include <array>
#include <map>

// Plays one of nine Zerg compositions and switches between them mid-match based on scouted enemy tech.
// Step timings come from a genome evolved between matches; composition choice is learned per enemy tech context.
class Adaptive : public BuildOrder {
public:
    static Adaptive& Instance();
    void onStart() override;
    void Execute() override;
    void onEnd(bool isWinner) override;
    void onUnitShow(BWAPI::Unit unit) override;
    void onUnitMorph(BWAPI::Unit unit) override;
    void onUnitDestroy(BWAPI::Unit unit) override;
    void onSendText(std::string text) override;
    std::string GetName() const override { return "Adaptive"; }

private:
    Adaptive() = default;

    enum class Tech { Extractor, HydraliskDen, Lair, Spire, QueensNest, Hive, GreaterSpire, UltraliskCavern, EvolutionChamber, Count };

    struct Counts {
        int drones = 0, bases = 0, miningSites = 0, larva = 0;
        int armySupply = 0; // BWAPI supply units (a Zergling is 1, a Drone is 2)
        std::array<int, Learning::ArmyCount> army{};
        int queens = 0;
    };

    Learning::Learner m_learner;
    Learning::Genome m_genome = Learning::Genome::Default();
    Learning::Composition m_composition = Learning::Composition::LingMutaQueen;
    bool m_learning = true;
    bool m_locked = false;
    int m_context = 0;
    int m_lastSwitchFrame = 0;
    int m_lastEvaluationFrame = 0;
    int m_lastRecordFrame = 0;
    int m_switches = 0;
    bool m_attacking = false;
    bool m_rushLaunched = false;
    bool m_openerDone = false;
    int m_reserveMinerals = 0;
    int m_reserveGas = 0;
    std::string m_enemyRace = "Unknown";
    std::string m_dataPath;
    std::string m_decision;
    std::map<int, BWAPI::UnitType> m_enemyUnits;

    Counts Count() const;
    Learning::EnemyProfile BuildProfile() const;
    std::array<double, Learning::CompositionCount> OwnedTech() const;
    static bool Needs(Learning::Composition composition, Tech tech);
    bool HasTech(Tech tech, bool completed) const;

    void RememberEnemy(BWAPI::Unit unit);
    void RecordActiveTime();
    void EvaluateComposition(bool opening);
    void SetComposition(Learning::Composition composition, const std::string& reason);

    bool RushPending() const;
    int Gate(Learning::Gene gene) const;
    void Opener(const Counts& counts);
    void AdvanceTech(const Counts& counts);
    void Economy(const Counts& counts, bool emergency);
    void QueenSupport(const Counts& counts);
    void Upgrades(const Counts& counts);
    void MorphAdvancedUnits(const Counts& counts);
    void SpendArmyBudget(const Counts& counts);
    void SpendExcessMinerals(const Counts& counts, bool emergency);
    int KnownEnemyArmySupply() const;
    void ManageAttack(const Counts& counts, bool emergency);
};
