#include "micro.h"
#include "Units.h"
#include "BWAPI.h" // Ensure this header is included for TILE_SIZE definition
#include <random>
#include "../../BasesTools.h"
#include "Tools.h"

enum MicroMode { Neutral, Aggressive, Defensive };
int safeRange = 64;

namespace Micro {
    static MicroMode mode = MicroMode::Neutral;

    void SetMode(MicroMode newMode) { mode = newMode; }
    MicroMode GetMode() { return mode; }
}

void Micro::SmartAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target)
{
    if (!attacker || !target) return;
    if (attacker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    if (attacker->getLastCommand().getTarget() == target) return;
    attacker->attack(target);
    BWAPI::Broodwar->drawCircleMap(target->getPosition(), 3, BWAPI::Colors::Red, true);
}

void Micro::SmartMove(BWAPI::Unit unit, BWAPI::Position position)
{
    if (!unit) return;
    if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    if (unit->getLastCommand().getTargetPosition() == position) return;

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
    if (rangedUnit->getDistance(target) > weaponRange)
    {
        SmartMove(rangedUnit, target->getPosition());
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

// TODO Vicinity strength - test if there are enough ally units nearby to overwell the enemy
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
    // --- Flee if any other lethal enemy is in range ---
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

    if (closestLethal && !cantFlee) {
       /* if (!BasesTools::IsAreaEnemyBase(unit->getPosition(), 3)) {
			Retreat(unit);
        }
        else {*/
        Flee(unit, closestLethal);
        //}
        int x = unit->getPosition().x + 5;
        int y = unit->getPosition().y + 5;
        BWAPI::Broodwar->drawTextMap(BWAPI::Position(x, y), "Range %i", range);
        return;
    }

    // --- Target selection with priority system ---
    BWAPI::Unit bestTarget = nullptr;
    int bestPriority = 100;
    int minDist = std::numeric_limits<int>::max();
    bool bestTargetIsLethal = true; // Start with lethal as worst

    // For building targeting
    BWAPI::Unit lowestPercentHP = nullptr;
    double lowestPercent = 1.1; // > 100%
    int lowestPercentPriority = 3;
    int lowestAbsHP = std::numeric_limits<int>::max();

    for (auto enemy : enemies)
    {
        if (!enemy || !enemy->exists()) continue;
        int maxTargetDistance = 32 * 50; // Only skip full-HP buildings farther than this

        // Assign priority: 0 = offensive, 1 = worker, 2 = building, 3 = other
        int priority = 4;
        if (enemy->getType().canAttack() && !enemy->getType().isWorker() && !enemy->getType().isBuilding()) {
            priority = 0;
            maxTargetDistance = 8;
        }
        else if (enemy->getType().isWorker()) {
            maxTargetDistance = 16;
            priority = 1;
        }
        else if (enemy->getType().isBuilding() && enemy->isCompleted())
            priority = 2;
        else if (enemy->getType() == BWAPI::UnitTypes::Zerg_Larva || enemy->getType().isBuilding() && !enemy->isCompleted())
            priority = 3;

        BWAPI::WeaponType weapon = enemy->getType().groundWeapon();
        if (unit->getType().isFlyer()) weapon = enemy->getType().airWeapon();
        int damage = weapon.damageAmount();
        int unitHP = unit->getHitPoints() + unit->getShields();
        bool isLethal = (damage > 0 && damage * 2 >= unitHP) || alwaysAvoid;

        int dist = unit->getDistance(enemy);

        // Prefer non-lethal, then higher priority, then closer
        if ((!isLethal && bestTargetIsLethal) ||
            (isLethal == bestTargetIsLethal && priority < bestPriority) ||
            (isLethal == bestTargetIsLethal && priority == bestPriority && dist < minDist))
        {
            bestTarget = enemy;
            minDist = dist;
            bestTargetIsLethal = isLethal;
            
            if (priority <= bestPriority) {
                int hp = enemy->getHitPoints() + enemy->getShields();
                int maxHp = enemy->getType().maxHitPoints() + enemy->getType().maxShields();
                if (maxHp > 0) {
                    double percent = static_cast<double>(hp) / maxHp;
                    // Skip if is at 100% HP and too far away
                    if (percent >= 1.0 && dist > maxTargetDistance) {
                        if (percent < lowestPercent ||
                            (percent == lowestPercent && hp < lowestAbsHP) || priority > bestPriority) {
                            lowestPercent = percent;
                            lowestAbsHP = hp;
                            lowestPercentHP = enemy;
                            lowestPercentPriority = priority;
                        }
                    }
                }
            }
            bestPriority = priority;
        }
    }

    // If the best target is a building, prefer the lowest percent HP building (break ties with lowest HP)
    if (bestTarget && lowestPercentHP && lowestPercentPriority == bestPriority)
        bestTarget = lowestPercentHP;

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
    BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Attack best target");
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
            // Get the closest mineral to this worker unit
            BWAPI::Unit closestMineral = Tools::GetClosestUnitTo(unit, BWAPI::Broodwar->getMinerals());

            // If a valid mineral was found, right click it with the unit in order to start harvesting
            if (closestMineral) { unit->rightClick(closestMineral); }
        }
    }
}

