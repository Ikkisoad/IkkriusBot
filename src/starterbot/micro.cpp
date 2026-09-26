#include "micro.h"
#include "Units.h"
#include "BWAPI.h" // Ensure this header is included for TILE_SIZE definition
#include <random>
#include "../../visualstudio/BasesTools.h"
#include "Tools.h"
#include "CombatPolicy.h"
#include "MatchLog.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

enum MicroMode { Neutral, Aggressive, Defensive };
int safeRange = 64;

namespace Micro {
    static MicroMode mode = MicroMode::Neutral;

    void SetMode(MicroMode newMode) { mode = newMode; }
    MicroMode GetMode() { return mode; }
}

// React to visible threats at every depot, including expansions under construction.
BWAPI::Unitset Micro::GetBaseThreats() {
    BWAPI::Unitset threats;
    for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
        if (!enemy->exists() || !enemy->isVisible() ||
            !BWAPI::Broodwar->self()->isEnemy(enemy->getPlayer())) continue;
        if (!enemy->getType().canAttack() && !enemy->getType().isSpellcaster() &&
            enemy->getType() != BWAPI::UnitTypes::Terran_Bunker) continue;
        for (auto depot : BWAPI::Broodwar->self()->getUnits()) {
            if (depot->getType().isResourceDepot() && depot->getDistance(enemy) <= 12 * 32) {
                threats.insert(enemy);
                break;
            }
        }
    }
    return threats;
}

bool Micro::DefendBases(BWAPI::Unit unit, const BWAPI::Unitset& threats) {
    if (!unit || !unit->isCompleted() || unit->isMorphing() || unit->isLoaded() ||
        unit->getType().isWorker() || unit->getType().isBuilding()) return false;
    BWAPI::Unit target = nullptr;
    for (auto threat : threats) {
        if (!unit->canAttack(threat)) continue;
        if (!target || unit->getDistance(threat) < unit->getDistance(target)) target = threat;
    }
    if (!target) return false;
    SmartAttackUnit(unit, target);
    return true;
}

void Micro::SmartAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target)
{
    if (!attacker || !target) return;
    if (attacker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    if (!attacker->isIdle() &&
        attacker->getLastCommand().getType() == BWAPI::UnitCommandTypes::Attack_Unit &&
        attacker->getLastCommand().getTarget() == target) return;
    attacker->attack(target);
    BWAPI::Broodwar->drawCircleMap(target->getPosition(), 3, BWAPI::Colors::Red, true);
}

void Micro::SmartMove(BWAPI::Unit unit, BWAPI::Position position)
{
    if (!unit) return;
    if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    if (!unit->isIdle() && unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Move &&
        unit->getLastCommand().getTargetPosition() == position) return;

    // Only check walkability for ground units
    if (!unit->getType().isFlyer()) {
        int tileX = position.x / 32;
        int tileY = position.y / 32;
        if (!BWAPI::Broodwar->isWalkable(tileX * 4, tileY * 4)) return;
    }

    unit->move(position);
    BWAPI::Broodwar->drawCircleMap(BWAPI::Position(position), 5, BWAPI::Colors::Green, true);
}

void Micro::SmartKiteTarget(BWAPI::Unit rangedUnit, BWAPI::Unit target)
{
    if (!rangedUnit || !target) return;
    int weaponRange = rangedUnit->getType().groundWeapon().maxRange();
    const auto threatWeapon = rangedUnit->isFlying() ? target->getType().airWeapon() : target->getType().groundWeapon();
    // Kiting only pays when we outrange the target; otherwise stand and trade.
    if (threatWeapon == BWAPI::WeaponTypes::None || rangedUnit->getGroundWeaponCooldown() == 0 ||
        !CombatPolicy::KiteWorthwhile(weaponRange, target->getPlayer()->weaponMaxRange(threatWeapon)))
    {
        SmartAttackUnit(rangedUnit, target);
    }
    else
    {
        BWAPI::Position fleePosition = rangedUnit->getPosition() - (target->getPosition() - rangedUnit->getPosition());
        SmartMove(rangedUnit, fleePosition);
    }
}

void Micro::SmartFleeUntilHealed(BWAPI::Unit meleeUnit, BWAPI::Unit enemyUnit) {
    if (!meleeUnit) return;

    if (!enemyUnit) {
        enemyUnit = Units::GetNearestEnemyUnit(meleeUnit);
        if (!enemyUnit) return;
    }
    if (meleeUnit->getType().groundWeapon().maxRange() > 32 || enemyUnit->getType().groundWeapon().maxRange() > 32 || enemyUnit->getType().isBuilding()) return; // Only for melee vs melee

    // Calculate flee direction (opposite of enemy), fixed length (2 tiles)
    BWAPI::Position myPos = meleeUnit->getPosition();
    BWAPI::Position enemyPos = enemyUnit->getPosition();
    int dx = myPos.x - enemyPos.x;
    int dy = myPos.y - enemyPos.y;
    double length = std::sqrt(dx * dx + dy * dy);
    const int FLEE_DISTANCE = 32; // 1 tiles

    int fleeX = myPos.x;
    int fleeY = myPos.y;
    if (length > 0.0) {
        fleeX += static_cast<int>(FLEE_DISTANCE * dx / length);
        fleeY += static_cast<int>(FLEE_DISTANCE * dy / length);
    }
    BWAPI::Position fleeVector(fleeX, fleeY);

    // Check if the flee position is walkable
    int tileX = fleeVector.x / 32;
    int tileY = fleeVector.y / 32;
    bool isWalkable = meleeUnit->getType().isFlyer() || BWAPI::Broodwar->isWalkable(tileX * 4, tileY * 4);

    if (!isWalkable) {
        // Try to the right (perpendicular to flee direction)
        int perpDx = -dy;
        int perpDy = dx;
        double perpLength = std::sqrt(perpDx * perpDx + perpDy * perpDy);
        const int SIDE_STEP = 32; // 1 tile to the side

        if (perpLength > 0.0) {
            // Try right
            int rightX = fleeX + static_cast<int>(SIDE_STEP * perpDx / perpLength);
            int rightY = fleeY + static_cast<int>(SIDE_STEP * perpDy / perpLength);
            int rightTileX = rightX / 32;
            int rightTileY = rightY / 32;
            if (BWAPI::Broodwar->isWalkable(rightTileX * 4, rightTileY * 4)) {
                fleeVector = BWAPI::Position(rightX, rightY);
            } else {
                // Try left
                int leftX = fleeX - static_cast<int>(SIDE_STEP * perpDx / perpLength);
                int leftY = fleeY - static_cast<int>(SIDE_STEP * perpDy / perpLength);
                int leftTileX = leftX / 32;
                int leftTileY = leftY / 32;
                if (BWAPI::Broodwar->isWalkable(leftTileX * 4, leftTileY * 4)) {
                    fleeVector = BWAPI::Position(leftX, leftY);
                }
                // If neither is walkable, fallback to original fleeVector (will fail in SmartMove)
            }
        }
    }

    SmartMove(meleeUnit, fleeVector);
}

// SmartScoutMove: Move a scout to a target position only if no other friendly unit is already moving there
void Micro::SmartScoutMove(BWAPI::Unit scout, BWAPI::Position targetPos)
{
    if (!scout) return;
    if (targetPos == BWAPI::Positions::None) return;

    // Check if another friendly unit is already moving to this position
    for (auto unit : BWAPI::Broodwar->getAllUnits()) {
        if (unit->getPlayer() == BWAPI::Broodwar->self() &&
            unit != scout &&
            unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Move &&
            unit->getOrderTargetPosition() == targetPos) {
            // Another unit is already moving to this position
            return;
        }
    }

    // If not already being scouted, move the scout to the target position
    SmartMove(scout, targetPos);
}

void Micro::ScoutAndWander(BWAPI::Unit scout)
{
    if (!scout) return;
    // Flee if being attacked
    if (scout->isUnderAttack()) {
        // Find the nearest enemy unit
        BWAPI::Unit nearestEnemy = Units::GetNearestEnemyUnit(scout);
        if (nearestEnemy) {
            // Flee away from the nearest enemy
            BWAPI::Position myPos = scout->getPosition();
            BWAPI::Position enemyPos = nearestEnemy->getPosition();
            int dx = myPos.x - enemyPos.x;
            int dy = myPos.y - enemyPos.y;
            double length = std::sqrt(dx * dx + dy * dy);
            const int FLEE_DISTANCE = 64; // 2 tiles

            int fleeX = myPos.x;
            int fleeY = myPos.y;
            if (length > 0.0) {
                fleeX += static_cast<int>(FLEE_DISTANCE * dx / length);
                fleeY += static_cast<int>(FLEE_DISTANCE * dy / length);
            }
            BWAPI::Position fleeVector(fleeX, fleeY);

            // Only check walkability for ground units
            if (!scout->getType().isFlyer()) {
                int tileX = fleeVector.x / 32;
                int tileY = fleeVector.y / 32;
                if (!BWAPI::Broodwar->isWalkable(tileX * 4, tileY * 4)) {
                    // If not walkable, just move in a random direction
                    fleeVector = myPos + BWAPI::Position(rand() % 128 - 64, rand() % 128 - 64);
                }
            }

            scout->move(fleeVector);
            return;
        }
    }
    auto orderPos = scout->getOrderTargetPosition();
    if (!scout->isIdle() && orderPos != BWAPI::Positions::None && !BWAPI::Broodwar->isExplored(BWAPI::TilePosition(orderPos))) return;

    // First, try to scout the nearest unexplored starting location
    const auto& startLocations = BWAPI::Broodwar->getStartLocations();
    BWAPI::TilePosition nearestUnexplored;
    int minDist = std::numeric_limits<int>::max();
    bool foundUnexplored = false;
    for (const auto& startLoc : startLocations)
    {
        if (!BWAPI::Broodwar->isExplored(startLoc))
        {
            const int dist = scout->getDistance(BWAPI::Position(startLoc));
            if (dist < minDist)
            {
                auto alreadyBeingScouted = false;
                for (auto unit : BWAPI::Broodwar->getAllUnits()) {
                    if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Move && unit->getOrderTargetPosition() == BWAPI::Position(startLoc)) {
                        alreadyBeingScouted = true;
                        break;
                    }
                }
				if (alreadyBeingScouted) continue; // Skip if already being scouted
                minDist = dist;
                nearestUnexplored = startLoc;
                foundUnexplored = true;
            }
        }
    }
    if (foundUnexplored)
    {
        SmartMove(scout, BWAPI::Position(nearestUnexplored));
        return;
    }

    // Then, try to scout unexplored bases
    const auto& basePositions = BasesTools::GetBWEMBases();
    for (const auto& pos : basePositions)
    {
        const BWAPI::TilePosition tilePos(pos);
        if (!BWAPI::Broodwar->isExplored(tilePos))
        {
            SmartMove(scout, pos);
            return;
        }
    }

    // If all bases and start locations are explored, wander randomly
    const int mapWidth = BWAPI::Broodwar->mapWidth() * 32;
    const int mapHeight = BWAPI::Broodwar->mapHeight() * 32;
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> distX(0, mapWidth - 1);
    std::uniform_int_distribution<int> distY(0, mapHeight - 1);

    for (int tries = 0; tries < 10; ++tries)
    {
        int x = distX(rng);
        int y = distY(rng);
        BWAPI::Position pos(x, y);
        int tileX = x / 32;
        int tileY = y / 32;
        if (scout->getType().isFlyer() || BWAPI::Broodwar->isWalkable(tileX * 4, tileY * 4))
        {
            SmartMove(scout, pos);
            return;
        }
    }
}

