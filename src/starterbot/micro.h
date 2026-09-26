#pragma once

#include <BWAPI.h>
#include <vector>

namespace Micro
{
    enum class MicroMode
    {
        Neutral,
        Aggressive,
        Defensive
    };

    BWAPI::Unitset GetBaseThreats();
    bool DefendBases(BWAPI::Unit unit, const BWAPI::Unitset& threats);
    void SetMode(MicroMode newMode);
    MicroMode GetMode();

    void SmartAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target);
    void SmartMove(BWAPI::Unit unit, BWAPI::Position position);
    void SmartKiteTarget(BWAPI::Unit rangedUnit, BWAPI::Unit target);
    void SmartFleeUntilHealed(BWAPI::Unit meleeUnit, BWAPI::Unit enemyUnit);
    void SmartScoutMove(BWAPI::Unit scout, BWAPI::Position targetPos);
    void ScoutAndWander(BWAPI::Unit scout);
    void SmartAvoidLethalAndAttackNonLethal(BWAPI::Unit unit, bool alwaysAvoid);
    void sendIdleWorkersToMinerals();
    void GatherMinerals(BWAPI::Unit unit);
    void GatherResources(BWAPI::Unit unit);
    void SmartGatherMinerals(BWAPI::Unit drone);
    void unitAttack(BWAPI::Unit unit);
    void attack();
    void BasicAttackAndScoutLoop(BWAPI::Unitset myUnits);
    void Retreat(BWAPI::Unit unit);
    void Flee(BWAPI::Unit unit, BWAPI::Unit closestLethal);
    
    // Group combat helpers
    struct LocalFight { double friendlyPower = 0, enemyPower = 0; int allies = 0; BWAPI::Position allyCenter = BWAPI::Positions::None; };
    LocalFight AssessLocalFight(BWAPI::Unit unit, int radius);
    BWAPI::Unit ChooseFocusTarget(BWAPI::Unit unit, const BWAPI::Unitset& candidates, bool preferWorkers);
    void FallBack(BWAPI::Unit unit, BWAPI::Unit threat, BWAPI::Position anchor);

    void ResetCombatState();
    std::vector<BWAPI::Unitset> GetHydraGroups(const BWAPI::Unitset& units);
    void LurkerSupportLoop(BWAPI::Unit unit, const BWAPI::Unitset& threats, BWAPI::Position rally, BWAPI::Position center);
    void DevourerEscortLoop(BWAPI::Unit unit, BWAPI::Position escort);
    bool SpellReserved(BWAPI::TechType tech, BWAPI::Unit target, BWAPI::Position position);

    // HiveTech Micro
    void GroundArmyLoop(BWAPI::Unit unit, const BWAPI::Unitset& threats, BWAPI::Position rally, BWAPI::Position center);
    void SmartAttackMove(BWAPI::Unit unit, BWAPI::Position position);
    // `needsDetection` is set once the enemy has been scouted with cloak/burrow attackers, so a
    // spare Overlord is pulled in to escort the attacking army instead of just spreading for vision.
    void HiveTechMicroLoop(BWAPI::Unitset myUnits, const BWAPI::Unitset& pressureWave = {}, bool needsDetection = false);
    void MutaliskHarassLoop(BWAPI::Unit muta, BWAPI::Unitset enemies);
    // Scourges only carry an air weapon; they fly with the flock and suicide into the highest-value target in reach.
    void ScourgeStrikeLoop(BWAPI::Unit scourge, BWAPI::Position escort);
    // Small-flock worker raid; `retreat` sends the raider home to regenerate or disengage.
    void MutaliskRaidLoop(BWAPI::Unit muta, BWAPI::Position raidTarget, BWAPI::Position squadCenter, BWAPI::Position home, bool retreat);
    // Siege from max range; spend weapon cooldown outside enemy anti-air reach.
    // With no target in reach, `siegeTarget` (an expansion to deny or a base to contain) replaces the enemy main.
    void GuardianAssaultLoop(BWAPI::Unit guardian, BWAPI::Unitset enemies, BWAPI::Position fallback = BWAPI::Positions::None,
                             BWAPI::Position siegeTarget = BWAPI::Positions::None);
    bool QueenCastLoop(BWAPI::Unit queen, BWAPI::Unitset enemies);
    // Friendly spell areas (Ensnare) that our own units step out of until the spell lands.
    void MarkSpellArea(BWAPI::Position center, int radius);
    bool DodgeFriendlySpell(BWAPI::Unit unit);
    // While Guardians are on the field, targets under enemy static defense are left to them.
    bool AvoidsStaticDefense(BWAPI::Unit unit, BWAPI::Unit enemy);
}