void Micro::GatherMinerals(BWAPI::Unit unit) {  
    if (!unit) return; // Check for nullness to address C26429  

    // Get the closest mineral to this worker unit  
    BWAPI::Unit closestMineral = Tools::GetClosestUnitTo(unit, BWAPI::Broodwar->getMinerals());  

    // If a valid mineral was found, right click it with the unit in order to start harvesting  
    if (closestMineral) {  
        unit->rightClick(closestMineral);  
    }  
}

void Micro::GatherResources(BWAPI::Unit unit) {
    if (!unit) return; // Check for nullness to address C26429  

	BWAPI::Unit extractor = nullptr;
    int workersOnGas = 0;

    for (auto u : BWAPI::Broodwar->self()->getUnits()) {
        if (u->isCarryingGas() || u->isGatheringGas()) {
            workersOnGas++;
		}
        if (u->getType() == BWAPI::UnitTypes::Zerg_Extractor) {
            extractor = u;
        }
	}

    if (workersOnGas < 3 && extractor) {
        // If we already have enough workers on gas, gather minerals
        unit->rightClick(extractor);
        return;
	}

    // Get the closest mineral to this worker unit  
    BWAPI::Unit closestMineral = Tools::GetClosestUnitTo(unit, BWAPI::Broodwar->getMinerals());

    // If a valid mineral was found, right click it with the unit in order to start harvesting  
    if (closestMineral) {
        unit->rightClick(closestMineral);
    }
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

    // If not holding minerals, find the closest mineral patch
    BWAPI::Unit closestMineral = nullptr;
    int minDist = std::numeric_limits<int>::max();
    for (auto& mineral : BWAPI::Broodwar->getMinerals())
    {
        if (!mineral->exists() || mineral->getResources() <= 0) continue;
        int dist = drone->getDistance(mineral);
        if (dist < minDist)
        {
            minDist = dist;
            closestMineral = mineral;
        }
    }

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
            unit->attack(enemyBase);
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
                    unit->attack(enemyBase);
                }
            }
        }
    }
}