// TODO Rabge safety - If a ranged unit kills us in two hits, don't enter its range
// TODO Consider enemy lethal range as its range + 2 tiles
void Micro::SmartAvoidLethalAndAttackNonLethal(BWAPI::Unit unit, bool alwaysAvoid)
{
    if (!unit) return;

    Units unitsInstance;
    // If the unit is stuck, attack the nearest enemy unit
    if (unit->isStuck()) {
        BWAPI::Unit nearest = Units::GetNearestEnemyUnit(unit);
        if (nearest) {
            SmartAttackUnit(unit, nearest);
        }
        return;
    }

    auto enemies = unitsInstance.GetNearbyEnemyUnits(unit, 640);

    // --- Lethal building prioritization: attack at all costs ---
    BWAPI::Unit lethalBuilding = nullptr; // Declare it once at the top of the relevant scope
    int lethalBuildingDist = std::numeric_limits<int>::max();
    for (auto enemy : enemies) {
        if (!enemy || !enemy->exists()) continue;
        if (!enemy->getType().isBuilding()) continue;

        BWAPI::WeaponType weapon = unit->getType().isFlyer() ? enemy->getType().airWeapon() : enemy->getType().groundWeapon();
        int damage = weapon.damageAmount();
        int unitHP = unit->getHitPoints() + unit->getShields();
        bool isLethal = (damage > 0 && damage * 2 >= unitHP);

        int dist = unit->getDistance(enemy);
        if (isLethal && dist < lethalBuildingDist) {
            lethalBuilding = enemy;
            lethalBuildingDist = dist;
        }
    }
    if (lethalBuilding) {
        BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Attack lethal building");
        SmartAttackUnit(unit, lethalBuilding);
        return;
    }
	bool cantFlee = false; // Flag to indicate if fleeing is possible
    // --- Find the closest lethal enemy in range ---
    BWAPI::Unit closestLethal = nullptr;
    int range = -1;
    int minLethalDist = std::numeric_limits<int>::max();
    for (auto enemy : enemies)
    {
        if (!enemy || !enemy->exists()) continue;
        if (enemy->getType().isBuilding()) continue; // Already handled above

        BWAPI::WeaponType weapon = unit->getType().isFlyer() ? enemy->getType().airWeapon() : enemy->getType().groundWeapon();
        int damage = weapon.damageAmount();
        range = weapon.maxRange();
        if (range <= 0) range = 32; // fallback for melee

        int unitHP = unit->getHitPoints() + unit->getShields();
        bool isLethal = (damage > 0 && damage * 2 >= unitHP);
        cantFlee = range - unit->getType().groundWeapon().maxRange() > safeRange;

        int dist = unit->getDistance(enemy);
        const int RANGE_BUFFER = safeRange;
        if (isLethal && dist <= range + RANGE_BUFFER) {
            if (dist < minLethalDist) {
                minLethalDist = dist;
                closestLethal = enemy;
            }
        }
    }

    // --- Group decision: fight together when the local battle is winnable, hit-and-run when close ---
    const auto fight = AssessLocalFight(unit, 320);
    const auto engagement = alwaysAvoid ? CombatPolicy::Engagement::Withdraw
                                        : CombatPolicy::AssessEngagement(fight.friendlyPower, fight.enemyPower,
                                              CombatPolicy::TakeCloseFight(BWAPI::Broodwar->getFrameCount()));
    if (closestLethal && !cantFlee) {
        const bool ranged = unit->getType().groundWeapon().maxRange() > 32;
        const int cooldown = unit->getGroundWeaponCooldown();
        const int maxHp = unit->getType().maxHitPoints() + unit->getType().maxShields();
        if (CombatPolicy::ShouldStepBack(engagement, ranged, cooldown, unit->getHitPoints() + unit->getShields(), maxHp)) {
            // Fall back toward nearby allies; a lone unit heads home to meet reinforcements.
            const auto anchor = fight.allies > 1 ? fight.allyCenter : BWAPI::Position(BasesTools::GetMainBasePosition());
            FallBack(unit, closestLethal, anchor);
            int x = unit->getPosition().x + 5;
            int y = unit->getPosition().y + 5;
            BWAPI::Broodwar->drawTextMap(BWAPI::Position(x, y), engagement == CombatPolicy::Engagement::Withdraw ? "Withdraw" : "Hit and run");
            return;
        }
    }

    // --- Focus fire: the group converges on the same target ---
    BWAPI::Unitset candidates;
    for (auto enemy : enemies) {
        if (!enemy || !enemy->exists()) continue;
        // While trading at even odds, don't dive past threats to chase a far-away target.
        if (engagement != CombatPolicy::Engagement::Commit && unit->getDistance(enemy) > 256) continue;
        candidates.insert(enemy);
    }
    BWAPI::Unit bestTarget = ChooseFocusTarget(unit, candidates, false);

    if (!bestTarget) {
        // If the unit is stuck, attack the nearest enemy unit
        if (unitsInstance.GetNearbyEnemyUnits(unit, 8).size() > 0 && unit->getGroundWeaponCooldown() == 0) {
            BWAPI::Unit nearest = Units::GetNearestEnemyUnit(unit);
            if (nearest) {
                SmartAttackUnit(unit, nearest);
                BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Attack nearest");
            }
            return;
        }
        BWAPI::Broodwar->drawTextMap(unit->getPosition(), "No targets");
        return;
    }

    SmartAttackUnit(unit, bestTarget);
    BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Focus target");
}

// Send our idle workers to mine minerals so they don't just stand there
void Micro::sendIdleWorkersToMinerals()
{
    // Let's send all of our starting workers to the closest mineral to them
    // First we need to loop over all of the units that we (BWAPI::Broodwar->self()) own
    const BWAPI::Unitset& myUnits = BWAPI::Broodwar->self()->getUnits();
    for (auto& unit : myUnits)
    {
        // Check the unit type, if it is an idle worker, then we want to send it somewhere
        if (unit->getType().isWorker() && unit->isIdle())
        {
            // Mine at one of our bases, never at a distant patch
            Tools::GatherNearestBaseMinerals(unit);
        }
    }
}

void Micro::GatherMinerals(BWAPI::Unit unit) {  
    if (!unit) return; // Check for nullness to address C26429  

    // Mine at one of our bases, never at a distant patch
    Tools::GatherNearestBaseMinerals(unit);
}

void Micro::GatherResources(BWAPI::Unit unit) {
    if (!unit) return; // Check for nullness to address C26429  

	BWAPI::Unit extractor = nullptr;
    int workersOnGas = 0;

    for (auto u : BWAPI::Broodwar->self()->getUnits()) {
        if (u->isCarryingGas() || u->isGatheringGas()) {
            workersOnGas++;
		}
        if (u->getType() == BWAPI::UnitTypes::Zerg_Extractor && u->isCompleted() &&
            (!extractor || unit->getDistance(u) < unit->getDistance(extractor))) {
            extractor = u;
        }
	}

    if (workersOnGas < 3 && extractor) {
        // If we already have enough workers on gas, gather minerals
        unit->rightClick(extractor);
        return;
	}

    // Mine at one of our bases, never at a distant patch
    Tools::GatherNearestBaseMinerals(unit);
}

