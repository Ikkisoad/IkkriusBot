#pragma once

#include <BWAPI.h>

namespace Micro
{
    enum class MicroMode
    {
        Neutral,
        Aggressive,
        Defensive
    };

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
    
    // HiveTech Micro
    void HiveTechMicroLoop(BWAPI::Unitset myUnits);
    void MutaliskHarassLoop(BWAPI::Unit muta, BWAPI::Unitset enemies);
    void GuardianAssaultLoop(BWAPI::Unit guardian, BWAPI::Unitset enemies);
    void QueenCastLoop(BWAPI::Unit queen, BWAPI::Unitset enemies);
}