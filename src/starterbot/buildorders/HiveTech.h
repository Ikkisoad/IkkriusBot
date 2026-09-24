#pragma once
#include "../../../visualstudio/src/starterbot/BuildOrder.h"
#include <BWAPI.h>

class HiveTech : public BuildOrder {
public:
    static HiveTech& Instance();
    void Execute() override;
    void OnUnitCreate(BWAPI::Unit unit) override;
    void onUnitComplete(BWAPI::Unit unit) override;
    void onStart() override;
    std::string GetName() const override { return "HiveTech"; }

protected:
    explicit HiveTech(bool airBuild = false) : m_airBuild(airBuild) {}

private:
    const bool m_airBuild;

    enum class Phase {
        SafeOpener,
        TechToLair,
        LairHarass, // Mutas or Hydras
        TechToHive,
        HiveAssault // Guardians/Devourers/Queens
    };

    enum class SubStrategy {
        Mutalisk,
        HydraliskLurker
    };

    Phase currentPhase = Phase::SafeOpener;
    SubStrategy currentSubStrategy = SubStrategy::Mutalisk;

    // Trackers
    bool builtSpawningPool = false;
    bool builtExtractor = false;
    bool builtLair = false;
    bool builtSpire = false;
    bool builtHydraliskDen = false;
    bool builtQueensNest = false;
    bool builtHive = false;
    bool builtGreaterSpire = false;
    
    int  zerglingsTarget = 6;
    bool hasNatural = false;
    
    bool m_attacking = false;
    BWAPI::Unitset m_pressureWave;
    void UpdatePressureWave(bool safe, int armySupply);
    int m_reserveMinerals = 0;
    int m_reserveGas = 0;

    // Helpers
    void MaintainQueenSupport();
    void ExecuteAirTech();
    void ExecuteAirHarass();
    void ExecuteAirAssault();
    void SpendArmyBudget(int reserveMinerals, int reserveGas, int reserveSupply = 0);
    void ExecuteSafeOpener();
    void ExecuteTechToLair();
    void ExecuteLairHarass();
    void ExecuteTechToHive();
    void ExecuteHiveAssault();
    
    void DetermineSubStrategy();
};