void Micro::SmartGatherMinerals(BWAPI::Unit drone)
{
    if (!drone || !drone->exists()) return;
    if (!drone->isCompleted() || drone->isConstructing()) return;
    if (!drone->getType().isWorker()) return;

    // If the drone is already performing a move command, let it finish its current command
    if (drone->getOrder() == BWAPI::Orders::Move) return;
    // Prevent issuing gather if already gathering minerals
    if (drone->getOrder() == BWAPI::Orders::MiningMinerals ||
        drone->getOrder() == BWAPI::Orders::Harvest1 ||
        drone->getOrder() == BWAPI::Orders::Harvest2)
    {
        return;
    }

    // If holding minerals, return to the closest hatchery
    if (drone->isCarryingMinerals())
    {
        // Find the closest hatchery
        BWAPI::Unit closestHatchery = nullptr;
        int minDist = std::numeric_limits<int>::max();
        for (auto& unit : BWAPI::Broodwar->self()->getUnits())
        {
            if (!unit->exists()) continue;
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery ||
                unit->getType() == BWAPI::UnitTypes::Zerg_Lair ||
                unit->getType() == BWAPI::UnitTypes::Zerg_Hive)
            {
                int dist = drone->getDistance(unit);
                if (dist < minDist)
                {
                    minDist = dist;
                    closestHatchery = unit;
                }
            }
        }

        if (closestHatchery)
        {
            const int RETURN_RADIUS = 32; // 1 tile
            const int distToHatchery = drone->getDistance(closestHatchery);

            if (distToHatchery < RETURN_RADIUS)
            {
                // If close enough, return cargo
                if (drone->getOrder() != BWAPI::Orders::ReturnMinerals)
                {
                    drone->returnCargo();
                }
            }
            else
            {
                // If not close enough, move to hatchery
                if (drone->getOrderTarget() != closestHatchery || drone->getOrder() != BWAPI::Orders::Move)
                {
                    drone->move(closestHatchery->getPosition());
                }
            }
        }
        return;
    }

    // If not holding minerals, pick a patch at one of our bases
    BWAPI::Unit closestMineral = Tools::GetMineralForWorker(drone);

    if (closestMineral)
    {
        // If close enough, gather
        if (drone->getDistance(closestMineral) < 64) // 2 tiles
        {
            // Only issue gather if the last command was not already a gather command
            if (drone->getLastCommand().getType() != BWAPI::UnitCommandTypes::Gather) {
                if (drone->getOrder() != BWAPI::Orders::MiningMinerals &&
                    drone->getOrder() != BWAPI::Orders::Harvest1 &&
                    drone->getOrder() != BWAPI::Orders::Harvest2)
                {
                    drone->gather(closestMineral);
                }
            }
        }
        else
        {
            // Move to mineral patch if not already moving there
            if (drone->getOrderTarget() != closestMineral || drone->getOrder() != BWAPI::Orders::Move)
            {
                drone->move(closestMineral->getPosition());
            }
        }
    }
}

void Micro::unitAttack(BWAPI::Unit unit) {
    if (!unit) return;
    const BWAPI::Position enemyBase = BasesTools::GetEnemyBasePosition();
    if (enemyBase == BWAPI::Positions::None) {
        Micro::ScoutAndWander(unit);
    }
    else {
        if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) { return; }
        if (BasesTools::IsAreaEnemyBase(unit->getPosition(), 3)) {
            //Units::AttackNearestNonLethalEnemyUnit(unit);
            Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
        }
        else {
            SmartAttackMove(unit, enemyBase);
        }
    }
}

void Micro::attack() {
    const BWAPI::Position enemyBase = BasesTools::GetEnemyBasePosition();
    const BWAPI::Unitset& myUnits = BWAPI::Broodwar->self()->getUnits();

    for (auto& unit : myUnits) {
        if (!unit->getType().isWorker() && unit->getType() != BWAPI::UnitTypes::Zerg_Overlord) {
            // If there are enemies nearby, use micro logic
            Units unitsInstance;
            auto enemies = unitsInstance.GetNearbyEnemyUnits(unit, 640);
            if (!enemies.empty()) {
                Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
                continue;
            }

            if (enemyBase == BWAPI::Positions::None) {
                Micro::ScoutAndWander(unit);
            } else {
                if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) { continue; }
                if (BasesTools::IsAreaEnemyBase(unit->getPosition(), 3)) {
                    //Units::AttackNearestNonLethalEnemyUnit(unit);
                    Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
                } else {
                    SmartAttackMove(unit, enemyBase);
                }
            }
        }
    }
}

void Micro::BasicAttackAndScoutLoop(BWAPI::Unitset myUnits) {
    const auto threats = GetBaseThreats();
    // Army center, so leading units wait for the rest instead of arriving one at a time.
    int armyX = 0, armyY = 0, armySize = 0;
    for (auto unit : myUnits) {
        const auto type = unit->getType();
        if (!unit->exists() || !unit->isCompleted() || unit->isMorphing() || unit->isBurrowed() || unit->isLoaded() ||
            type.isWorker() || type.isBuilding() || !type.canAttack()) continue;
        armyX += unit->getPosition().x; armyY += unit->getPosition().y; ++armySize;
    }
    const auto armyCenter = armySize > 0 ? BWAPI::Position(armyX / armySize, armyY / armySize) : BWAPI::Positions::None;
    const auto enemyBase = BasesTools::GetEnemyBasePosition();
    for (auto& unit : myUnits) {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord) {
            Micro::ScoutAndWander(unit);
            continue;
        }
        if (unit->getType().isWorker()) {
            if (unit->isIdle() && !Tools::HasPendingConstruction(unit) &&
                unit->getLastCommandFrame() < BWAPI::Broodwar->getFrameCount()) Micro::GatherResources(unit);
            continue;
        }
        if (DefendBases(unit, threats)) continue;
        if (!unit->getType().isBuilding()) {
            if (unit->isMorphing() || unit->isBurrowed() || unit->isLoaded()) continue;

            auto enemies = unit->getUnitsInRadius(640, BWAPI::Filter::IsEnemy && BWAPI::Filter::Exists);

            switch (static_cast<int>(Micro::GetMode())) {
                case static_cast<int>(MicroMode::Aggressive):
                    if (!enemies.empty()) {
                        Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
                    } else if (armySize > 1 && enemyBase.isValid() && unit->getDistance(armyCenter) > 384 &&
                               unit->getDistance(enemyBase) + 256 < armyCenter.getApproxDistance(enemyBase)) {
                        Micro::SmartAttackMove(unit, armyCenter);
                    } else {
                        Micro::unitAttack(unit);
                    }
                    break;

                case static_cast<int>(MicroMode::Defensive):
                    if (!enemies.empty()) {
                        Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
                    } else {
                        Micro::Retreat(unit);
                    }
                    break;

                case static_cast<int>(MicroMode::Neutral):
                    Micro::SmartAvoidLethalAndAttackNonLethal(unit, true);
                default:
                    if (!enemies.empty()) {
                        // Avoid combat: flee from nearest offensive enemy
                        BWAPI::Unit nearestOffensive = nullptr;
                        int minDist = 645;
                        for (auto enemy : enemies) {
                            if (!enemy || !enemy->exists()) continue;
                            if (enemy->getType().canAttack() && !enemy->getType().isWorker() && !enemy->getType().isBuilding()) {
                                int dist = unit->getDistance(enemy);
                                if (dist < minDist) {
                                    minDist = dist;
                                    nearestOffensive = enemy;
                                }
                            }
                        }
                        if (nearestOffensive) {
                            Micro::Flee(unit, nearestOffensive);
                        } else {
                            Micro::Flee(unit, *enemies.begin());
                        }
                    } else {
                        // Out on the map, avoid combat, scout/wander
                        Micro::ScoutAndWander(unit);
                    }
                    break;
            }
        }
    }
}

void Micro::Retreat(BWAPI::Unit unit) {
    if (!unit) return;
    if (unit->isMorphing() || unit->isBurrowed() || unit->isLoaded()) return;
    SmartMove(unit, BWAPI::Position(BasesTools::GetMainBasePosition()));
}

