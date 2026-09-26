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

    // Fights are taken when clearly winnable. Close fights are declined, except for a small
    // share of time windows where the whole army gambles on them together.
    constexpr double WinningRatio = 1.3;    // Our power over theirs needed to commit.
    constexpr double CloseFightRatio = 0.9; // Below this the fight is lost; withdraw.
    constexpr int CloseFightWindowFrames = 24 * 15;
    constexpr int CloseFightChancePercent = 15;
    // Deterministic per window, so every unit makes the same call and the army does not split.
    inline bool TakeCloseFight(int frame) {
        const unsigned window = static_cast<unsigned>(std::max(0, frame) / CloseFightWindowFrames);
        return (window * 2654435761u >> 16) % 100 < static_cast<unsigned>(CloseFightChancePercent);
    }

    // Local fight evaluation: units commit together when the nearby group wins the trade,
    // hit-and-run through a close fight only when gambling on it, and otherwise withdraw.
    enum class Engagement { Commit, HitAndRun, Withdraw };
    inline Engagement AssessEngagement(double friendlyPower, double enemyPower, bool takeCloseFight = false) {
        if (enemyPower <= 0 || friendlyPower >= enemyPower * WinningRatio) return Engagement::Commit;
        if (takeCloseFight && friendlyPower >= enemyPower * CloseFightRatio) return Engagement::HitAndRun;
        return Engagement::Withdraw;
    }
    // Army-level attack decision on the enemy army we have scouted (both in BWAPI supply units).
    inline bool AttackWinnable(int armySupply, int knownEnemySupply, bool takeCloseFight) {
        if (knownEnemySupply <= 0) return true;
        return armySupply >= knownEnemySupply * (takeCloseFight ? CloseFightRatio : WinningRatio);
    }
    // Abandon an attack once the scouted enemy army clearly outweighs ours.
    inline bool AttackLost(int armySupply, int knownEnemySupply) {
        return knownEnemySupply > 0 && armySupply * WinningRatio < knownEnemySupply;
    }
    // A base this outmatched (or undefended) is finished off rather than pulled back once our own
    // army count trips the attrition-based retreat fraction; only a real threat turns it around.
    constexpr double OverwhelmRatio = 2.0;
    inline bool Overwhelming(int armySupply, int knownEnemySupply) {
        return knownEnemySupply <= 0 || armySupply >= knownEnemySupply * OverwhelmRatio;
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
    // Devourers are support: one per five Mutalisks, fighting inside the Mutalisk flock.
    constexpr int MutalisksPerDevourer = 5;
    inline int DevourerTarget(int mutalisks) { return std::max(0, mutalisks) / MutalisksPerDevourer; }
    inline bool WantDevourer(int mutalisks, int devourers) {
        // Morphing consumes a Mutalisk, so check the ratio still holds afterwards.
        return devourers < DevourerTarget(mutalisks - 1);
    }

    enum class AirMorph { None, Guardian, Devourer };
    inline AirMorph NextAirMorph(int mutalisks, int guardians, int devourers, int enemyAirSupply) {
        // Keep an escort/harassment flock rather than converting all mobile anti-air.
        if (mutalisks <= 8) return AirMorph::None;
        const int targetGuardians = std::clamp((mutalisks + guardians + devourers) / 2, 4, 12);
        if (enemyAirSupply > 0 && WantDevourer(mutalisks, devourers)) return AirMorph::Devourer;
        if (guardians < targetGuardians) return AirMorph::Guardian;
        if (WantDevourer(mutalisks, devourers)) return AirMorph::Devourer;
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

    // Ensnare slows every unit under it, ours included: an allied unit caught in the
    // cloud costs about two enemies' worth of value. Zero means "do not cast here".
    constexpr int EnsnareRadius = 96;
    inline int EnsnareScore(int enemies, int allies) {
        const int score = enemies - allies * 2;
        return enemies >= 3 && score >= 3 ? score : 0;
    }

    // Maxed out with a bank: minerals can no longer become units, so each mining base
    // turns them into colonies. Returns the Sunken target per base (Spores follow it).
    inline int SurplusColonies(int supplyUsed, int minerals) {
        if (supplyUsed < 380 || minerals < 800) return 0;
        return std::min(4, 1 + (minerals - 800) / 400);
    }
    inline int SurplusSpores(int colonies, bool enemyAir) {
        if (colonies <= 0) return 0;
        return enemyAir ? std::min(3, colonies) : 1;
    }

    // Guardians outrange every static defense, so once they are on the field the rest of
    // the army leaves colonies/cannons/bunkers/turrets to them instead of trading into them.
    inline bool GuardianSiegeActive(int guardians) { return guardians >= 1; }
    // A Guardian group big enough to survive alone holds the enemy's next base between attacks.
    inline bool GuardianContain(int guardians, bool baseThreatened) { return !baseThreatened && guardians >= 4; }
    // While Guardians deny the opponent's expansions, keep taking more of our own.
    inline bool ContainExpansion(int guardians, int miningSites, int drones) {
        return guardians >= 4 && miningSites >= 2 && miningSites < 7 && drones >= miningSites * 10;
    }
}