void Micro::BasicAttackAndScoutLoop(BWAPI::Unitset myUnits) {
    for (auto& unit : myUnits) {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord) {
            Micro::ScoutAndWander(unit);
            continue;
        }
        if (unit->getType().isWorker()) {
            if (unit->isIdle()) Micro::GatherResources(unit);
            continue;
        }
        if (!unit->getType().isBuilding()) {
            if (unit->isMorphing() || unit->isBurrowed() || unit->isLoaded()) continue;

            auto enemies = unit->getUnitsInRadius(640, BWAPI::Filter::IsEnemy && BWAPI::Filter::Exists);

            switch (static_cast<int>(Micro::GetMode())) {
                case static_cast<int>(MicroMode::Aggressive):
                    if (!enemies.empty()) {
                        Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
                    } else {
                        Micro::unitAttack(unit);
                    }
                    break;

                case static_cast<int>(MicroMode::Defensive):
                    if (!enemies.empty()) {
                        // If near our base, defend; otherwise, retreat
                        if (BasesTools::IsAreaEnemyBase(unit->getPosition(), 3)) {
                            Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
                        }
                        else  if (BasesTools::IsAreaEnemyBase(unit->getPosition(), 3)) {
                            Micro::SmartAvoidLethalAndAttackNonLethal(unit, false);
                        }else {
                            Micro::Retreat(unit);
                        }
                    } else {
                        // Stay near base or patrol
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
void Micro::HiveTechMicroLoop(BWAPI::Unitset myUnits) {
    BWAPI::Unitset mutalisks;
    BWAPI::Unitset guardians;
    BWAPI::Unitset devourers;
    BWAPI::Unitset queens;
    BWAPI::Unitset defaultUnits;

    for (auto& unit : myUnits) {
        if (!unit->isCompleted() || unit->isLoaded() || unit->isBurrowed() || unit->isMorphing()) continue;

        auto type = unit->getType();
        if (type == BWAPI::UnitTypes::Zerg_Mutalisk) {
            mutalisks.insert(unit);
        } else if (type == BWAPI::UnitTypes::Zerg_Guardian) {
            guardians.insert(unit);
        } else if (type == BWAPI::UnitTypes::Zerg_Devourer) {
            devourers.insert(unit);
        } else if (type == BWAPI::UnitTypes::Zerg_Queen) {
            queens.insert(unit);
        } else {
            defaultUnits.insert(unit);
        }
    }

    // Call default micro on normal units
    BasicAttackAndScoutLoop(defaultUnits);

    // Calculate a target base for harss/assault
    BWAPI::Position targetPos = BasesTools::GetEnemyBasePosition();
    if (targetPos == BWAPI::Positions::None) {
        // Just look for some enemy
        for (auto enemy : BWAPI::Broodwar->enemy()->getUnits()) {
            if (enemy->getType().isBuilding()) {
                targetPos = enemy->getPosition();
                break;
            }
        }
        if (targetPos == BWAPI::Positions::None) {
            targetPos = BWAPI::Position(BWAPI::Broodwar->mapWidth()*16, BWAPI::Broodwar->mapHeight()*16);
        }
    }

    // Harass loops
    for (auto muta : mutalisks) {
        MutaliskHarassLoop(muta, BWAPI::Broodwar->enemy()->getUnits());
    }

    for (auto guardian : guardians) {
        GuardianAssaultLoop(guardian, BWAPI::Broodwar->enemy()->getUnits());
    }

    // Devourers escort guardians or mutalisks
    for (auto devourer : devourers) {
        BWAPI::Unit targetToEscort = nullptr;
        if (!guardians.empty()) {
            targetToEscort = *guardians.begin();
        } else if (!mutalisks.empty()) {
            targetToEscort = *mutalisks.begin();
        }

        if (targetToEscort) {
            SmartMove(devourer, targetToEscort->getPosition());
            // Attack nearby air threats if any
            auto enemies = devourer->getUnitsInRadius(devourer->getType().airWeapon().maxRange() + 32, BWAPI::Filter::IsEnemy);
            BWAPI::Unit bestTarget = nullptr;
            for (auto e : enemies) {
                if (e->getType().isFlyer()) {
                    bestTarget = e;
                    break;
                }
            }
            if (bestTarget) {
                SmartAttackUnit(devourer, bestTarget);
            }
        } else {
            SmartMove(devourer, targetPos);
        }
    }

    // Queens use spells
    for (auto queen : queens) {
        QueenCastLoop(queen, BWAPI::Broodwar->enemy()->getUnits());
        // Follow army
        if (!guardians.empty()) {
            SmartMove(queen, (*guardians.begin())->getPosition());
        } else if (!mutalisks.empty()) {
            SmartMove(queen, (*mutalisks.begin())->getPosition());
        }
    }
}

void Micro::MutaliskHarassLoop(BWAPI::Unit muta, BWAPI::Unitset enemies) {
    if (!muta) return;

    // Mutalisks use airWeapon for everything
    int range = muta->getType().airWeapon().maxRange();
    
    // Find threats and targets
    auto nearbyEnemies = muta->getUnitsInRadius(range + 128, BWAPI::Filter::IsEnemy);
    
    BWAPI::Unit bestTarget = nullptr;
    BWAPI::Unit worstThreat = nullptr;
    int minThreatDist = 99999;

    for (auto enemy : nearbyEnemies) {
        // Threat analysis
        BWAPI::WeaponType w = enemy->getType().airWeapon();
        if (w != BWAPI::WeaponTypes::None) {
            int dist = muta->getDistance(enemy);
            if (dist < minThreatDist && dist <= w.maxRange() + 64) {
                minThreatDist = dist;
                worstThreat = enemy;
            }
        }

        // Target priority (Workers > AntiAir > Buildings)
        if (!bestTarget) {
            bestTarget = enemy;
        } else if (enemy->getType().isWorker() && !bestTarget->getType().isWorker()) {
            bestTarget = enemy;
        }
    }

    if (worstThreat && muta->getGroundWeaponCooldown() > 0) {
        // Kite away
        BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Kiting!");
        Flee(muta, worstThreat);
    } else if (bestTarget && muta->getGroundWeaponCooldown() == 0) {
        BWAPI::Broodwar->drawTextMap(muta->getPosition(), "Attacking!");
        SmartAttackUnit(muta, bestTarget);
    } else {
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

void Micro::GuardianAssaultLoop(BWAPI::Unit guardian, BWAPI::Unitset enemies) {
    if (!guardian) return;

    auto nearbyEnemies = guardian->getUnitsInRadius(guardian->getType().groundWeapon().maxRange() + 64, BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlyer);
    
    if (!nearbyEnemies.empty()) {
        BWAPI::Unit bestTarget = nullptr;
        // Prioritize static D and scary units
        for (auto enemy : nearbyEnemies) {
            if (!bestTarget) {
                bestTarget = enemy;
            } else if (enemy->getType().groundWeapon().maxRange() > 0 && bestTarget->getType().groundWeapon().maxRange() == 0) {
                bestTarget = enemy;
            }
        }
        if (guardian->getGroundWeaponCooldown() == 0) {
             BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Sieging");
             SmartAttackUnit(guardian, bestTarget);
        } else if (bestTarget) {
             // Guardians are slow, but try to kite slightly if possible
             int enemyRange = bestTarget->getType().airWeapon().maxRange();
             if (enemyRange > 0 && guardian->getDistance(bestTarget) <= enemyRange) {
                 BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Kiting");
                 Flee(guardian, bestTarget);
             }
        }
    } else {
        BWAPI::Position targetPos = BasesTools::GetEnemyBasePosition();
        if (targetPos != BWAPI::Positions::None) {
            BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Assaulting base");
            SmartMove(guardian, targetPos);
        } else {
             BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Scouting");
             ScoutAndWander(guardian);
        }
    }
}

void Micro::QueenCastLoop(BWAPI::Unit queen, BWAPI::Unitset enemies) {
    if (!queen) return;

    auto nearbyEnemies = queen->getUnitsInRadius(10 * 32, BWAPI::Filter::IsEnemy); // ~Sight range
    
    for (auto enemy : nearbyEnemies) {
        // Spawn Broodlings on Tanks/Ultras
        if (queen->getEnergy() >= 150 && BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Spawn_Broodlings)) {
            if (enemy->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode || 
                enemy->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
                enemy->getType() == BWAPI::UnitTypes::Protoss_High_Templar ||
                enemy->getType() == BWAPI::UnitTypes::Zerg_Ultralisk ||
                enemy->getType() == BWAPI::UnitTypes::Zerg_Defiler) 
            {
                queen->useTech(BWAPI::TechTypes::Spawn_Broodlings, enemy);
                return;
            }
        }

        // Ensnare clumps of bio/mutas
        if (queen->getEnergy() >= 75 && BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Ensnare)) {
            // Very simple: just ensnare the first enemy unit we see that is scary
            if (enemy->getType().canAttack() && !enemy->getType().isBuilding()) {
                queen->useTech(BWAPI::TechTypes::Ensnare, enemy->getPosition());
                return;
            }
        }

        // Parasite for scouting
        if (queen->getEnergy() >= 75) {
            if (!enemy->getType().isBuilding() && !enemy->isParasited() && enemy->getType().maxHitPoints() > 100) {
                queen->useTech(BWAPI::TechTypes::Parasite, enemy);
                return;
            }
        }
    }
}