void Micro::Flee(BWAPI::Unit unit, BWAPI::Unit closestLethal) {
    if (!unit || !closestLethal) return;

    // Flee from the closest lethal enemy in range, but always slightly to the right or left
    BWAPI::Position myPos = unit->getPosition();
    BWAPI::Position lethalPos = closestLethal->getPosition();
    int dx = myPos.x - lethalPos.x;
    int dy = myPos.y - lethalPos.y;
    double length = std::sqrt(dx * dx + dy * dy);

    const int FLEE_DISTANCE = 32;
    const int SIDE_STEP = 16; // Slightly to the side (half a tile)
    int fleeX = myPos.x;
    int fleeY = myPos.y;
    if (length > 0.0) {
        fleeX += static_cast<int>(FLEE_DISTANCE * dx / length);
        fleeY += static_cast<int>(FLEE_DISTANCE * dy / length);

        // Perpendicular vector (right: -dy, dx)
        int perpDx = -dy;
        int perpDy = dx;
        double perpLength = std::sqrt(perpDx * perpDx + perpDy * perpDy);
        if (perpLength > 0.0) {
            // Alternate between right and left based on unit id for determinism
            int side = (unit->getID() % 2 == 0) ? 1 : -1;
            fleeX += static_cast<int>(side * SIDE_STEP * perpDx / perpLength);
            fleeY += static_cast<int>(side * SIDE_STEP * perpDy / perpLength);
        }
    }
    BWAPI::Position fleeVector(fleeX, fleeY);

    // Validate if the tile is walkable, if not flee directly to the right or left
    int tileX = fleeVector.x / 32;
    int tileY = fleeVector.y / 32;
    // Validate if the tile is walkable and not occupied by any unit
   //bool isOccupied = false;
   //for (const auto& u : BWAPI::Broodwar->getAllUnits()) {
   //    if (!u || !u->exists()) continue;
   //    // Use a small radius to check for overlap (8 pixels)
   //    if (u->getPosition().getDistance(BWAPI::Position(fleeVector)) < 32) {
   //        isOccupied = true;
   //        break;
   //    }
   //}
   //bool isWalkable = unit->getType().isFlyer() || BWAPI::Broodwar->isWalkable(tileX * 4, tileY * 4) || !isOccupied;
    if (BWAPI::Broodwar->getUnitsOnTile(fleeVector.x, fleeVector.y).size() > 0) {
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), fleeVector, BWAPI::Colors::Purple);
        // If not walkable, try to flee strictly perpendicular (left/right or up/down)
        // Determine if horizontal or vertical flee is more open
        int absDx = std::abs(dx);
        int absDy = std::abs(dy);
        const int SIDE_ONLY_STEP = 32; // 1 tile

        // Try horizontal (left/right)
        int leftX = myPos.x - SIDE_ONLY_STEP;
        int leftY = myPos.y;
        int rightX = myPos.x + SIDE_ONLY_STEP;
        int rightY = myPos.y;
        bool leftWalkable = BWAPI::Broodwar->isWalkable((leftX / 32) * 4, (leftY / 32) * 4);
        bool rightWalkable = BWAPI::Broodwar->isWalkable((rightX / 32) * 4, (rightY / 32) * 4);

        // Try vertical (up/down)
        int upX = myPos.x;
        int upY = myPos.y - SIDE_ONLY_STEP;
        int downX = myPos.x;
        int downY = myPos.y + SIDE_ONLY_STEP;
        bool upWalkable = BWAPI::Broodwar->isWalkable((upX / 32) * 4, (upY / 32) * 4);
        bool downWalkable = BWAPI::Broodwar->isWalkable((downX / 32) * 4, (downY / 32) * 4);

        // Prefer the direction perpendicular to the main flee vector
        if (absDx > absDy) {
            // Flee up or down
            if (upWalkable) {
                fleeVector = BWAPI::Position(upX, upY);
            }
            else if (downWalkable) {
                fleeVector = BWAPI::Position(downX, downY);
            }
            else if (leftWalkable) {
                fleeVector = BWAPI::Position(leftX, leftY);
            }
            else if (rightWalkable) {
                fleeVector = BWAPI::Position(rightX, rightY);
            }
            // else fallback to original fleeVector (will fail in SmartMove)
        }
        else {
            // Flee left or right
            if (leftWalkable) {
                fleeVector = BWAPI::Position(leftX, leftY);
            }
            else if (rightWalkable) {
                fleeVector = BWAPI::Position(rightX, rightY);
            }
            else if (upWalkable) {
                fleeVector = BWAPI::Position(upX, upY);
            }
            else if (downWalkable) {
                fleeVector = BWAPI::Position(downX, downY);
            }
            // else fallback to original fleeVector (will fail in SmartMove)
        }
        // Try to the right (perpendicular to flee direction)
        int perpDx = -dy;
        int perpDy = dx;
        double perpLength = std::sqrt(perpDx * perpDx + perpDy * perpDy);
        if (perpLength > 0.0) {
            // Try right
            int rightX = myPos.x + static_cast<int>(SIDE_ONLY_STEP * perpDx / perpLength);
            int rightY = myPos.y + static_cast<int>(SIDE_ONLY_STEP * perpDy / perpLength);
            int rightTileX = rightX / 32;
            int rightTileY = rightY / 32;
            if (BWAPI::Broodwar->isWalkable(rightTileX * 4, rightTileY * 4)) {
                fleeVector = BWAPI::Position(rightX, rightY);
            }
            else {
                // Try left
                int leftX = myPos.x - static_cast<int>(SIDE_ONLY_STEP * perpDx / perpLength);
                int leftY = myPos.y - static_cast<int>(SIDE_ONLY_STEP * perpDy / perpLength);
                int leftTileX = leftX / 32;
                int leftTileY = leftY / 32;
                if (BWAPI::Broodwar->isWalkable(leftTileX * 4, leftTileY * 4)) {
                    fleeVector = BWAPI::Position(leftX, leftY);
                }
                // If neither is walkable, fallback to original fleeVector (will fail in SmartMove)
            }
        }
    }

    BWAPI::Broodwar->drawLineMap(unit->getPosition(), lethalPos, BWAPI::Colors::Cyan);
	auto range = closestLethal->getType().groundWeapon().maxRange();
    BWAPI::Broodwar->drawBoxMap(BWAPI::Position(lethalPos.x - range, lethalPos.y - range), BWAPI::Position(lethalPos.x + range, lethalPos.y + range), BWAPI::Colors::Cyan);
    BWAPI::Broodwar->drawBoxMap(BWAPI::Position(lethalPos.x - (range + safeRange), lethalPos.y - (range + safeRange)), BWAPI::Position(lethalPos.x + range + safeRange, lethalPos.y + range + safeRange), BWAPI::Colors::Teal);
    SmartMove(unit, fleeVector);
    BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Fleeing");
    return;
}

namespace {
    double CombatPower(BWAPI::Unit unit) {
        const auto type = unit->getType();
        if (!type.canAttack() || type.isWorker()) return 0;
        return std::max(2, type.supplyRequired()) *
            double(unit->getHitPoints() + unit->getShields()) / std::max(1, type.maxHitPoints() + type.maxShields());
    }
}

// Compare the strength of our units around this one against the enemies able to hurt it.
Micro::LocalFight Micro::AssessLocalFight(BWAPI::Unit unit, int radius) {
    LocalFight fight;
    if (!unit) return fight;
    int x = 0, y = 0;
    auto nearbyUnits = unit->getUnitsInRadius(radius);
    nearbyUnits.insert(unit);
    for (auto nearby : nearbyUnits) {
        const auto type = nearby->getType();
        if (!nearby->exists() || !nearby->isCompleted()) continue;
        const double power = CombatPower(nearby);
        if (nearby->getPlayer() == BWAPI::Broodwar->self()) {
            if (power <= 0) continue;
            fight.friendlyPower += power;
            x += nearby->getPosition().x; y += nearby->getPosition().y; ++fight.allies;
        } else if (BWAPI::Broodwar->self()->isEnemy(nearby->getPlayer()) && nearby->isVisible()) {
            const auto weapon = unit->isFlying() ? type.airWeapon() : type.groundWeapon();
            if (weapon == BWAPI::WeaponTypes::None && type != BWAPI::UnitTypes::Terran_Bunker) continue;
            fight.enemyPower += type.isBuilding() ? std::max(2.0, power) + 6 : power;
        }
    }
    if (fight.allies > 0) fight.allyCenter = BWAPI::Position(x / fight.allies, y / fight.allies);
    return fight;
}

// Pick a target the whole nearby group can agree on instead of each unit chasing its nearest enemy.
BWAPI::Unit Micro::ChooseFocusTarget(BWAPI::Unit unit, const BWAPI::Unitset& candidates, bool preferWorkers) {
    if (!unit) return nullptr;
    BWAPI::Unit best = nullptr;
    double bestScore = std::numeric_limits<double>::max();
    for (auto enemy : candidates) {
        if (!enemy || !enemy->exists() || !enemy->isVisible() || !enemy->isDetected()) continue;
        const auto ownWeapon = enemy->isFlying() ? unit->getType().airWeapon() : unit->getType().groundWeapon();
        if (ownWeapon == BWAPI::WeaponTypes::None) continue;
        const auto type = enemy->getType();
        const auto enemyWeapon = unit->isFlying() ? type.airWeapon() : type.groundWeapon();
        const bool armed = enemyWeapon != BWAPI::WeaponTypes::None || type == BWAPI::UnitTypes::Terran_Bunker;
        int tier = 4;
        if (armed && !type.isWorker()) tier = 0;
        else if (type.isWorker()) tier = 1;
        else if (!type.isBuilding() && type != BWAPI::UnitTypes::Zerg_Larva && type != BWAPI::UnitTypes::Zerg_Egg) tier = 2;
        else if (type.isBuilding() && enemy->isCompleted()) tier = 3;
        if (preferWorkers && tier <= 1) tier = 1 - tier;
        int allies = 0;
        for (auto ally : enemy->getUnitsInRadius(320, BWAPI::Filter::IsOwned)) {
            if (ally != unit && ally->getOrderTarget() == enemy) ++allies;
        }
        const int maxHp = std::max(1, type.maxHitPoints() + type.maxShields());
        const double hpFraction = double(enemy->getHitPoints() + enemy->getShields()) / maxHp;
        const double score = CombatPolicy::FocusScore(tier, unit->getDistance(enemy), ownWeapon.maxRange(), hpFraction, allies);
        if (score < bestScore) { bestScore = score; best = enemy; }
    }
    return best;
}

