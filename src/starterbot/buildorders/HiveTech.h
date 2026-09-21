#pragma once
#include "../../src/starterbot/BuildOrder.h"
#include <BWAPI.h>

class HiveTech : public BuildOrder {
public:
    static HiveTech& Instance();
    void Execute() override;
    void OnUnitCreate(BWAPI::Unit unit) override;
    void onUnitComplete(BWAPI::Unit unit) override;
    void onStart() override;
    std::string GetName() const override { return "HiveTech"; }

private:
    HiveTech() = default;

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
    
    // Helpers
    void ExecuteSafeOpener();
    void ExecuteTechToLair();
    void ExecuteLairHarass();
    void ExecuteTechToHive();
    void ExecuteHiveAssault();
    
    void DetermineSubStrategy();
};
