#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

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
    // Attack as soon as the army clearly outweighs everything scouted instead of waiting for the full
    // attack size, which let the army grow far past the opponent's before it ever moved out.
    constexpr double AdvantageRatio = 1.6;
    constexpr int AdvantageMinArmy = 12; // Normal supply: a handful of units is not an army.
    inline bool AttackOnAdvantage(int armySupply, int knownEnemySupply, int attackSupply, bool scouted) {
        const int army = armySupply / 2;
        if (army < std::max(AdvantageMinArmy, attackSupply / 3)) return false;
        // Scouted and nothing seen: an army of half the normal size is already enough.
        if (knownEnemySupply <= 0) return scouted && army >= std::max(AdvantageMinArmy, attackSupply / 2);
        return armySupply >= knownEnemySupply * AdvantageRatio;
    }
    // Every idle minute since the last attack lowers the army size needed to go again, down to half.
    inline int PatientAttackSupply(int attackSupply, double idleSeconds) {
        const double factor = std::clamp(1.0 - std::max(0.0, idleSeconds) / 60.0 * 0.1, 0.5, 1.0);
        return std::max(AdvantageMinArmy, static_cast<int>(attackSupply * factor));
    }

    // Ground units march on the target; only units running ahead of the main body, out of contact, wait
    // for it. Units behind the body keep marching, so nothing is pulled backwards onto a midpoint.
    inline bool WaitForMainBody(int unitToTarget, int bodyToTarget, int unitToBody) {
        return unitToBody > 320 && unitToTarget + 192 < bodyToTarget;
    }
    // Center of the densest group of points (the army's main body), not the mean of every straggler:
    // the mean of a front line and fresh reinforcements at home lies in the middle of the map.
    template <class Point>
    Point MainBody(const std::vector<Point>& points, int radius, Point fallback) {
        if (points.empty()) return fallback;
        const long long r2 = 1LL * radius * radius;
        const auto within = [r2](const Point& a, const Point& b) {
            const long long dx = a.x - b.x, dy = a.y - b.y;
            return dx * dx + dy * dy <= r2;
        };
        size_t anchor = 0; int best = -1;
        for (size_t i = 0; i < points.size(); ++i) {
            int count = 0;
            for (const auto& other : points) count += within(points[i], other);
            if (count > best) { best = count; anchor = i; }
        }
        long long x = 0, y = 0; int n = 0;
        for (const auto& other : points)
            if (within(points[anchor], other)) { x += other.x; y += other.y; ++n; }
        Point center = fallback;
        center.x = static_cast<int>(x / n); center.y = static_cast<int>(y / n);
        return center;
    }

    // Gas never takes the drones minerals need: at least eight and two thirds of the drones stay on
    // minerals, and banked gas far beyond minerals pulls miners back off the geysers.
    inline int GasWorkerBudget(int drones, int extractors, int minerals, int gas) {
        int budget = std::min(extractors * 3, std::max(0, drones - std::max(8, drones * 2 / 3)));
        if (gas > 600 && gas > minerals) budget = std::min(budget, extractors);
        if (gas >= 1000 && gas > minerals * 4) budget = 0;
        return std::max(0, budget);
    }

    // Scourge against enemy air our other anti-air cannot handle: one Scourge hit (110 damage) per
    // 110 hit points of enemy air, scaled down by the Mutalisks/Hydras/Devourers already covering it.
    inline int ScourgeTarget(int enemyAirHitPoints, int enemyAirSupply, int ourAntiAirSupply) {
        if (enemyAirSupply <= 0 || enemyAirHitPoints <= 0) return 0;
        const double uncovered = std::clamp(1.0 - ourAntiAirSupply / (enemyAirSupply * 1.5), 0.0, 1.0);
        const int scourge = static_cast<int>(std::ceil(enemyAirHitPoints / 110.0 * uncovered));
        return std::min(24, (scourge + 1) / 2 * 2); // They hatch in pairs.
    }
    // Overkill guard: Scourge assigned to one target, enough to kill it and no more.
    inline int ScourgeNeeded(int hitPoints) { return std::max(1, (hitPoints + 109) / 110); }

    // Defilers join only very late: a long game with a big ground army, where Dark Swarm and Plague decide fights.
    inline int DefilerTarget(double minutes, int supplyUsed, int groundArmySupply) {
        const bool late = (minutes >= 20 && supplyUsed >= 300) || minutes >= 28;
        if (!late || groundArmySupply < 40) return 0;
        return std::clamp(groundArmySupply / 40 + (minutes >= 28 ? 1 : 0), 1, 4);
    }
    // Plague and Dark Swarm follow the Ensnare rule: allies caught count double.
    inline int PlagueScore(int enemies, int allies) {
        const int score = enemies - allies * 2;
        return enemies >= 4 && score >= 4 ? score : 0;
    }

    // A Queen with Spawn Broodlings energy goes out fishing for one valuable ground unit, as long as the
    // trip is short and little anti-air guards it. Zero means "not worth the trip".
    constexpr int BroodlingHuntRange = 32 * 60;
    inline double BroodlingHuntScore(int value, int distance, double antiAirPower) {
        if (value < 200 || distance > BroodlingHuntRange || antiAirPower > 3.0) return 0;
        const double score = value - distance * 0.25 - antiAirPower * 150;
        return score > 0 ? score : 0;
    }

    // Ground-heavy compositions link a forward Hatchery to the main with a Nydus Canal.
    inline bool WantsNydus(double groundShare, bool rushPending, int miningSites) {
        return groundShare >= 0.5 && !rushPending && miningSites >= 2;
    }
    // Take the Nydus when entering at one end and walking from the other saves real distance.
    inline bool NydusShortcut(int unitToEntrance, int exitToDestination, int unitToDestination) {
        return unitToEntrance <= 640 && unitToEntrance + exitToDestination + 320 < unitToDestination;
    }
}