// Back away from a threat while drifting toward the group, so retreating units regroup instead of scattering.
void Micro::FallBack(BWAPI::Unit unit, BWAPI::Unit threat, BWAPI::Position anchor) {
    if (!unit || !threat) return;
    const auto position = unit->getPosition();
    double awayX = position.x - threat->getPosition().x, awayY = position.y - threat->getPosition().y;
    const double awayLength = std::sqrt(awayX * awayX + awayY * awayY);
    if (awayLength > 0) { awayX /= awayLength; awayY /= awayLength; }
    double dx = awayX, dy = awayY;
    if (anchor.isValid() && unit->getDistance(anchor) > 64) {
        const double ax = anchor.x - position.x, ay = anchor.y - position.y;
        const double anchorLength = std::sqrt(ax * ax + ay * ay);
        dx += ax / anchorLength; dy += ay / anchorLength;
    }
    double length = std::sqrt(dx * dx + dy * dy);
    // Anchor straight through the threat: plain retreat beats running into it.
    if (length < 0.3) { dx = awayX; dy = awayY; length = awayLength > 0 ? 1.0 : 0.0; }
    if (length <= 0) { if (anchor.isValid()) SmartMove(unit, anchor); return; }
    BWAPI::Position destination(position.x + int(96 * dx / length), position.y + int(96 * dy / length));
    destination.makeValid();
    const auto command = unit->getLastCommand();
    if (command.getType() == BWAPI::UnitCommandTypes::Move && !unit->isIdle() &&
        command.getTargetPosition().getApproxDistance(destination) < 48 &&
        BWAPI::Broodwar->getFrameCount() - unit->getLastCommandFrame() < 8) return;
    if (!unit->isFlying() && !BWAPI::Broodwar->isWalkable(BWAPI::WalkPosition(destination))) {
        Flee(unit, threat);
        return;
    }
    BWAPI::Broodwar->drawLineMap(position, destination, BWAPI::Colors::Orange);
    SmartMove(unit, destination);
}
void Micro::SmartAttackMove(BWAPI::Unit unit, BWAPI::Position position) {
    if (!unit || !position.isValid() || unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    const auto command = unit->getLastCommand();
    if (!unit->isIdle() && command.getType() == BWAPI::UnitCommandTypes::Attack_Move &&
        command.getTargetPosition().getApproxDistance(position) < 64) return;
    unit->attack(position);
}

void Micro::GroundArmyLoop(BWAPI::Unit unit, const BWAPI::Unitset& threats, BWAPI::Position rally, BWAPI::Position center) {
    const bool lurker = unit->getType() == BWAPI::UnitTypes::Zerg_Lurker;
    if (lurker) { LurkerSupportLoop(unit, threats, rally, center); return; }
    BWAPI::Unit target = nullptr;
    bool defending = false;
    for (auto threat : threats) {
        if (threat->isFlying() && unit->getType().airWeapon() == BWAPI::WeaponTypes::None) continue;
        if (!threat->isDetected()) continue;
        if (!target || unit->getDistance(threat) < unit->getDistance(target)) target = threat;
    }
    defending = target != nullptr;
    double friendlyPower = 0, enemyPower = 0;
    BWAPI::Unitset candidates;
    for (auto nearby : unit->getUnitsInRadius(320)) {
        const auto type = nearby->getType();
        if (!nearby->exists() || !nearby->isCompleted()) continue;
        const double power = (type.canAttack() && !type.isWorker() ? std::max(2, type.supplyRequired()) : 0) *
            double(nearby->getHitPoints() + nearby->getShields()) / std::max(1, type.maxHitPoints() + type.maxShields());
        if (nearby->getPlayer() == BWAPI::Broodwar->self()) friendlyPower += power;
        else if (BWAPI::Broodwar->self()->isEnemy(nearby->getPlayer()) && nearby->isVisible()) {
            if (type.groundWeapon() != BWAPI::WeaponTypes::None || type == BWAPI::UnitTypes::Terran_Bunker)
                enemyPower += type.isBuilding() ? power + 6 : power;
            if (defending || !nearby->isDetected() || (nearby->isFlying() && unit->getType().airWeapon() == BWAPI::WeaponTypes::None)) continue;
            candidates.insert(nearby);
        }
    }
    // Focus fire with the nearby group rather than each unit taking its nearest enemy.
    if (!defending) target = ChooseFocusTarget(unit, candidates, false);
    const auto engagement = CombatPolicy::AssessEngagement(friendlyPower, enemyPower,
        CombatPolicy::TakeCloseFight(BWAPI::Broodwar->getFrameCount()));
    if (!defending && engagement == CombatPolicy::Engagement::Withdraw) {
        SmartMove(unit, rally);
        return;
    }
    // Hit and run between shots, falling back toward the group rather than splitting off alone.
    if (!defending && target && CombatPolicy::ShouldStepBack(engagement, unit->getType().groundWeapon().maxRange() > 32,
        unit->getGroundWeaponCooldown(), unit->getHitPoints(), unit->getType().maxHitPoints())) {
        FallBack(unit, target, center);
        return;
    }
    if (target) {
        if (lurker) SmartMove(unit, target->getPosition());
        else SmartAttackUnit(unit, target);
        return;
    }
    if (GetMode() != MicroMode::Aggressive) { SmartAttackMove(unit, rally); return; }
    if (unit->getDistance(center) > 320) { SmartAttackMove(unit, center); return; }
    const auto enemyBase = BasesTools::GetEnemyBasePosition();
    if (enemyBase.isValid()) SmartAttackMove(unit, enemyBase);
    else ScoutAndWander(unit);
}

namespace {
    std::map<int, int> lurkerLastContact;
    BWAPI::Position UnitCenter(const BWAPI::Unitset& units, BWAPI::Position fallback) {
        if (units.empty()) return fallback;
        int x = 0, y = 0;
        for (auto unit : units) { x += unit->getPosition().x; y += unit->getPosition().y; }
        return BWAPI::Position(x / static_cast<int>(units.size()), y / static_cast<int>(units.size()));
    }

    // Longest distance at which the enemy can hit an air unit; -1 when it cannot.
    int AirThreatRange(BWAPI::Unit enemy) {
        if (!enemy->isCompleted()) return -1;
        const auto type = enemy->getType();
        if (type == BWAPI::UnitTypes::Terran_Bunker)
            return enemy->getPlayer()->weaponMaxRange(BWAPI::WeaponTypes::Gauss_Rifle) + 32;
        if (type == BWAPI::UnitTypes::Protoss_Carrier) return 8 * 32; // Interceptor launch range
        const auto weapon = type.airWeapon();
        if (weapon == BWAPI::WeaponTypes::None) return -1;
        return enemy->getPlayer()->weaponMaxRange(weapon);
    }
    bool IsStaticAntiAir(BWAPI::Unit enemy) { return enemy->getType().isBuilding() && AirThreatRange(enemy) >= 0; }
    // Mobile threats close distance while we reposition, so give them a wider berth.
    int ThreatMargin(BWAPI::Unit enemy) { return enemy->getType().isBuilding() ? 32 : 64; }

    // Step directly away from every threat at once; flyers ignore terrain.
    BWAPI::Position AwayFrom(BWAPI::Unit unit, const std::vector<BWAPI::Unit>& threats, BWAPI::Position fallback) {
        double dx = 0, dy = 0;
        for (auto threat : threats) {
            const double x = unit->getPosition().x - threat->getPosition().x, y = unit->getPosition().y - threat->getPosition().y;
            const double length = std::max(1.0, std::sqrt(x * x + y * y));
            dx += x / length; dy += y / length;
        }
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.01) return fallback.isValid() ? fallback : unit->getPosition();
        BWAPI::Position away(unit->getPosition().x + int(96 * dx / length), unit->getPosition().y + int(96 * dy / length));
        away.makeValid();
        return away;
    }

    // Mutalisk raid squad, kept stable across frames.
    std::set<int> harassSquad, harassRegen;
    int harassRetreatUntil = 0;
    BWAPI::Position harassTarget = BWAPI::Positions::None;
    std::map<int, BWAPI::Position> enemyDepots;
    std::vector<std::pair<BWAPI::Position, int>> harassAvoid; // Defended or empty bases, until frame
    struct RaidOrders { BWAPI::Unitset raiders; BWAPI::Position target = BWAPI::Positions::None, center = BWAPI::Positions::None; bool retreat = false; };

    bool HarassAvoided(BWAPI::Position position) {
        for (const auto& avoid : harassAvoid)
            if (avoid.first.getApproxDistance(position) <= 320) return true;
        return false;
    }

    void EndRaid(const std::string& reason) {
        if (!harassSquad.empty()) MatchLog::Event("harass_end", reason);
        harassSquad.clear(); harassRegen.clear();
        harassTarget = BWAPI::Positions::None;
    }

    RaidOrders UpdateRaidSquad(const BWAPI::Unitset& mutalisks, const BWAPI::Unitset& reserved, BWAPI::Position home) {
        const int frame = BWAPI::Broodwar->getFrameCount();
        // Remember enemy depots so raids can move on once a base is cleared or defended.
        for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
            if (enemy->exists() && enemy->isVisible() && BWAPI::Broodwar->self()->isEnemy(enemy->getPlayer()) &&
                enemy->getType().isResourceDepot()) enemyDepots[enemy->getID()] = enemy->getPosition();
        }
        for (auto it = enemyDepots.begin(); it != enemyDepots.end();) {
            const auto depot = BWAPI::Broodwar->getUnit(it->first);
            if (BWAPI::Broodwar->isVisible(BWAPI::TilePosition(it->second)) && (!depot || !depot->exists())) it = enemyDepots.erase(it);
            else ++it;
        }
        harassAvoid.erase(std::remove_if(harassAvoid.begin(), harassAvoid.end(),
            [frame](const std::pair<BWAPI::Position, int>& avoid) { return avoid.second <= frame; }), harassAvoid.end());

        std::vector<BWAPI::Unit> available;
        for (auto muta : mutalisks) if (!reserved.contains(muta)) available.push_back(muta);
        const int size = CombatPolicy::HarassSquadSize(static_cast<int>(available.size()), Micro::GetMode() == Micro::MicroMode::Aggressive);
        if (size == 0) { EndRaid(Micro::GetMode() == Micro::MicroMode::Aggressive ? "main_attack" : "too_few"); return {}; }

        std::set<int> alive;
        for (auto muta : available) alive.insert(muta->getID());
        for (auto squad : {&harassSquad, &harassRegen})
            for (auto it = squad->begin(); it != squad->end();) it = alive.count(*it) ? std::next(it) : squad->erase(it);
        // Top up with the healthiest free Mutalisks.
        std::sort(available.begin(), available.end(), [](BWAPI::Unit a, BWAPI::Unit b) { return a->getHitPoints() > b->getHitPoints(); });
        for (auto muta : available) if (static_cast<int>(harassSquad.size()) < size) harassSquad.insert(muta->getID());

        RaidOrders orders;
        BWAPI::Unitset active;
        double power = 0;
        for (auto muta : available) {
            if (!harassSquad.count(muta->getID())) continue;
            orders.raiders.insert(muta);
            if (harassRegen.count(muta->getID())) continue;
            active.insert(muta);
            power += 2.0 * muta->getHitPoints() / std::max(1, muta->getType().maxHitPoints());
        }
        orders.center = active.empty() ? home : UnitCenter(active, home);

        double antiAir = 0;
        for (auto enemy : BWAPI::Broodwar->getUnitsInRadius(orders.center, 320, BWAPI::Filter::IsEnemy)) {
            if (!enemy->isVisible() || AirThreatRange(enemy) < 0) continue;
            const auto type = enemy->getType();
            antiAir += type.isBuilding() ? CombatPolicy::StaticAntiAirPower :
                std::max(1, type.supplyRequired()) / 2.0 * (enemy->getHitPoints() + enemy->getShields()) / std::max(1, type.maxHitPoints() + type.maxShields());
        }
        if (frame >= harassRetreatUntil && !active.empty() && CombatPolicy::HarassAbort(power, antiAir)) {
            harassAvoid.push_back({harassTarget.isValid() ? harassTarget : orders.center, frame + 24 * 90});
            harassRetreatUntil = frame + 24 * 8;
            harassTarget = BWAPI::Positions::None;
            MatchLog::Event("harass_abort", "power=" + std::to_string(int(power)) + " anti_air=" + std::to_string(int(antiAir)));
        }
        // Too many raiders regenerating: the rest wait at home instead of raiding alone.
        orders.retreat = frame < harassRetreatUntil || static_cast<int>(active.size()) < CombatPolicy::HarassSquadMin;
        if (orders.retreat) return orders;

        // A mineral line with nobody left to kill is not worth hovering over.
        if (harassTarget.isValid() && orders.center.getApproxDistance(harassTarget) <= 192) {
            bool workers = false;
            for (auto enemy : BWAPI::Broodwar->getUnitsInRadius(harassTarget, 320, BWAPI::Filter::IsEnemy))
                workers = workers || (enemy->isVisible() && enemy->getType().isWorker());
            if (!workers) { harassAvoid.push_back({harassTarget, frame + 24 * 45}); harassTarget = BWAPI::Positions::None; }
        }
        if (!harassTarget.isValid() || HarassAvoided(harassTarget)) {
            BWAPI::Position best = BWAPI::Positions::None;
            for (const auto& depot : enemyDepots) {
                if (HarassAvoided(depot.second)) continue;
                if (!best.isValid() || orders.center.getApproxDistance(depot.second) < orders.center.getApproxDistance(best)) best = depot.second;
            }
            const auto enemyMain = BasesTools::GetEnemyBasePosition();
            if (!best.isValid() && enemyMain.isValid() && !HarassAvoided(enemyMain)) best = enemyMain;
            if (best.isValid()) MatchLog::Event("harass_start", "raiders=" + std::to_string(orders.raiders.size()) +
                " target=" + std::to_string(best.x) + "," + std::to_string(best.y));
            harassTarget = best;
        }
        orders.target = harassTarget;
        return orders;
    }
}

