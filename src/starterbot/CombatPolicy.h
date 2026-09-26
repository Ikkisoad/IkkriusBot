#pragma once
#include <algorithm>

// Pure composition decisions shared by strategy, micro and focused regressions.
namespace CombatPolicy {
    inline bool SurplusExpansion(int minerals, int drones, int armySupply) {
        return minerals >= 1200 && drones >= 24 && armySupply >= 32;
    }
    inline bool PressureAllowed(bool active, int supply, int minerals, bool safe, bool production) {
        return safe && production && supply >= (active ? 350 : 390) && minerals >= (active ? 600 : 1500);
    }
    inline bool BuyReplacement(int mineralCost, int gasCost, int supplyCost,
                               int& minerals, int& gas, int& supply) {
        if (minerals < mineralCost || gas < gasCost || supply < supplyCost) return false;
        minerals -= mineralCost; gas -= gasCost; supply -= supplyCost;
        return true;
    }

    // Local fight evaluation: units commit together when the nearby group wins the trade,
    // hit-and-run when it is close, and only withdraw when clearly outmatched.
    enum class Engagement { Commit, HitAndRun, Withdraw };
    inline Engagement AssessEngagement(double friendlyPower, double enemyPower) {
        if (enemyPower <= 0 || friendlyPower >= enemyPower * 1.2) return Engagement::Commit;
        if (enemyPower > friendlyPower * 1.6) return Engagement::Withdraw;
        return Engagement::HitAndRun;
    }
    // Step back only between shots or when badly hurt, so the group keeps its damage on target.
    inline bool ShouldStepBack(Engagement engagement, bool ranged, int cooldown, int hitPoints, int maxHitPoints) {
        if (engagement == Engagement::Withdraw) return true;
        if (cooldown <= 0) return false;
        if (hitPoints * 4 < maxHitPoints) return true;
        return engagement == Engagement::HitAndRun && (ranged || hitPoints * 5 < maxHitPoints * 2);
    }
    // Lower is better: threats first, then the target allies already shoot, then the weakest and closest.
    inline double FocusScore(int tier, int distance, int reach, double hpFraction, int alliesOnTarget) {
        const int chase = std::max(0, distance - reach);
        return tier * 400.0 + chase * 2.0 + hpFraction * 120.0 - std::min(alliesOnTarget, 4) * 60.0;
    }

    constexpr int HydrasPerGroup = 12;
    inline int QueenTarget(int hydraGroups, int airCombatUnits) {
        return hydraGroups + (airCombatUnits > 0 ? (airCombatUnits + 15) / 16 : 0);
    }
    enum class AirMorph { None, Guardian, Devourer };
    inline AirMorph NextAirMorph(int mutalisks, int guardians, int devourers, int enemyAirSupply) {
        // Keep an escort/harassment flock rather than converting all mobile anti-air.
        if (mutalisks <= 8) return AirMorph::None;
        const int targetDevourers = enemyAirSupply > 0 ? std::clamp((enemyAirSupply + 7) / 8, 2, 6) : 1;
        const int targetGuardians = std::clamp((mutalisks + guardians + devourers) / 2, 4, 12);
        if (enemyAirSupply > 0 && devourers < targetDevourers) return AirMorph::Devourer;
        if (guardians < targetGuardians) return AirMorph::Guardian;
        if (devourers < targetDevourers) return AirMorph::Devourer;
        return AirMorph::None;
    }

    // Hit-and-run only pays against shorter-ranged units; against equal or longer
    // range, backing off during cooldown just hands the enemy free shots.
    inline bool KiteWorthwhile(int myRange, int threatRange) { return myRange > threatRange; }

    // Mutalisk worker harassment: small flocks raid while the main army cannot win outright.
    constexpr int HarassSquadMin = 3;
    constexpr int HarassSquadMax = 6;
    inline int HarassSquadSize(int mutalisks, bool mainAttackActive) {
        if (mainAttackActive || mutalisks < HarassSquadMin) return 0;
        return std::min(mutalisks, HarassSquadMax);
    }
    // Static anti-air is worth several Mutalisks; mobile anti-air counts by supply.
    constexpr double StaticAntiAirPower = 6.0;
    inline bool HarassAbort(double squadPower, double antiAirPower) {
        return antiAirPower > squadPower * 0.75;
    }
    // Damaged raiders leave to regenerate and only rejoin once nearly full.
    inline bool HarassNeedsRegen(int hp, int maxHp, bool regenerating) {
        return regenerating ? hp * 10 < maxHp * 9 : hp * 5 < maxHp * 2;
    }
}