void Micro::ResetCombatState() {
    lurkerLastContact.clear();
    harassSquad.clear(); harassRegen.clear(); enemyDepots.clear(); harassAvoid.clear();
    harassRetreatUntil = 0;
    harassTarget = BWAPI::Positions::None;
}

std::vector<BWAPI::Unitset> Micro::GetHydraGroups(const BWAPI::Unitset& units) {
    std::vector<BWAPI::Unit> unassigned;
    for (auto unit : units) {
        if (unit->exists() && unit->isCompleted() && !unit->isMorphing() &&
            unit->getType() == BWAPI::UnitTypes::Zerg_Hydralisk) unassigned.push_back(unit);
    }
    std::sort(unassigned.begin(), unassigned.end(), [](BWAPI::Unit a, BWAPI::Unit b) { return a->getID() < b->getID(); });
    std::vector<BWAPI::Unitset> groups;
    while (!unassigned.empty()) {
        auto anchor = unassigned.front();
        BWAPI::Unitset group;
        for (auto it = unassigned.begin(); it != unassigned.end();) {
            if (group.size() < CombatPolicy::HydrasPerGroup && anchor->getDistance(*it) <= 512) {
                group.insert(*it); it = unassigned.erase(it);
            } else ++it;
        }
        groups.push_back(group);
    }
    return groups;
}

void Micro::LurkerSupportLoop(BWAPI::Unit unit, const BWAPI::Unitset& threats, BWAPI::Position rally, BWAPI::Position center) {
    if (!unit || !unit->isCompleted() || unit->isMorphing() || unit->isLoaded()) return;
    const int frame = BWAPI::Broodwar->getFrameCount();
    const auto command = unit->getLastCommand();
    if (unit->getLastCommandFrame() >= frame || !unit->isInterruptible() ||
        ((command.getType() == BWAPI::UnitCommandTypes::Burrow || command.getType() == BWAPI::UnitCommandTypes::Unburrow) &&
         frame - unit->getLastCommandFrame() <= BWAPI::Broodwar->getLatencyFrames() + 24)) return;
    const int range = unit->getType().groundWeapon().maxRange();
    BWAPI::Unit target = nullptr;
    // A distant base raid must not make a deployed Lurker ignore enemies already in its firing lane.
    for (auto enemy : unit->getUnitsInRadius(range + 96, BWAPI::Filter::IsEnemy)) {
        if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || enemy->isFlying()) continue;
        if (!target || unit->getDistance(enemy) < unit->getDistance(target)) target = enemy;
    }
    const bool inRange = target && unit->getDistance(target) <= range;
    if (inRange) lurkerLastContact[unit->getID()] = frame;
    if (unit->isBurrowed()) {
        if (inRange) { SmartAttackUnit(unit, target); return; }
        const auto seen = lurkerLastContact.find(unit->getID());
        if (unit->getGroundWeaponCooldown() > 0 || (seen != lurkerLastContact.end() && frame - seen->second < 72)) return;
        if (unit->canUnburrow() && unit->unburrow()) MatchLog::Event("lurker_unburrow", std::to_string(unit->getID()));
        return;
    }
    if (inRange) {
        if (unit->canBurrow() && unit->burrow()) MatchLog::Event("lurker_burrow", std::to_string(unit->getID()));
        return;
    }
    if (target) { SmartMove(unit, target->getPosition()); return; }
    for (auto threat : threats) {
        if (!threat->isFlying() && threat->isDetected() && (!target || unit->getDistance(threat) < unit->getDistance(target))) target = threat;
    }
    if (target) { SmartMove(unit, target->getPosition()); return; }
    if (unit->getDistance(center) > 192) { SmartMove(unit, center); return; }
    const auto enemyBase = BasesTools::GetEnemyBasePosition();
    if (GetMode() == MicroMode::Aggressive && enemyBase.isValid()) SmartMove(unit, enemyBase);
    else SmartMove(unit, center.isValid() ? center : rally);
}

void Micro::DevourerEscortLoop(BWAPI::Unit unit, BWAPI::Position escort) {
    // Devourers support the Mutalisk flock: only engage air the flock is fighting too, never alone.
    constexpr int FlockReach = 320;
    BWAPI::Unit target = nullptr;
    for (auto enemy : unit->getUnitsInRadius(384, BWAPI::Filter::IsEnemy)) {
        if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !enemy->isFlying() || !unit->canAttack(enemy)) continue;
        if (escort.isValid() && enemy->getPosition().getApproxDistance(escort) > FlockReach) continue;
        if (!target || unit->getDistance(enemy) < unit->getDistance(target)) target = enemy;
    }
    // Attack first: issuing an escort move first prevents the attack in the same frame.
    if (target) { SmartAttackUnit(unit, target); return; }
    SmartMove(unit, escort);
}

void Micro::HiveTechMicroLoop(BWAPI::Unitset myUnits, const BWAPI::Unitset& pressureWave) {
    const auto threats = GetBaseThreats();
    auto rally = BWAPI::Position(BasesTools::GetMainBasePosition());
    const auto enemyBase = BasesTools::GetEnemyBasePosition();
    BWAPI::Unitset mutalisks, guardians, devourers, queens, combat, defaults;
    BWAPI::Unit scout = nullptr;
    for (auto unit : myUnits) {
        if (!unit->exists() || !unit->isCompleted() || unit->isLoaded() || unit->isMorphing()) continue;
        const auto type = unit->getType();
        if (type.isResourceDepot() && enemyBase.isValid() && unit->getDistance(enemyBase) < rally.getDistance(enemyBase)) rally = unit->getPosition();
        if (type == BWAPI::UnitTypes::Zerg_Overlord && (!scout || unit->getID() < scout->getID())) scout = unit;
        if (type == BWAPI::UnitTypes::Zerg_Queen) queens.insert(unit);
        else if (type == BWAPI::UnitTypes::Zerg_Mutalisk) mutalisks.insert(unit);
        else if (type == BWAPI::UnitTypes::Zerg_Guardian) guardians.insert(unit);
        else if (type == BWAPI::UnitTypes::Zerg_Devourer) devourers.insert(unit);
        if (!type.isWorker() && !type.isBuilding() && type.canAttack()) combat.insert(unit);
    }
    // Raiders leave the main flock, so escort and regroup centers ignore them.
    const auto raid = UpdateRaidSquad(mutalisks, pressureWave, rally);
    for (auto raider : raid.raiders) mutalisks.erase(raider);
    const auto center = UnitCenter(combat, rally);
    // Without an air group, free Queens and Devourers follow the main army instead of idling at home.
    const auto airCenter = !guardians.empty() ? UnitCenter(guardians, rally) : UnitCenter(mutalisks, center);
    // Devourers fly with the Mutalisks (the raid squad when it holds every Mutalisk).
    const auto flockCenter = !mutalisks.empty() ? UnitCenter(mutalisks, center) :
        !raid.raiders.empty() ? raid.center : airCenter;
    const auto groups = GetHydraGroups(myUnits);
    const bool queenNestReady = Tools::CountUnitOfType(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0;
    std::map<int, BWAPI::Position> hydraCenters, queenEscorts;
    std::map<int, BWAPI::Unit> hydraQueens;
    BWAPI::Unitset freeQueens = queens;
    int supported = 0;
    for (const auto& group : groups) {
        const auto groupCenter = UnitCenter(group, rally);
        auto queen = Tools::GetClosestUnitTo(groupCenter, freeQueens);
        if (queen) {
            freeQueens.erase(queen);
            queenEscorts[queen->getID()] = groupCenter;
            ++supported;
        }
        for (auto hydra : group) { hydraCenters[hydra->getID()] = groupCenter; hydraQueens[hydra->getID()] = queen; }
    }
    for (auto queen : freeQueens) queenEscorts[queen->getID()] = airCenter;
    if (BWAPI::Broodwar->getFrameCount() % 120 == 0)
        MatchLog::Event("queen_support", "hydra_groups=" + std::to_string(groups.size()) + " supported=" + std::to_string(supported) +
            " queens=" + std::to_string(queens.size()));

    for (auto unit : myUnits) {
        if (!unit->exists() || !unit->isCompleted() || unit->isLoaded() || unit->isMorphing()) continue;
        const auto type = unit->getType();
        if (pressureWave.contains(unit) && threats.empty() &&
            (type == BWAPI::UnitTypes::Zerg_Zergling || type == BWAPI::UnitTypes::Zerg_Hydralisk || type == BWAPI::UnitTypes::Zerg_Mutalisk)) {
            BWAPI::Unit target = nullptr;
            for (auto enemy : unit->getUnitsInRadius(320, BWAPI::Filter::IsEnemy)) {
                if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !unit->canAttack(enemy)) continue;
                if (!target || unit->getDistance(enemy) < unit->getDistance(target)) target = enemy;
            }
            if (target) SmartAttackUnit(unit, target);
            else if (enemyBase.isValid()) SmartAttackMove(unit, enemyBase);
            else ScoutAndWander(unit);
            continue;
        }
        if (type == BWAPI::UnitTypes::Zerg_Hydralisk) {
            auto queen = hydraQueens[unit->getID()];
            // Let the assigned Queen catch up before advancing, but always fight local threats.
            if (GetMode() == MicroMode::Aggressive && threats.empty() &&
                queenNestReady &&
                (!queen || queen->getDistance(unit) > 384) && unit->getUnitsInRadius(320, BWAPI::Filter::IsEnemy).empty())
                SmartAttackMove(unit, hydraCenters[unit->getID()]);
            else GroundArmyLoop(unit, threats, rally, hydraCenters[unit->getID()]);
        } else if (type == BWAPI::UnitTypes::Zerg_Lurker) {
            auto escort = center;
            int distance = std::numeric_limits<int>::max();
            for (const auto& group : groups) {
                const auto position = UnitCenter(group, rally);
                if (unit->getDistance(position) < distance) { escort = position; distance = unit->getDistance(position); }
            }
            LurkerSupportLoop(unit, threats, rally, escort);
        } else if (type == BWAPI::UnitTypes::Zerg_Zergling || type == BWAPI::UnitTypes::Zerg_Ultralisk) {
            GroundArmyLoop(unit, threats, rally, center);
        } else if (type == BWAPI::UnitTypes::Zerg_Queen) {
            if (QueenCastLoop(unit, BWAPI::Broodwar->getAllUnits())) continue;
            BWAPI::Unit danger = nullptr;
            for (auto enemy : unit->getUnitsInRadius(256, BWAPI::Filter::IsEnemy)) {
                if (enemy->getType().airWeapon() != BWAPI::WeaponTypes::None &&
                    unit->getDistance(enemy) < enemy->getType().airWeapon().maxRange() + 32) { danger = enemy; break; }
            }
            if (danger) { Flee(unit, danger); continue; }
            auto escort = queenEscorts[unit->getID()];
            const auto towardHome = rally - escort;
            const double length = std::sqrt(double(towardHome.x) * towardHome.x + double(towardHome.y) * towardHome.y);
            if (length > 96) escort += BWAPI::Position(int(towardHome.x * 96 / length), int(towardHome.y * 96 / length));
            if (escort.isValid() && unit->getDistance(escort) > 64) SmartMove(unit, escort);
        } else if (type == BWAPI::UnitTypes::Zerg_Overlord) {
            BWAPI::Unit danger = nullptr;
            for (auto enemy : unit->getUnitsInRadius(320, BWAPI::Filter::IsEnemy)) {
                if (enemy->getType().airWeapon() != BWAPI::WeaponTypes::None) { danger = enemy; break; }
            }
            if (danger) Flee(unit, danger);
            else if (unit == scout && !enemyBase.isValid()) ScoutAndWander(unit);
            else SmartMove(unit, unit == scout && GetMode() == MicroMode::Aggressive ? center : rally);
        } else if (type == BWAPI::UnitTypes::Zerg_Mutalisk && raid.raiders.contains(unit)) {
            // Raiders already in the enemy mineral line finish the job instead of flying home.
            if ((!raid.target.isValid() || unit->getDistance(raid.target) > 320) && DefendBases(unit, threats)) continue;
            const bool regen = CombatPolicy::HarassNeedsRegen(unit->getHitPoints(), type.maxHitPoints(), harassRegen.count(unit->getID()) > 0);
            if (regen) harassRegen.insert(unit->getID()); else harassRegen.erase(unit->getID());
            MutaliskRaidLoop(unit, raid.target, raid.center, rally, raid.retreat || regen);
        } else if (type == BWAPI::UnitTypes::Zerg_Mutalisk || type == BWAPI::UnitTypes::Zerg_Guardian || type == BWAPI::UnitTypes::Zerg_Devourer) {
            if (DefendBases(unit, threats)) continue;
            if (GetMode() != MicroMode::Aggressive) { SmartMove(unit, rally); continue; }
            if (type == BWAPI::UnitTypes::Zerg_Devourer) DevourerEscortLoop(unit, flockCenter);
            else if (type == BWAPI::UnitTypes::Zerg_Guardian)
                GuardianAssaultLoop(unit, BWAPI::Broodwar->getAllUnits(), UnitCenter(devourers.empty() ? mutalisks : devourers, rally));
            else if (unit->getDistance(UnitCenter(mutalisks, rally)) > 256 && unit->getUnitsInRadius(224, BWAPI::Filter::IsEnemy).empty())
                SmartMove(unit, UnitCenter(mutalisks, rally));
            else MutaliskHarassLoop(unit, BWAPI::Broodwar->getAllUnits());
        } else defaults.insert(unit);
    }
    BasicAttackAndScoutLoop(defaults);
}

void Micro::MutaliskHarassLoop(BWAPI::Unit muta, BWAPI::Unitset enemies) {
    if (!muta) return;

    const int range = std::max(muta->getType().groundWeapon().maxRange(), muta->getType().airWeapon().maxRange());
    
    // Find threats and targets
    auto nearbyEnemies = muta->getUnitsInRadius(range + 128, BWAPI::Filter::IsEnemy);
    
    BWAPI::Unitset candidates;
    BWAPI::Unit worstThreat = nullptr;
    int minThreatDist = 99999;

    for (auto enemy : nearbyEnemies) {
        if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !muta->canAttack(enemy)) continue;
        // Threat analysis
        const int threatRange = AirThreatRange(enemy);
        if (threatRange >= 0) {
            int dist = muta->getDistance(enemy);
            if (dist < minThreatDist && dist <= threatRange + 64) {
                minThreatDist = dist;
                worstThreat = enemy;
            }
        }
        candidates.insert(enemy);
    }

    // Workers first while harassing, but the whole flock converges on one target.
    const auto fight = AssessLocalFight(muta, 288);
    const auto engagement = CombatPolicy::AssessEngagement(fight.friendlyPower, fight.enemyPower,
        CombatPolicy::TakeCloseFight(BWAPI::Broodwar->getFrameCount()));
    BWAPI::Unit bestTarget = ChooseFocusTarget(muta, candidates, engagement != CombatPolicy::Engagement::Commit);

    const int cooldown = bestTarget && bestTarget->isFlying() ? muta->getAirWeaponCooldown() : muta->getGroundWeaponCooldown();
    // Only kite what we outrange; against longer range, retreating on cooldown just donates free shots.
    const bool kiteWorthwhile = worstThreat && CombatPolicy::KiteWorthwhile(muta->getPlayer()->weaponMaxRange(muta->getType().airWeapon()), AirThreatRange(worstThreat));
    if (worstThreat && ((cooldown > 0 && kiteWorthwhile) || engagement == CombatPolicy::Engagement::Withdraw)) {
        // Kite back toward the flock so it stays stacked
        BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Kiting!");
        FallBack(muta, worstThreat, fight.allies > 1 ? fight.allyCenter : BWAPI::Position(BasesTools::GetMainBasePosition()));
    } else if (bestTarget && cooldown == 0) {
        BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Attacking!");
        SmartAttackUnit(muta, bestTarget);
    } else if (!bestTarget) {
        // Move towards enemy base
        BWAPI::Position targetPos = BasesTools::GetEnemyBasePosition();
        if (targetPos != BWAPI::Positions::None) {
            BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Moving to enemy");
            SmartMove(muta, targetPos);
        } else {
            BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Scouting");
            ScoutAndWander(muta);
        }
    }
}

void Micro::MutaliskRaidLoop(BWAPI::Unit muta, BWAPI::Position raidTarget, BWAPI::Position squadCenter, BWAPI::Position home, bool retreat) {
    if (!muta) return;
    if (retreat) {
        BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Raid: regroup");
        SmartMove(muta, home);
        return;
    }
    const int range = muta->getPlayer()->weaponMaxRange(muta->getType().airWeapon());
    std::vector<BWAPI::Unit> threats, staticDefense, candidates;
    bool outranged = false;
    for (auto enemy : muta->getUnitsInRadius(320, BWAPI::Filter::IsEnemy)) {
        if (!enemy->exists() || !enemy->isVisible()) continue;
        const int threatRange = AirThreatRange(enemy);
        if (threatRange >= 0) {
            if (IsStaticAntiAir(enemy)) staticDefense.push_back(enemy);
            if (muta->getDistance(enemy) <= threatRange + ThreatMargin(enemy)) {
                threats.push_back(enemy);
                outranged = outranged || !CombatPolicy::KiteWorthwhile(range, threatRange);
            }
        }
        if (enemy->isDetected() && muta->canAttack(enemy)) candidates.push_back(enemy);
    }
    // Workers first, then harmless units, then anything that shoots back; never under static anti-air.
    const auto rank = [](BWAPI::Unit enemy) {
        if (enemy->getType().isWorker()) return 0;
        if (enemy->getType().isBuilding()) return 3;
        return AirThreatRange(enemy) < 0 ? 1 : 2;
    };
    BWAPI::Unit target = nullptr;
    for (auto enemy : candidates) {
        bool covered = false;
        for (auto defense : staticDefense)
            covered = covered || (defense != enemy && defense->getDistance(enemy) <= AirThreatRange(defense) + 32);
        if (covered) continue;
        if (!target || rank(enemy) < rank(target) ||
            (rank(enemy) == rank(target) && enemy->getHitPoints() + enemy->getShields() < target->getHitPoints() + target->getShields()) ||
            (rank(enemy) == rank(target) && enemy->getHitPoints() + enemy->getShields() == target->getHitPoints() + target->getShields() &&
             muta->getDistance(enemy) < muta->getDistance(target))) target = enemy;
    }
    const int cooldown = target && target->isFlying() ? muta->getAirWeaponCooldown() : muta->getGroundWeaponCooldown();
    if (cooldown > 0 && !threats.empty() && !outranged) {
        BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Raid: kite");
        SmartMove(muta, AwayFrom(muta, threats, home));
        return;
    }
    if (target) {
        BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Raid: attack");
        SmartAttackUnit(muta, target);
        return;
    }
    // Travel as a flock so the raid arrives with its full damage.
    if (squadCenter.isValid() && muta->getDistance(squadCenter) > 160) { SmartMove(muta, squadCenter); return; }
    if (raidTarget.isValid()) SmartMove(muta, raidTarget);
    else ScoutAndWander(muta);
}

void Micro::GuardianAssaultLoop(BWAPI::Unit guardian, BWAPI::Unitset enemies, BWAPI::Position fallback) {
    if (!guardian) return;
    const int range = guardian->getPlayer()->weaponMaxRange(guardian->getType().groundWeapon());
    if (!fallback.isValid()) fallback = BWAPI::Position(BasesTools::GetMainBasePosition());

    std::vector<BWAPI::Unit> threats;
    BWAPI::Unit outranger = nullptr, target = nullptr, approach = nullptr;
    // Kill what can shoot air first, then the army, then workers, then buildings.
    const auto rank = [](BWAPI::Unit enemy) {
        if (AirThreatRange(enemy) >= 0) return 0;
        if (enemy->getType().isWorker()) return 2;
        if (enemy->getType().isBuilding()) return 3;
        return 1;
    };
    for (auto enemy : guardian->getUnitsInRadius(range + 320, BWAPI::Filter::IsEnemy)) {
        if (!enemy->exists() || !enemy->isVisible()) continue;
        const int dist = guardian->getDistance(enemy);
        const int threatRange = AirThreatRange(enemy);
        if (threatRange >= 0 && dist <= threatRange + ThreatMargin(enemy)) {
            threats.push_back(enemy);
            if (!CombatPolicy::KiteWorthwhile(range, threatRange)) outranger = enemy;
        }
        if (enemy->isFlying() || !enemy->isDetected() || !guardian->canAttack(enemy)) continue;
        if (dist > range) {
            if (!approach || dist < guardian->getDistance(approach)) approach = enemy;
            continue;
        }
        if (!target || rank(enemy) < rank(target) ||
            (rank(enemy) == rank(target) && enemy->getHitPoints() + enemy->getShields() < target->getHitPoints() + target->getShields())) target = enemy;
    }

    // Anything that matches our range makes hit-and-run a losing trade: fall back to the escort.
    if (outranger) {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Siege: outranged");
        SmartMove(guardian, guardian->getDistance(fallback) > 96 ? fallback : AwayFrom(guardian, threats, fallback));
        return;
    }
    // Siege unit: while reloading, step out of every anti-air reach we outrange.
    if (guardian->getGroundWeaponCooldown() > 0) {
        if (!threats.empty()) {
            BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Siege: reposition");
            SmartMove(guardian, AwayFrom(guardian, threats, fallback));
        }
        return;
    }
    if (target) {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Sieging");
        SmartAttackUnit(guardian, target);
        return;
    }
    if (approach) {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Siege: approach");
        SmartAttackUnit(guardian, approach);
        return;
    }
    BWAPI::Position targetPos = BasesTools::GetEnemyBasePosition();
    if (targetPos != BWAPI::Positions::None) {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Assaulting base");
        SmartMove(guardian, targetPos);
    } else {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Scouting");
        ScoutAndWander(guardian);
    }
}

bool Micro::SpellReserved(BWAPI::TechType tech, BWAPI::Unit target, BWAPI::Position position) {
    for (auto ally : BWAPI::Broodwar->self()->getUnits()) {
        if (ally->getType() != BWAPI::UnitTypes::Zerg_Queen) continue;
        const auto command = ally->getLastCommand();
        if (BWAPI::Broodwar->getFrameCount() - ally->getLastCommandFrame() > BWAPI::Broodwar->getLatencyFrames() + 24) continue;
        if (command.getTechType() != tech) continue;
        if (target && command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit && command.getTarget() == target) return true;
        if (!target && command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Position &&
            command.getTargetPosition().getApproxDistance(position) <= 96) return true;
    }
    return false;
}

bool Micro::QueenCastLoop(BWAPI::Unit queen, BWAPI::Unitset enemies) {
    if (!queen || !queen->isCompleted() || queen->isMorphing()) return false;
    const auto command = queen->getLastCommand();
    const int age = BWAPI::Broodwar->getFrameCount() - queen->getLastCommandFrame();
    if (age <= 0 || !queen->isInterruptible() || queen->getSpellCooldown() > 0 ||
        ((command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit || command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Position) &&
         age <= BWAPI::Broodwar->getLatencyFrames() + 24)) return true;
    BWAPI::Unitset nearby;
    for (auto enemy : enemies) {
        if (enemy->exists() && enemy->isVisible() && BWAPI::Broodwar->self()->isEnemy(enemy->getPlayer()) && queen->getDistance(enemy) <= 9 * 32)
            nearby.insert(enemy);
    }
    const auto broodlings = BWAPI::TechTypes::Spawn_Broodlings;
    if (queen->getEnergy() >= broodlings.energyCost() && BWAPI::Broodwar->self()->hasResearched(broodlings)) {
        BWAPI::Unit target = nullptr;
        int bestValue = 0;
        for (auto enemy : nearby) {
            const auto type = enemy->getType();
            if (!enemy->isDetected() || enemy->isFlying() || type.isBuilding() ||
                SpellReserved(broodlings, enemy, enemy->getPosition()) || !queen->canUseTech(broodlings, enemy)) continue;
            int value = type.mineralPrice() + type.gasPrice() * 2;
            if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode || type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
                type == BWAPI::UnitTypes::Protoss_High_Templar || type == BWAPI::UnitTypes::Zerg_Defiler) value += 500;
            if (value >= 200 && value > bestValue) { target = enemy; bestValue = value; }
        }
        if (target && queen->useTech(broodlings, target)) {
            MatchLog::Event("queen_spell", "Spawn_Broodlings queen=" + std::to_string(queen->getID()) + " target=" + std::to_string(target->getID()));
            return true;
        }
    }
    const auto ensnare = BWAPI::TechTypes::Ensnare;
    if (queen->getEnergy() >= ensnare.energyCost() && BWAPI::Broodwar->self()->hasResearched(ensnare)) {
        BWAPI::Unit target = nullptr;
        int bestCluster = 2;
        for (auto candidate : nearby) {
            if (candidate->getType().isBuilding() || candidate->isEnsnared() ||
                SpellReserved(ensnare, nullptr, candidate->getPosition()) || !queen->canUseTech(ensnare, candidate->getPosition())) continue;
            int cluster = 0;
            for (auto enemy : nearby) {
                if (!enemy->getType().isBuilding() && enemy->getType().canAttack() && !enemy->isEnsnared() &&
                    enemy->getDistance(candidate) <= 96) ++cluster;
            }
            if (cluster > bestCluster) { target = candidate; bestCluster = cluster; }
        }
        if (target && queen->useTech(ensnare, target->getPosition())) {
            MatchLog::Event("queen_spell", "Ensnare queen=" + std::to_string(queen->getID()) + " targets=" + std::to_string(bestCluster));
            return true;
        }
    }
    // Keep enough energy for battle spells; Parasite is only a surplus-energy option.
    if (queen->getEnergy() >= 175) {
        for (auto enemy : nearby) {
            if (enemy->isParasited() || enemy->getType().isBuilding() || enemy->getType().supplyRequired() < 4 ||
                SpellReserved(BWAPI::TechTypes::Parasite, enemy, enemy->getPosition())) continue;
            if (queen->canUseTech(BWAPI::TechTypes::Parasite, enemy) && queen->useTech(BWAPI::TechTypes::Parasite, enemy)) {
                MatchLog::Event("queen_spell", "Parasite queen=" + std::to_string(queen->getID()) + " target=" + std::to_string(enemy->getID()));
                return true;
            }
        }
    }
    return false;
}
