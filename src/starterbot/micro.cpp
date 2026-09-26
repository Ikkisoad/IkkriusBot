#include "micro.h"
#include "Units.h"
#include "BWAPI.h" // Ensure this header is included for TILE_SIZE definition
#include <random>
#include "../../visualstudio/BasesTools.h"
#include "Tools.h"
#include "CombatPolicy.h"
#include "MatchLog.h"
#include <algorithm>
#include <cmath>
#include <limits>
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
    // Depots first: scanning every unit we own for each enemy does not scale with a big army.
    std::vector<BWAPI::Unit> depots;
    for (auto unit : BWAPI::Broodwar->self()->getUnits())
        if (unit->getType().isResourceDepot()) depots.push_back(unit);
    if (depots.empty()) return threats;
    for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
        if (!enemy->exists() || !enemy->isVisible() ||
            !BWAPI::Broodwar->self()->isEnemy(enemy->getPlayer())) continue;
        if (!enemy->getType().canAttack() && !enemy->getType().isSpellcaster() &&
            enemy->getType() != BWAPI::UnitTypes::Terran_Bunker) continue;
        for (auto depot : depots) {
            if (depot->getDistance(enemy) <= 12 * 32) {
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

namespace {
    double CombatPower(BWAPI::Unit unit) {
        const auto type = unit->getType();
        if (!type.canAttack() || type.isWorker()) return 0;
        return std::max(2, type.supplyRequired()) *
            double(unit->getHitPoints() + unit->getShields()) / std::max(1, type.maxHitPoints() + type.maxShields());
    }

    // Drones currently pulled off the minerals to fight at a base.
    std::set<int> droneDefenders;
    constexpr int DroneDefenseRadius = 8 * 32;   // Enemies this close to a depot are fought by its drones.
    constexpr int DroneGatherRadius = 10 * 32;   // Drones and army this close to the depot join in.
    constexpr int DroneRetreatHitPoints = 16;    // A drone this hurt goes back to mining.

    // Local power as the drones see it: an enemy worker fights like a drone, a building going up is worth
    // killing with a few drones before it finishes, completed static defense is left to the army.
    double DroneFightPower(BWAPI::Unit enemy) {
        const auto type = enemy->getType();
        if (type.isWorker()) return CombatPolicy::DronePower;
        if (type.isBuilding()) return enemy->isCompleted() ? 0.0 : 2.0;
        if (!type.canAttack()) return 0.0;
        return std::max(2, type.supplyRequired()) *
            double(enemy->getHitPoints() + enemy->getShields()) / std::max(1, type.maxHitPoints() + type.maxShields());
    }
}

BWAPI::Unitset Micro::DefendWithDrones(const BWAPI::Unitset& threats) {
    const auto self = BWAPI::Broodwar->self();
    const int frame = BWAPI::Broodwar->getFrameCount();
    BWAPI::Unitset fighting;
    std::set<int> pulled;
    for (auto depot : self->getUnits()) {
        if (!depot->getType().isResourceDepot() || !depot->isCompleted() || threats.empty()) continue;
        // Enemies drones can fight here: on the ground, visible, not completed static defense, and workers only
        // when they attack or build (a passing scout is not worth a mineral line).
        BWAPI::Unitset enemies;
        double enemyPower = 0;
        for (auto threat : threats) {
            if (!threat->exists() || threat->isFlying() || !threat->isDetected() || depot->getDistance(threat) > DroneDefenseRadius) continue;
            const auto type = threat->getType();
            if (type.isWorker() && !threat->isAttacking() && !threat->isConstructing()) continue;
            const double power = DroneFightPower(threat);
            if (power <= 0) continue;
            enemies.insert(threat);
            enemyPower += power;
        }
        // Buildings going up next to the base (cannon or bunker rush) are not "threats" until they finish.
        for (auto enemy : depot->getUnitsInRadius(DroneDefenseRadius, BWAPI::Filter::IsEnemy)) {
            if (!enemy->isVisible() || !enemy->getType().isBuilding() || enemy->isCompleted() || enemies.contains(enemy)) continue;
            enemies.insert(enemy);
            enemyPower += DroneFightPower(enemy);
        }
        if (enemies.empty()) continue;
        double armyPower = 0;
        std::vector<BWAPI::Unit> drones;
        for (auto unit : depot->getUnitsInRadius(DroneGatherRadius, BWAPI::Filter::IsOwned)) {
            if (!unit->exists() || !unit->isCompleted()) continue;
            const auto type = unit->getType();
            if (type == BWAPI::UnitTypes::Zerg_Drone) {
                if (!unit->isConstructing() && !Tools::HasPendingConstruction(unit) && !unit->isMorphing() &&
                    unit->getHitPoints() > DroneRetreatHitPoints && !pulled.count(unit->getID())) drones.push_back(unit);
            } else if (type == BWAPI::UnitTypes::Zerg_Sunken_Colony) armyPower += CombatPolicy::StaticDefensePower;
            else if (!type.isBuilding() && type.canAttack() && !unit->isFlying()) armyPower += CombatPower(unit);
        }
        const int wanted = CombatPolicy::DronesToDefend(enemyPower, armyPower, static_cast<int>(drones.size()));
        if (wanted == 0) continue;
        // The drones closest to the fight go; the rest keep mining.
        int x = 0, y = 0;
        for (auto enemy : enemies) { x += enemy->getPosition().x; y += enemy->getPosition().y; }
        const BWAPI::Position center(x / static_cast<int>(enemies.size()), y / static_cast<int>(enemies.size()));
        std::sort(drones.begin(), drones.end(), [center](BWAPI::Unit a, BWAPI::Unit b) {
            return a->getDistance(center) != b->getDistance(center) ? a->getDistance(center) < b->getDistance(center) : a->getID() < b->getID();
        });
        drones.resize(wanted);
        // Logged when a base starts pulling drones, not on every frame of the fight.
        bool alreadyFighting = false;
        for (auto drone : drones) alreadyFighting = alreadyFighting || droneDefenders.count(drone->getID()) > 0;
        if (!alreadyFighting)
            MatchLog::Event("drone_defense", "drones=" + std::to_string(wanted) + " enemy=" + std::to_string(int(enemyPower)) +
                " army=" + std::to_string(int(armyPower)));
        for (auto drone : drones) {
            // Units that fight back first, then workers, then buildings going up; nearest within each.
            BWAPI::Unit target = nullptr;
            const auto rank = [](BWAPI::Unit enemy) { return enemy->getType().isBuilding() ? 2 : enemy->getType().isWorker() ? 1 : 0; };
            for (auto enemy : enemies)
                if (!target || rank(enemy) < rank(target) || (rank(enemy) == rank(target) && drone->getDistance(enemy) < drone->getDistance(target)))
                    target = enemy;
            if (!target) continue;
            pulled.insert(drone->getID());
            fighting.insert(drone);
            SmartAttackUnit(drone, target);
        }
    }
    // Drones no longer needed go straight back to work; each is tracked until its attack order is replaced.
    auto still = pulled;
    for (int id : droneDefenders) {
        if (pulled.count(id)) continue;
        const auto drone = BWAPI::Broodwar->getUnit(id);
        if (!drone || !drone->exists() || drone->getPlayer() != self ||
            drone->getLastCommand().getType() != BWAPI::UnitCommandTypes::Attack_Unit) continue;
        // No free patch: stop, and the idle-worker handling finds it work.
        if (!Tools::GatherNearestBaseMinerals(drone) && drone->getLastCommandFrame() < frame) drone->stop();
        still.insert(id);
    }
    droneDefenders = still;
    return fighting;
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

namespace {
    // How many of our units are ordered onto each target, counted once per frame for every focus-fire choice.
    std::map<int, int> attackersOnTarget;
    int attackersFrame = -1;
    int AttackersOn(BWAPI::Unit enemy) {
        const int frame = BWAPI::Broodwar->getFrameCount();
        if (attackersFrame != frame) {
            attackersFrame = frame;
            attackersOnTarget.clear();
            for (auto ally : BWAPI::Broodwar->self()->getUnits())
                if (const auto target = ally->getOrderTarget()) ++attackersOnTarget[target->getID()];
        }
        const auto found = attackersOnTarget.find(enemy->getID());
        return found == attackersOnTarget.end() ? 0 : found->second;
    }
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
        const int allies = AttackersOn(enemy) - (unit->getOrderTarget() == enemy ? 1 : 0);
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
namespace {
    // Enemy static defense the army stays out of, with each colony's reach, rebuilt every frame: all of it
    // while Guardians siege (they outrange it), otherwise every colony no attack is strong enough to kill.
    struct Cover { BWAPI::Position position; int reach = 0; };
    std::vector<Cover> groundCover, airCover;
    // Static defense is remembered through the fog: a cannon does not go away because we stopped seeing it.
    struct KnownDefense { BWAPI::Unit unit = nullptr; BWAPI::Position position; int ground = -1, air = -1; };
    std::map<int, KnownDefense> knownDefenses;
    // Committed assaults by colony ID: held until the first frame, re-evaluated from the second.
    std::map<int, int> assaultUntil, assaultCheck;
    // Colonies are remembered by their center; this turns that into an approximate distance to their edge.
    constexpr int DefenseHalfSize = 32;
    int CoverDistance(BWAPI::Unit unit, const Cover& cover) { return std::max(0, unit->getDistance(cover.position) - DefenseHalfSize); }
    int CoverDistance(BWAPI::Position point, const Cover& cover) { return std::max(0, point.getApproxDistance(cover.position) - DefenseHalfSize); }
    bool GroundCovered(BWAPI::Position point, int margin) {
        for (const auto& cover : groundCover)
            if (CoverDistance(point, cover) <= cover.reach + margin) return true;
        return false;
    }
    // Where the ground army advances while Guardians siege; None means the enemy main.
    BWAPI::Position siegeEscort = BWAPI::Positions::None;
    // The known enemy base closest to the ground army's main body; None means the enemy main.
    BWAPI::Position groundObjective = BWAPI::Positions::None;
    // The Mutalisk picked to morph into a Guardian or Devourer, flown home first, and when it was picked.
    int airMorpher = -1, airMorpherSince = 0;
}

// True when `enemy` sits inside enemy static defense that `unit` is staying out of.
bool Micro::AvoidsStaticDefense(BWAPI::Unit unit, BWAPI::Unit enemy) {
    for (const auto& cover : unit->isFlying() ? airCover : groundCover)
        if (CoverDistance(enemy, cover) <= cover.reach + 32) return true;
    return false;
}

void Micro::SmartAttackMove(BWAPI::Unit unit, BWAPI::Position position) {
    if (!unit || !position.isValid() || unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    const auto command = unit->getLastCommand();
    if (!unit->isIdle() && command.getType() == BWAPI::UnitCommandTypes::Attack_Move &&
        command.getTargetPosition().getApproxDistance(position) < 64) return;
    unit->attack(position);
}

namespace { BWAPI::Position ControlPoint(BWAPI::Unit unit, BWAPI::Position rally); }

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
            if (AvoidsStaticDefense(unit, nearby)) continue;
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
    // No orders to attack and nothing threatening a base: spread out for map control instead of
    // clumping the whole army at the rally point. Attack-move still lets it fight anything it finds.
    if (GetMode() != MicroMode::Aggressive) {
        const auto point = threats.empty() ? ControlPoint(unit, rally) : rally;
        if (!UseNydus(unit, point)) SmartAttackMove(unit, point);
        return;
    }
    const auto destination = siegeEscort.isValid() ? siegeEscort :
        groundObjective.isValid() ? groundObjective : BasesTools::GetEnemyBasePosition();
    if (!destination.isValid()) { ScoutAndWander(unit); return; }
    // Only units running ahead of the main body wait for it; the rest keep marching on the target.
    if (center.isValid() && CombatPolicy::WaitForMainBody(unit->getDistance(destination),
        center.getApproxDistance(destination), unit->getDistance(center))) {
        SmartAttackMove(unit, center);
        return;
    }
    if (UseNydus(unit, destination)) return;
    SmartAttackMove(unit, destination);
}

void Micro::ZerglingSquadLoop(const BWAPI::Unitset& zerglings, const BWAPI::Unitset& threats, BWAPI::Position rally, BWAPI::Position body) {
    const int frame = BWAPI::Broodwar->getFrameCount();
    const auto self = BWAPI::Broodwar->self();
    const bool aggressive = GetMode() == MicroMode::Aggressive;
    std::vector<BWAPI::Unit> lings(zerglings.begin(), zerglings.end());
    std::sort(lings.begin(), lings.end(), [](BWAPI::Unit a, BWAPI::Unit b) { return a->getID() < b->getID(); });
    std::vector<BWAPI::Position> positions;
    for (auto ling : lings) positions.push_back(ling->getPosition());
    for (const auto& squad : CombatPolicy::Squads(positions, CombatPolicy::ZerglingSquadRadius, CombatPolicy::ZerglingSquadSize)) {
        const auto anchor = lings[squad.front()];
        int x = 0, y = 0;
        for (int i : squad) { x += positions[i].x; y += positions[i].y; }
        const BWAPI::Position center(x / static_cast<int>(squad.size()), y / static_cast<int>(squad.size()));
        int span = 0;
        for (int i : squad) span = std::max(span, positions[i].getApproxDistance(center));

        // Where the squad goes when it has nothing to fight: the same plan as the rest of the ground army.
        const auto destination = [&]() {
            if (!aggressive) return threats.empty() ? ControlPoint(anchor, rally) : rally;
            const auto target = siegeEscort.isValid() ? siegeEscort :
                groundObjective.isValid() ? groundObjective : BasesTools::GetEnemyBasePosition();
            // Squads running ahead of the main body wait for it; the rest keep marching.
            if (target.isValid() && body.isValid() && CombatPolicy::WaitForMainBody(center.getApproxDistance(target),
                body.getApproxDistance(target), center.getApproxDistance(body))) return body;
            return target;
        };
        const auto march = [&](BWAPI::Unit ling, BWAPI::Position point) {
            if (!point.isValid()) ScoutAndWander(ling);
            else if (!UseNydus(ling, point)) SmartAttackMove(ling, point);
        };

        // Base defense: each ling takes the nearest ground threat.
        std::vector<int> available;
        for (int i : squad) {
            BWAPI::Unit target = nullptr;
            for (auto threat : threats) {
                if (threat->isFlying() || !threat->isDetected()) continue;
                if (!target || lings[i]->getDistance(threat) < lings[i]->getDistance(target)) target = threat;
            }
            if (target) SmartAttackUnit(lings[i], target);
            else available.push_back(i);
        }
        if (available.empty()) continue;

        // One scan for the whole squad: the power balance and the targets around it. Anything under static
        // defense we are staying out of is neither a target nor a reason to run.
        double friendlyPower = 0, enemyPower = 0;
        BWAPI::Unitset candidates;
        for (auto nearby : BWAPI::Broodwar->getUnitsInRadius(center, span + 320)) {
            if (!nearby->exists() || !nearby->isCompleted()) continue;
            const auto type = nearby->getType();
            if (nearby->getPlayer() == self) { friendlyPower += CombatPower(nearby); continue; }
            if (!self->isEnemy(nearby->getPlayer()) || !nearby->isVisible() || AvoidsStaticDefense(anchor, nearby)) continue;
            if (type.groundWeapon() != BWAPI::WeaponTypes::None || type == BWAPI::UnitTypes::Terran_Bunker)
                enemyPower += type.isBuilding() ? CombatPower(nearby) + 6 : CombatPower(nearby);
            if (nearby->isDetected() && !nearby->isFlying()) candidates.insert(nearby);
        }

        const auto engagement = CombatPolicy::AssessEngagement(friendlyPower, enemyPower, CombatPolicy::TakeCloseFight(frame));
        // Losing the local fight (including to enemies lings cannot hit, like air): fall back to the rally.
        if (engagement == CombatPolicy::Engagement::Withdraw) {
            for (int i : available) SmartMove(lings[i], rally);
            continue;
        }
        if (candidates.empty()) {
            // Out of contact: standing orders are kept until the squad's turn to re-plan, except for idle lings.
            const bool due = CombatPolicy::SquadOrdersDue(frame, anchor->getID());
            const auto point = destination();
            for (int i : available)
                if (due || lings[i]->isIdle()) march(lings[i], point);
            continue;
        }
        for (int i : available) {
            const auto ling = lings[i];
            const auto target = ChooseFocusTarget(ling, candidates, false);
            if (!target) { march(ling, destination()); continue; }
            if (CombatPolicy::ShouldStepBack(engagement, false, ling->getGroundWeaponCooldown(), ling->getHitPoints(),
                ling->getType().maxHitPoints())) { FallBack(ling, target, center); continue; }
            SmartAttackUnit(ling, target);
        }
    }
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
    BWAPI::Position AwayFromPoints(BWAPI::Unit unit, const std::vector<BWAPI::Position>& threats, BWAPI::Position fallback) {
        double dx = 0, dy = 0;
        for (auto threat : threats) {
            const double x = unit->getPosition().x - threat.x, y = unit->getPosition().y - threat.y;
            const double length = std::max(1.0, std::sqrt(x * x + y * y));
            dx += x / length; dy += y / length;
        }
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.01) return fallback.isValid() ? fallback : unit->getPosition();
        BWAPI::Position away(unit->getPosition().x + int(96 * dx / length), unit->getPosition().y + int(96 * dy / length));
        away.makeValid();
        return away;
    }
    BWAPI::Position AwayFrom(BWAPI::Unit unit, const std::vector<BWAPI::Unit>& threats, BWAPI::Position fallback) {
        std::vector<BWAPI::Position> points;
        for (auto threat : threats) points.push_back(threat->getPosition());
        return AwayFromPoints(unit, points, fallback);
    }

    // Reach of an enemy colony, cannon, bunker or turret against ground or air units; -1 when it cannot hit them.
    int StaticReach(BWAPI::Unit defense, bool air) {
        const auto type = defense->getType();
        if (!type.isBuilding() || !defense->isCompleted()) return -1;
        if (air) return AirThreatRange(defense);
        if (type == BWAPI::UnitTypes::Terran_Bunker)
            return defense->getPlayer()->weaponMaxRange(BWAPI::WeaponTypes::Gauss_Rifle) + 32;
        if (type.groundWeapon() == BWAPI::WeaponTypes::None) return -1;
        return defense->getPlayer()->weaponMaxRange(type.groundWeapon());
    }

    // Guardians hover where enemy ground units cannot follow: over unwalkable terrain, or above
    // the target's cliff level, so anti-air has to path around or up before it can shoot back.
    int TerrainAdvantage(BWAPI::Position spot, BWAPI::Position target) {
        if (!spot.isValid()) return -1;
        int blocked = 0;
        for (auto offset : {BWAPI::Position(0, 0), BWAPI::Position(24, 0), BWAPI::Position(-24, 0), BWAPI::Position(0, 24), BWAPI::Position(0, -24)}) {
            BWAPI::Position probe(spot.x + offset.x, spot.y + offset.y);
            if (probe.isValid() && !BWAPI::Broodwar->isWalkable(BWAPI::WalkPosition(probe))) ++blocked;
        }
        int advantage = blocked >= 4 ? 2 : 0;
        if (target.isValid() && BWAPI::Broodwar->getGroundHeight(BWAPI::TilePosition(spot)) / 2 >
            BWAPI::Broodwar->getGroundHeight(BWAPI::TilePosition(target)) / 2) ++advantage;
        return advantage;
    }

    // A firing spot at max range from `target` with better terrain than where the Guardian is now and
    // outside every known anti-air reach; None when the current spot is already as good as it gets.
    BWAPI::Position GuardianPerch(BWAPI::Unit guardian, BWAPI::Unit target, int range, const std::vector<BWAPI::Unit>& antiAir) {
        const auto origin = target->getPosition();
        const int here = TerrainAdvantage(guardian->getPosition(), origin);
        BWAPI::Position best = BWAPI::Positions::None;
        int bestScore = std::numeric_limits<int>::min();
        for (int i = 0; i < 16; ++i) {
            const double angle = i * 3.14159265358979 / 8;
            BWAPI::Position spot(origin.x + int(std::cos(angle) * (range - 16)), origin.y + int(std::sin(angle) * (range - 16)));
            if (!spot.isValid()) continue;
            const int travel = guardian->getPosition().getApproxDistance(spot);
            if (travel > 320) continue; // Guardians are slow; a far perch costs more shots than it saves.
            bool exposed = false;
            for (auto threat : antiAir)
                exposed = exposed || threat->getPosition().getApproxDistance(spot) <= AirThreatRange(threat) + ThreatMargin(threat);
            if (exposed) continue;
            const int advantage = TerrainAdvantage(spot, origin);
            if (advantage <= here) continue;
            const int score = advantage * 256 - travel;
            if (score > bestScore) { best = spot; bestScore = score; }
        }
        return best;
    }

    // Our own units step out of an area a Queen is about to Ensnare.
    struct SpellArea { BWAPI::Position center; int radius = 0, until = 0; };
    std::vector<SpellArea> friendlySpells;

    // Broodling hunts: queen ID -> prey ID, and prey abandoned after the Queen met anti-air, until frame.
    std::map<int, int> queenPrey, preyAvoid;

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
            // Gone, or taken over (an infested Command Center is ours now, not a base to attack).
            if ((BWAPI::Broodwar->isVisible(BWAPI::TilePosition(it->second)) && (!depot || !depot->exists())) ||
                (depot && depot->exists() && !BWAPI::Broodwar->self()->isEnemy(depot->getPlayer()))) it = enemyDepots.erase(it);
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

    // Guardians siege the enemy's expansions first: every base killed early is income the opponent never
    // gets. With none to kill, an assault goes for the main and a containment holds the free base closest
    // to it, so the next Hatchery/Nexus/Command Center dies as it starts.
    BWAPI::Position ChooseGuardianSiege(BWAPI::Position from, bool assault) {
        const auto enemyMain = BasesTools::GetEnemyBasePosition();
        BWAPI::Position best = BWAPI::Positions::None;
        for (const auto& depot : enemyDepots) {
            if (enemyMain.isValid() && depot.second.getApproxDistance(enemyMain) <= 320) continue;
            if (!best.isValid() || from.getApproxDistance(depot.second) < from.getApproxDistance(best)) best = depot.second;
        }
        if (best.isValid() || assault || !enemyMain.isValid()) return best.isValid() ? best : enemyMain;
        for (auto base : BasesTools::GetAllBasePositions()) {
            if (base.getApproxDistance(enemyMain) <= 320) continue;
            bool ours = false;
            for (auto unit : BWAPI::Broodwar->self()->getUnits())
                ours = ours || (unit->getType().isResourceDepot() && unit->getDistance(base) <= 320);
            if (ours) continue;
            if (!best.isValid() || base.getApproxDistance(enemyMain) < best.getApproxDistance(enemyMain)) best = base;
        }
        return best.isValid() ? best : enemyMain;
    }

    // An attack goes in on a colony only with the power to kill it: our combat units around it against the
    // colony, every colony covering it and the enemy army standing by. Committed assaults are held for a
    // few seconds and dropped only once the trade is clearly lost, so the army does not dance at the edge.
    bool Assaulting(int id, const KnownDefense& defense, bool attacking, bool guardians, int frame) {
        if (!attacking || guardians) { assaultUntil.erase(id); assaultCheck.erase(id); return false; }
        auto& until = assaultUntil[id];
        auto& check = assaultCheck[id];
        if (frame < check) return until > frame;
        check = frame + 12;
        const int reach = std::max(defense.ground, defense.air);
        double friendly = 0, enemy = 0;
        for (auto unit : BWAPI::Broodwar->getUnitsInRadius(defense.position, reach + 384)) {
            if (!unit->exists() || !unit->isCompleted()) continue;
            if (unit->getPlayer() == BWAPI::Broodwar->self()) friendly += CombatPower(unit);
            else if (BWAPI::Broodwar->self()->isEnemy(unit->getPlayer()) && unit->isVisible() && !unit->getType().isBuilding() &&
                     unit->getPosition().getApproxDistance(defense.position) <= reach + 192) enemy += CombatPower(unit);
        }
        for (const auto& known : knownDefenses)
            if (known.second.position.getApproxDistance(defense.position) <=
                std::max(known.second.ground, known.second.air) + DefenseHalfSize * 2) enemy += CombatPolicy::StaticDefensePower;
        if (CombatPolicy::AssaultStaticDefense(attacking, guardians, friendly, enemy)) {
            if (until <= frame) MatchLog::Event("static_assault", "power=" + std::to_string(int(friendly)) + " defense=" + std::to_string(int(enemy)));
            until = frame + CombatPolicy::StaticAssaultHoldFrames;
        } else if (!CombatPolicy::KeepAssault(friendly, enemy)) until = 0;
        return until > frame;
    }

    // Rebuild the static defense the army stays out of. With Guardians out that is all of it; otherwise every
    // colony not under a committed assault. Colonies raiding our own bases are exempt: they are fought.
    void UpdateStaticCover(bool guardians, const BWAPI::Unitset& baseThreats) {
        const int frame = BWAPI::Broodwar->getFrameCount();
        const auto self = BWAPI::Broodwar->self();
        for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
            if (!enemy->exists() || !enemy->isVisible() || !self->isEnemy(enemy->getPlayer())) continue;
            const int ground = StaticReach(enemy, false), air = StaticReach(enemy, true);
            if (ground >= 0 || air >= 0) knownDefenses[enemy->getID()] = {enemy, enemy->getPosition(), ground, air};
        }
        // Forget a colony once its spot is in sight and it is gone, or no longer the enemy's.
        for (auto it = knownDefenses.begin(); it != knownDefenses.end();) {
            const auto unit = it->second.unit;
            if (BWAPI::Broodwar->isVisible(BWAPI::TilePosition(it->second.position)) &&
                (!unit->exists() || !self->isEnemy(unit->getPlayer()))) {
                assaultUntil.erase(it->first); assaultCheck.erase(it->first);
                it = knownDefenses.erase(it);
            } else ++it;
        }
        groundCover.clear(); airCover.clear();
        const bool attacking = Micro::GetMode() == Micro::MicroMode::Aggressive;
        for (const auto& [id, defense] : knownDefenses) {
            if (baseThreats.contains(defense.unit)) continue;
            if (Assaulting(id, defense, attacking, guardians, frame)) continue;
            if (defense.ground >= 0) groundCover.push_back({defense.position, defense.ground});
            if (defense.air >= 0) airCover.push_back({defense.position, defense.air});
        }
    }

    // Step out of static defense we are not going to kill, rather than trading units into it.
    bool KeepOutOfStaticDefense(BWAPI::Unit unit, BWAPI::Position fallback) {
        if (unit->isBurrowed()) return false;
        std::vector<BWAPI::Position> inReach;
        for (const auto& cover : unit->isFlying() ? airCover : groundCover)
            if (CoverDistance(unit, cover) <= cover.reach + 32) inReach.push_back(cover.position);
        if (inReach.empty()) return false;
        BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Avoid static defense");
        const auto away = AwayFromPoints(unit, inReach, fallback);
        Micro::SmartMove(unit, unit->isFlying() || BWAPI::Broodwar->isWalkable(BWAPI::WalkPosition(away)) ? away : fallback);
        return true;
    }

    // A destination inside static defense we are not assaulting becomes the first point outside its reach on
    // the way from `from`: the army gathers there, instead of walking into the fire one unit at a time.
    BWAPI::Position SafeObjective(BWAPI::Position from, BWAPI::Position destination) {
        if (!destination.isValid() || !from.isValid() || !GroundCovered(destination, 64)) return destination;
        const double dx = from.x - destination.x, dy = from.y - destination.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        for (double travelled = 64; travelled < length; travelled += 64) {
            const BWAPI::Position point(destination.x + int(dx * travelled / length), destination.y + int(dy * travelled / length));
            if (!GroundCovered(point, 64)) return point;
        }
        return from;
    }

    // An idle unit still takes a free kill instead of just standing at its rally point: attack
    // whatever is already in reach as long as the immediate local fight is not a losing one.
    bool AttackIfSafelyInRange(BWAPI::Unit unit, int radius) {
        BWAPI::Unit target = nullptr;
        for (auto enemy : unit->getUnitsInRadius(radius, BWAPI::Filter::IsEnemy)) {
            if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !unit->canAttack(enemy)) continue;
            if (!target || unit->getDistance(enemy) < unit->getDistance(target)) target = enemy;
        }
        if (!target) return false;
        const auto fight = Micro::AssessLocalFight(unit, 320);
        if (CombatPolicy::AssessEngagement(fight.friendlyPower, fight.enemyPower) == CombatPolicy::Engagement::Withdraw) return false;
        Micro::SmartAttackUnit(unit, target);
        return true;
    }

    // Idle offensive units spread across the map for control/vision instead of clumping at the
    // rally point when nothing is attacking us. Assignment is stable per unit ID so a unit does
    // not reshuffle to a different spot every frame. Known enemy bases and anything under enemy
    // static defense are not "control": units parked there just die. Spots are built once per frame.
    std::vector<BWAPI::Position> controlSpots;
    int controlFrame = -1;
    BWAPI::Position ControlPoint(BWAPI::Unit unit, BWAPI::Position rally) {
        const int frame = BWAPI::Broodwar->getFrameCount();
        if (controlFrame != frame) {
            controlFrame = frame;
            controlSpots.clear();
            std::vector<BWAPI::Unit> depots;
            for (auto depot : BWAPI::Broodwar->self()->getUnits())
                if (depot->getType().isResourceDepot()) depots.push_back(depot);
            for (const auto& base : BasesTools::GetAllBasePositions()) {
                bool taken = GroundCovered(base, 64);
                for (auto depot : depots) taken = taken || depot->getDistance(base) <= 320;
                for (const auto& depot : enemyDepots) taken = taken || depot.second.getApproxDistance(base) <= 320;
                if (!taken) controlSpots.push_back(base);
            }
        }
        if (controlSpots.empty()) return rally;
        return controlSpots[unit->getID() % controlSpots.size()];
    }

    // Overlords spread one per base rather than stacking over the main, so a single AoE hit or
    // detector sweep cannot wipe them all and every base keeps its own air detection.
    BWAPI::Position OverlordSpread(BWAPI::Unit unit, BWAPI::Position rally) {
        const auto& bases = BasesTools::GetAllOurBasePositions();
        if (bases.empty()) return rally;
        return bases[unit->getID() % bases.size()];
    }

    // Keep watching the enemy base instead of coming straight home once it is found: head for the
    // nearest still-unexplored tile around it. Flee() above already pulls the scout out of danger.
    void ScoutEnemyBuild(BWAPI::Unit scout, BWAPI::Position enemyBase) {
        const auto orderPos = scout->getOrderTargetPosition();
        if (!scout->isIdle() && orderPos != BWAPI::Positions::None && !BWAPI::Broodwar->isExplored(BWAPI::TilePosition(orderPos))) return;
        constexpr int radius = 9; // tiles
        const BWAPI::TilePosition center(enemyBase);
        BWAPI::TilePosition best;
        bool found = false;
        int bestDist = std::numeric_limits<int>::max();
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                const BWAPI::TilePosition tile(center.x + dx, center.y + dy);
                if (!tile.isValid() || BWAPI::Broodwar->isExplored(tile)) continue;
                const int dist = scout->getDistance(BWAPI::Position(tile));
                if (dist < bestDist) { bestDist = dist; best = tile; found = true; }
            }
        }
        Micro::SmartMove(scout, BWAPI::Position(found ? best : center));
    }
}

void Micro::MarkSpellArea(BWAPI::Position center, int radius) {
    // Hold the area until the missile has landed.
    friendlySpells.push_back({center, radius, BWAPI::Broodwar->getFrameCount() + BWAPI::Broodwar->getLatencyFrames() + 36});
}

bool Micro::DodgeFriendlySpell(BWAPI::Unit unit) {
    if (friendlySpells.empty() || unit->isBurrowed()) return false;
    const int frame = BWAPI::Broodwar->getFrameCount();
    friendlySpells.erase(std::remove_if(friendlySpells.begin(), friendlySpells.end(),
        [frame](const SpellArea& area) { return area.until <= frame; }), friendlySpells.end());
    const auto position = unit->getPosition();
    for (const auto& area : friendlySpells) {
        if (position.getApproxDistance(area.center) > area.radius + 16) continue;
        double dx = position.x - area.center.x, dy = position.y - area.center.y;
        double length = std::sqrt(dx * dx + dy * dy);
        if (length < 1) { dx = unit->getID() % 2 ? 1 : -1; dy = 0; length = 1; }
        BWAPI::Position out(area.center.x + int(dx / length * (area.radius + 48)), area.center.y + int(dy / length * (area.radius + 48)));
        out.makeValid();
        BWAPI::Broodwar->drawTextMap(position, "Dodge Ensnare");
        SmartMove(unit, out);
        return true;
    }
    return false;
}

void Micro::ResetCombatState() {
    lurkerLastContact.clear();
    droneDefenders.clear();
    harassSquad.clear(); harassRegen.clear(); enemyDepots.clear(); harassAvoid.clear();
    harassRetreatUntil = 0;
    harassTarget = BWAPI::Positions::None;
    friendlySpells.clear(); groundCover.clear(); airCover.clear();
    knownDefenses.clear(); assaultUntil.clear(); assaultCheck.clear();
    controlSpots.clear(); controlFrame = -1;
    siegeEscort = groundObjective = BWAPI::Positions::None;
    airMorpher = -1; airMorpherSince = 0;
    queenPrey.clear(); preyAvoid.clear();
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

void Micro::ScourgeStrikeLoop(BWAPI::Unit scourge, BWAPI::Position escort) {
    // One Scourge trades its own life for the target, so pick the highest-supply flyer in reach
    // rather than whatever is nearest; ties go to the closer target to spend less time exposed.
    BWAPI::Unit target = nullptr;
    int bestValue = -1;
    for (auto enemy : scourge->getUnitsInRadius(320, BWAPI::Filter::IsEnemy)) {
        if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !enemy->isFlying() || !scourge->canAttack(enemy)) continue;
        const int value = std::max(1, enemy->getType().supplyRequired());
        if (value > bestValue || (value == bestValue && target && scourge->getDistance(enemy) < scourge->getDistance(target))) {
            target = enemy;
            bestValue = value;
        }
    }
    if (target) { SmartAttackUnit(scourge, target); return; }
    SmartMove(scourge, escort);
}

namespace {
    // How far `spot` is outside the reach of everything known to shoot air: visible enemies, and static
    // anti-air remembered through the fog. Large when nothing is near.
    int AntiAirMargin(BWAPI::Position spot) {
        int margin = 100000;
        for (auto enemy : BWAPI::Broodwar->getUnitsInRadius(spot, 1024, BWAPI::Filter::IsEnemy)) {
            if (!enemy->exists() || !enemy->isVisible()) continue;
            int reach = AirThreatRange(enemy);
            // Spellcasters (Storm, Irradiate, Plague) hit cocoons as hard as any weapon.
            if (reach < 0 && enemy->getType().isSpellcaster() && !enemy->getType().isBuilding()) reach = 9 * 32;
            if (reach < 0) continue;
            margin = std::min(margin, enemy->getPosition().getApproxDistance(spot) - reach);
        }
        for (const auto& cover : airCover) margin = std::min(margin, CoverDistance(spot, cover) - cover.reach);
        return margin;
    }

    // Our completed bases with nothing that shoots air anywhere near: where cocoons are safe.
    std::vector<BWAPI::Position> SafeMorphBases() {
        std::vector<BWAPI::Position> bases;
        for (auto depot : BWAPI::Broodwar->self()->getUnits())
            if (depot->getType().isResourceDepot() && depot->isCompleted() &&
                CombatPolicy::SafeMorphSpot(0, AntiAirMargin(depot->getPosition()))) bases.push_back(depot->getPosition());
        return bases;
    }

    // Where `muta` should morph: over the closest safe base of ours, or None.
    BWAPI::Position MorphSpot(BWAPI::Unit muta, const std::vector<BWAPI::Position>& bases) {
        BWAPI::Position best = BWAPI::Positions::None;
        for (auto base : bases)
            if (!best.isValid() || muta->getDistance(base) < muta->getDistance(best)) best = base;
        return best;
    }

    // Distance from `unit` to our nearest completed base.
    int DistanceToOwnBase(BWAPI::Unit unit) {
        int distance = 100000;
        for (auto depot : BWAPI::Broodwar->self()->getUnits())
            if (depot->getType().isResourceDepot() && depot->isCompleted()) distance = std::min(distance, unit->getDistance(depot));
        return distance;
    }

    bool MorphEligible(BWAPI::Unit unit) {
        return unit && unit->exists() && unit->getPlayer() == BWAPI::Broodwar->self() &&
            unit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk && unit->isCompleted() && !unit->isMorphing() &&
            unit->getHitPoints() >= unit->getType().maxHitPoints() / 2;
    }
}

BWAPI::Unit Micro::AirMorphCandidate() {
    const int frame = BWAPI::Broodwar->getFrameCount();
    auto morpher = airMorpher >= 0 ? BWAPI::Broodwar->getUnit(airMorpher) : nullptr;
    if (!MorphEligible(morpher) || frame - airMorpherSince > CombatPolicy::MorphTravelFrames) morpher = nullptr;
    if (!morpher) {
        // The healthy Mutalisk closest to a safe base, so the trip home is short.
        const auto bases = SafeMorphBases();
        int best = std::numeric_limits<int>::max();
        for (auto unit : BWAPI::Broodwar->self()->getUnits()) {
            if (!MorphEligible(unit) || unit->getID() == airMorpher || unit->isUnderAttack()) continue;
            const auto spot = MorphSpot(unit, bases);
            if (!spot.isValid() || unit->getDistance(spot) >= best) continue;
            best = unit->getDistance(spot);
            morpher = unit;
        }
        airMorpher = morpher ? morpher->getID() : -1;
        airMorpherSince = frame;
        if (!morpher) return nullptr;
    }
    if (morpher->isUnderAttack() ||
        !CombatPolicy::SafeMorphSpot(DistanceToOwnBase(morpher), AntiAirMargin(morpher->getPosition()))) return nullptr;
    airMorpherSince = frame; // Arrived: it keeps the claim while the morph waits on resources.
    return morpher;
}

bool Micro::MorphingHome(BWAPI::Unit unit) {
    if (!unit || unit->getID() != airMorpher || !MorphEligible(unit)) return false;
    const auto spot = MorphSpot(unit, SafeMorphBases());
    if (!spot.isValid()) { airMorpher = -1; return false; }
    BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Morph: going home");
    if (unit->getDistance(spot) > 64) SmartMove(unit, spot);
    return true;
}

void Micro::HiveTechMicroLoop(BWAPI::Unitset myUnits, const BWAPI::Unitset& pressureWave, bool needsDetection) {
    const auto threats = GetBaseThreats();
    // Drones help hold a base the army cannot; they are left alone by the rest of this loop.
    const auto droneFighters = DefendWithDrones(threats);
    auto rally = BWAPI::Position(BasesTools::GetMainBasePosition());
    const auto enemyBase = BasesTools::GetEnemyBasePosition();
    BWAPI::Unitset mutalisks, guardians, devourers, queens, combat, defaults;
    std::vector<BWAPI::Position> groundPositions;
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
        if (!type.isWorker() && !type.isBuilding() && type.canAttack()) {
            combat.insert(unit);
            if (!unit->isFlying()) groundPositions.push_back(unit->getPosition());
        }
    }
    // A Mutalisk flying home to morph is out of the flock and the raid squad.
    if (airMorpher >= 0) {
        for (auto muta : mutalisks) if (muta->getID() == airMorpher) { mutalisks.erase(muta); break; }
    }
    // Raiders leave the main flock, so escort and regroup centers ignore them.
    const auto raid = UpdateRaidSquad(mutalisks, pressureWave, rally);
    for (auto raider : raid.raiders) mutalisks.erase(raider);
    const auto center = UnitCenter(combat, rally);
    // Ground units regroup on their densest cluster, never on the mean of the front line and fresh reinforcements.
    const auto groundBody = CombatPolicy::MainBody(groundPositions, 384, rally);
    // Static defense first: which colonies the army stays out of shapes where it goes.
    const bool guardianSiege = CombatPolicy::GuardianSiegeActive(static_cast<int>(guardians.size()));
    UpdateStaticCover(guardianSiege, threats);
    // The nearest known enemy base, preferring one not sitting under static defense we are not assaulting.
    // A covered objective becomes the edge of that cover, where the army gathers until it can go in.
    groundObjective = BWAPI::Positions::None;
    bool objectiveCovered = true;
    for (const auto& depot : enemyDepots) {
        const bool covered = GroundCovered(depot.second, 0);
        if (!groundObjective.isValid() || (objectiveCovered && !covered) ||
            (covered == objectiveCovered && groundBody.getApproxDistance(depot.second) < groundBody.getApproxDistance(groundObjective))) {
            groundObjective = depot.second;
            objectiveCovered = covered;
        }
    }
    groundObjective = SafeObjective(groundBody, groundObjective.isValid() ? groundObjective : enemyBase);
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
    // Queens without a Hydra group support the air group, or else march with the ground army's main body
    // (a Ling/Ultralisk army), never with the average of everything including units still at home.
    const bool airGroup = !guardians.empty() || !mutalisks.empty();
    for (auto queen : freeQueens) queenEscorts[queen->getID()] = airGroup ? airCenter : groundBody;

    // Guardians outrange all static defense: the rest of the army leaves it to them and advances behind them.
    const bool aggressive = GetMode() == MicroMode::Aggressive;
    const bool contain = !aggressive && CombatPolicy::GuardianContain(static_cast<int>(guardians.size()), !threats.empty());
    const auto guardianCenter = UnitCenter(guardians, rally);
    const auto siegeTarget = guardians.empty() ? BWAPI::Positions::None : ChooseGuardianSiege(guardianCenter, aggressive);
    siegeEscort = guardianSiege && aggressive ? guardianCenter : BWAPI::Positions::None;
    if (BWAPI::Broodwar->getFrameCount() % 120 == 0)
        MatchLog::Event("queen_support", "hydra_groups=" + std::to_string(groups.size()) + " supported=" + std::to_string(supported) +
            " queens=" + std::to_string(queens.size()));

    BWAPI::Unitset zerglings;
    for (auto unit : myUnits) {
        if (!unit->exists() || !unit->isCompleted() || unit->isLoaded() || unit->isMorphing()) continue;
        if (droneFighters.contains(unit)) continue;
        if (MorphingHome(unit)) continue;
        const auto type = unit->getType();
        const bool army = !type.isWorker() && !type.isBuilding() && type != BWAPI::UnitTypes::Zerg_Larva &&
            type != BWAPI::UnitTypes::Zerg_Overlord && type != BWAPI::UnitTypes::Zerg_Egg;
        if (army && DodgeFriendlySpell(unit)) continue;
        // Guardians outrange static defense and Infested Terrans are spent on it; everything else stays out.
        if (army && type != BWAPI::UnitTypes::Zerg_Guardian && type != BWAPI::UnitTypes::Zerg_Infested_Terran &&
            KeepOutOfStaticDefense(unit, center)) continue;
        if (pressureWave.contains(unit) && threats.empty() &&
            (type == BWAPI::UnitTypes::Zerg_Zergling || type == BWAPI::UnitTypes::Zerg_Hydralisk || type == BWAPI::UnitTypes::Zerg_Mutalisk)) {
            BWAPI::Unit target = nullptr;
            for (auto enemy : unit->getUnitsInRadius(320, BWAPI::Filter::IsEnemy)) {
                if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !unit->canAttack(enemy) ||
                    AvoidsStaticDefense(unit, enemy)) continue;
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
            auto escort = groundBody;
            int distance = std::numeric_limits<int>::max();
            for (const auto& group : groups) {
                const auto position = UnitCenter(group, rally);
                if (unit->getDistance(position) < distance) { escort = position; distance = unit->getDistance(position); }
            }
            LurkerSupportLoop(unit, threats, rally, escort);
        } else if (type == BWAPI::UnitTypes::Zerg_Zergling) {
            zerglings.insert(unit);
        } else if (type == BWAPI::UnitTypes::Zerg_Ultralisk) {
            GroundArmyLoop(unit, threats, rally, groundBody);
        } else if (type == BWAPI::UnitTypes::Zerg_Infested_Terran) {
            InfestedTerranLoop(unit, threats, groundBody, rally);
        } else if (type == BWAPI::UnitTypes::Zerg_Scourge) {
            ScourgeLoop(unit, threats, !guardians.empty() ? guardianCenter : !mutalisks.empty() ? UnitCenter(mutalisks, rally) : rally);
        } else if (type == BWAPI::UnitTypes::Zerg_Defiler) {
            DefilerLoop(unit, groundBody, rally);
        } else if (type == BWAPI::UnitTypes::Zerg_Queen) {
            if (QueenCastLoop(unit, BWAPI::Broodwar->getAllUnits())) continue;
            BWAPI::Unit danger = nullptr;
            for (auto enemy : unit->getUnitsInRadius(256, BWAPI::Filter::IsEnemy)) {
                if (enemy->getType().airWeapon() != BWAPI::WeaponTypes::None &&
                    unit->getDistance(enemy) < enemy->getType().airWeapon().maxRange() + 32) { danger = enemy; break; }
            }
            if (danger) {
                // A hunt that runs into anti-air is dropped for a while.
                const auto prey = queenPrey.find(unit->getID());
                if (prey != queenPrey.end()) { preyAvoid[prey->second] = BWAPI::Broodwar->getFrameCount() + 24 * 45; queenPrey.erase(prey); }
                Flee(unit, danger);
                continue;
            }
            if (BroodlingHunt(unit)) continue;
            // Trail the escorted group a little, and move right up with it when a spell is ready and the
            // enemy is close, so spell targets come within cast range.
            auto escort = queenEscorts[unit->getID()];
            const bool enemyNear = escort.isValid() && !BWAPI::Broodwar->getUnitsInRadius(escort, CombatPolicy::QueenCastSearch, BWAPI::Filter::IsEnemy).empty();
            const int trail = CombatPolicy::QueenTrail(unit->getEnergy(), enemyNear);
            const auto towardHome = rally - escort;
            const double length = std::sqrt(double(towardHome.x) * towardHome.x + double(towardHome.y) * towardHome.y);
            if (trail > 0 && length > trail) escort += BWAPI::Position(int(towardHome.x * trail / length), int(towardHome.y * trail / length));
            if (escort.isValid() && unit->getDistance(escort) > 64) SmartMove(unit, escort);
        } else if (type == BWAPI::UnitTypes::Zerg_Overlord) {
            BWAPI::Unit danger = nullptr;
            for (auto enemy : unit->getUnitsInRadius(320, BWAPI::Filter::IsEnemy)) {
                if (enemy->getType().airWeapon() != BWAPI::WeaponTypes::None) { danger = enemy; break; }
            }
            if (danger) Flee(unit, danger);
            else if (unit == scout && !enemyBase.isValid()) ScoutAndWander(unit);
            else if (unit == scout && GetMode() != MicroMode::Aggressive) ScoutEnemyBuild(unit, enemyBase);
            else if (unit == scout) SmartMove(unit, center);
            else {
                const auto spread = OverlordSpread(unit, rally);
                // A cloaked/burrowed enemy army needs a detector nearby: the spare Overlord closer
                // to the fight than to its spread point escorts the attack instead of sitting at home.
                if (needsDetection && aggressive && unit->getDistance(center) < unit->getDistance(spread))
                    SmartMove(unit, center);
                else SmartMove(unit, spread);
            }
        } else if (type == BWAPI::UnitTypes::Zerg_Mutalisk && raid.raiders.contains(unit)) {
            // Raiders already in the enemy mineral line finish the job instead of flying home.
            if ((!raid.target.isValid() || unit->getDistance(raid.target) > 320) && DefendBases(unit, threats)) continue;
            const bool regen = CombatPolicy::HarassNeedsRegen(unit->getHitPoints(), type.maxHitPoints(), harassRegen.count(unit->getID()) > 0);
            if (regen) harassRegen.insert(unit->getID()); else harassRegen.erase(unit->getID());
            MutaliskRaidLoop(unit, raid.target, raid.center, rally, raid.retreat || regen);
        } else if (type == BWAPI::UnitTypes::Zerg_Scourge) {
            if (DefendBases(unit, threats)) continue;
            if (!aggressive) {
                if (!AttackIfSafelyInRange(unit, 320))
                    SmartMove(unit, threats.empty() ? ControlPoint(unit, rally) : rally);
                continue;
            }
            ScourgeStrikeLoop(unit, flockCenter);
        } else if (type == BWAPI::UnitTypes::Zerg_Mutalisk || type == BWAPI::UnitTypes::Zerg_Guardian || type == BWAPI::UnitTypes::Zerg_Devourer) {
            if (DefendBases(unit, threats)) continue;
            // A containing Guardian group keeps its Devourer escort; Mutalisks stay home to defend and raid.
            const bool containing = contain && type != BWAPI::UnitTypes::Zerg_Mutalisk;
            if (!aggressive && !containing) {
                // Idle at rally still takes a safe free kill instead of ignoring whatever wanders by,
                // and otherwise spreads out for map control rather than camping the whole flock at home.
                if (!AttackIfSafelyInRange(unit, type == BWAPI::UnitTypes::Zerg_Guardian ?
                    unit->getType().groundWeapon().maxRange() : 320))
                    SmartMove(unit, threats.empty() ? ControlPoint(unit, rally) : rally);
                continue;
            }
            if (type == BWAPI::UnitTypes::Zerg_Devourer) DevourerEscortLoop(unit, flockCenter);
            else if (type == BWAPI::UnitTypes::Zerg_Guardian) {
                // Slow and fragile: travel as one group and only split off to fight.
                if (unit->getDistance(guardianCenter) > 256 && unit->getUnitsInRadius(320, BWAPI::Filter::IsEnemy).empty()) {
                    SmartMove(unit, guardianCenter);
                    continue;
                }
                GuardianAssaultLoop(unit, BWAPI::Broodwar->getAllUnits(), UnitCenter(devourers.empty() ? mutalisks : devourers, rally), siegeTarget);
            } else if (unit->getDistance(UnitCenter(mutalisks, rally)) > 256 && unit->getUnitsInRadius(224, BWAPI::Filter::IsEnemy).empty())
                SmartMove(unit, UnitCenter(mutalisks, rally));
            else MutaliskHarassLoop(unit, BWAPI::Broodwar->getAllUnits());
        } else defaults.insert(unit);
    }
    ZerglingSquadLoop(zerglings, threats, rally, groundBody);
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
        if (AvoidsStaticDefense(muta, enemy)) continue;
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

void Micro::GuardianAssaultLoop(BWAPI::Unit guardian, BWAPI::Unitset enemies, BWAPI::Position fallback, BWAPI::Position siegeTarget) {
    if (!guardian) return;
    const int range = guardian->getPlayer()->weaponMaxRange(guardian->getType().groundWeapon());
    if (!fallback.isValid()) fallback = BWAPI::Position(BasesTools::GetMainBasePosition());

    std::vector<BWAPI::Unit> threats, antiAir;
    BWAPI::Unit outranger = nullptr, target = nullptr, approach = nullptr;
    // Kill what can shoot air first, then the army, static defense and new bases, then workers, then buildings.
    const auto rank = [](BWAPI::Unit enemy) {
        if (AirThreatRange(enemy) >= 0) return 0;
        const auto type = enemy->getType();
        if (type.isWorker()) return 2;
        if (type.isBuilding()) return StaticReach(enemy, false) >= 0 || (type.isResourceDepot() && !enemy->isCompleted()) ? 1 : 3;
        return 1;
    };
    for (auto enemy : guardian->getUnitsInRadius(range + 320, BWAPI::Filter::IsEnemy)) {
        if (!enemy->exists() || !enemy->isVisible()) continue;
        const int dist = guardian->getDistance(enemy);
        const int threatRange = AirThreatRange(enemy);
        if (threatRange >= 0) antiAir.push_back(enemy);
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
    // Prefer a perch over cliffs or unwalkable ground at max range, where anti-air cannot simply walk up.
    if (guardian->getGroundWeaponCooldown() > 0) {
        if (!threats.empty()) {
            const auto perch = target ? GuardianPerch(guardian, target, range, antiAir) : BWAPI::Positions::None;
            BWAPI::Broodwar->drawTextMap(guardian->getPosition(), perch.isValid() ? "Siege: perch" : "Siege: reposition");
            SmartMove(guardian, perch.isValid() ? perch : AwayFrom(guardian, threats, fallback));
        }
        return;
    }
    if (target) {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Sieging");
        SmartAttackUnit(guardian, target);
        return;
    }
    if (approach) {
        const auto perch = GuardianPerch(guardian, approach, range, antiAir);
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), perch.isValid() ? "Siege: perch" : "Siege: approach");
        if (perch.isValid()) SmartMove(guardian, perch);
        else SmartAttackUnit(guardian, approach);
        return;
    }
    BWAPI::Position targetPos = siegeTarget.isValid() ? siegeTarget : BasesTools::GetEnemyBasePosition();
    if (targetPos != BWAPI::Positions::None) {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), siegeTarget.isValid() ? "Siege: contain" : "Assaulting base");
        SmartMove(guardian, targetPos);
    } else {
        BWAPI::Broodwar->drawTextMap(guardian->getPosition(), "Scouting");
        ScoutAndWander(guardian);
    }
}

bool Micro::SpellReserved(BWAPI::TechType tech, BWAPI::Unit target, BWAPI::Position position) {
    for (auto ally : BWAPI::Broodwar->self()->getUnits()) {
        if (ally->getType() != BWAPI::UnitTypes::Zerg_Queen && ally->getType() != BWAPI::UnitTypes::Zerg_Defiler) continue;
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
    // BWAPI records an Infestation order as a right-click on the Command Center.
    const auto infesting = [](BWAPI::UnitCommand order) {
        return order.getType() == BWAPI::UnitCommandTypes::Right_Click_Unit && order.getTarget() &&
            order.getTarget()->getType() == BWAPI::UnitTypes::Terran_Command_Center;
    };
    if (age <= 0 || !queen->isInterruptible() || queen->getSpellCooldown() > 0 ||
        ((command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit || command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Position ||
          infesting(command)) && age <= BWAPI::Broodwar->getLatencyFrames() + 24)) return true;
    // A healthy Queen walks up to its 9-tile cast range for targets a little further out, instead of
    // waiting at the back until the fight comes to it.
    const int search = queen->getHitPoints() * 2 >= queen->getType().maxHitPoints() ? CombatPolicy::QueenCastSearch : 9 * 32;
    BWAPI::Unitset nearby;
    for (auto enemy : enemies) {
        if (enemy->exists() && enemy->isVisible() && BWAPI::Broodwar->self()->isEnemy(enemy->getPlayer()) && queen->getDistance(enemy) <= search)
            nearby.insert(enemy);
    }
    // A Command Center below half health is ours for free (no energy, no research): infest it,
    // unless anti-air guards it.
    const auto infest = BWAPI::TechTypes::Infestation;
    for (auto center : nearby) {
        if (center->getType() != BWAPI::UnitTypes::Terran_Command_Center || !center->isCompleted() ||
            center->getHitPoints() * 2 >= center->getType().maxHitPoints() || !queen->canUseTech(infest, center)) continue;
        bool taken = false;
        for (auto ally : BWAPI::Broodwar->self()->getUnits())
            taken = taken || (ally != queen && ally->getType() == BWAPI::UnitTypes::Zerg_Queen &&
                infesting(ally->getLastCommand()) && ally->getLastCommand().getTarget() == center &&
                BWAPI::Broodwar->getFrameCount() - ally->getLastCommandFrame() <= 24 * 10);
        if (taken) continue;
        int antiAir = 0;
        for (auto guard : nearby)
            if (guard->getType().airWeapon() != BWAPI::WeaponTypes::None && guard->getDistance(center) <= 192) ++antiAir;
        if (antiAir > 1) continue;
        if (queen->useTech(infest, center)) {
            MatchLog::Event("queen_spell", "Infestation queen=" + std::to_string(queen->getID()) + " target=" + std::to_string(center->getID()));
            return true;
        }
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
        // Ensnare slows our own units too: weigh our attackers under the cloud against the enemies caught.
        std::vector<BWAPI::Unit> allies;
        for (auto ally : BWAPI::Broodwar->self()->getUnits()) {
            if (ally->exists() && !ally->getType().isBuilding() && ally->getType().canAttack() &&
                queen->getDistance(ally) <= search + CombatPolicy::EnsnareRadius) allies.push_back(ally);
        }
        BWAPI::Unit target = nullptr;
        int bestScore = 0, bestCluster = 0;
        for (auto candidate : nearby) {
            if (candidate->getType().isBuilding() || candidate->isEnsnared() ||
                SpellReserved(ensnare, nullptr, candidate->getPosition()) || !queen->canUseTech(ensnare, candidate->getPosition())) continue;
            int cluster = 0, caught = 0;
            for (auto enemy : nearby) {
                if (!enemy->getType().isBuilding() && enemy->getType().canAttack() && !enemy->isEnsnared() &&
                    enemy->getDistance(candidate) <= CombatPolicy::EnsnareRadius) ++cluster;
            }
            for (auto ally : allies)
                if (ally->getDistance(candidate) <= CombatPolicy::EnsnareRadius) ++caught;
            const int score = CombatPolicy::EnsnareScore(cluster, caught);
            if (score > bestScore) { target = candidate; bestScore = score; bestCluster = cluster; }
        }
        if (target && queen->useTech(ensnare, target->getPosition())) {
            // Allies still near the impact point get out of the way while the spell is in flight.
            MarkSpellArea(target->getPosition(), CombatPolicy::EnsnareRadius);
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

bool Micro::BroodlingHunt(BWAPI::Unit queen) {
    const auto self = BWAPI::Broodwar->self();
    const auto broodlings = BWAPI::TechTypes::Spawn_Broodlings;
    const int frame = BWAPI::Broodwar->getFrameCount();
    if (!queen || !self->hasResearched(broodlings) || queen->getEnergy() < broodlings.energyCost() ||
        queen->getHitPoints() * 2 < queen->getType().maxHitPoints()) {
        if (queen) queenPrey.erase(queen->getID());
        return false;
    }
    for (auto it = preyAvoid.begin(); it != preyAvoid.end();) it = it->second <= frame ? preyAvoid.erase(it) : std::next(it);
    const auto current = queenPrey.find(queen->getID());
    BWAPI::Unit best = nullptr;
    double bestScore = 0;
    for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
        if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !self->isEnemy(enemy->getPlayer()) ||
            enemy->isFlying() || enemy->getType().isBuilding() || preyAvoid.count(enemy->getID())) continue;
        const auto type = enemy->getType();
        int value = type.mineralPrice() + type.gasPrice() * 2;
        if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode || type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
            type == BWAPI::UnitTypes::Protoss_High_Templar || type == BWAPI::UnitTypes::Zerg_Defiler ||
            type == BWAPI::UnitTypes::Zerg_Lurker) value += 500;
        if (value < 200 || !queen->canUseTech(broodlings, enemy) || SpellReserved(broodlings, enemy, enemy->getPosition())) continue;
        // Another Queen is already fishing for this one.
        bool taken = false;
        for (const auto& [hunter, prey] : queenPrey) taken = taken || (hunter != queen->getID() && prey == enemy->getID());
        if (taken) continue;
        double antiAir = 0;
        for (auto guard : BWAPI::Broodwar->getUnitsInRadius(enemy->getPosition(), 256, BWAPI::Filter::IsEnemy)) {
            if (!guard->isVisible() || AirThreatRange(guard) < 0) continue;
            antiAir += guard->getType().isBuilding() ? 2.0 : std::max(1, guard->getType().supplyRequired()) / 2.0;
        }
        double score = CombatPolicy::BroodlingHuntScore(value, queen->getDistance(enemy), antiAir);
        if (score > 0 && current != queenPrey.end() && current->second == enemy->getID()) score += 100; // Stay on the hunt.
        if (score > bestScore) { bestScore = score; best = enemy; }
    }
    if (!best) { queenPrey.erase(queen->getID()); return false; }
    if (current == queenPrey.end() || current->second != best->getID())
        MatchLog::Event("broodling_hunt", "queen=" + std::to_string(queen->getID()) + " prey=" + best->getType().getName());
    queenPrey[queen->getID()] = best->getID();
    // In cast range QueenCastLoop takes the shot on the next frame.
    BWAPI::Broodwar->drawLineMap(queen->getPosition(), best->getPosition(), BWAPI::Colors::Purple);
    SmartMove(queen, best->getPosition());
    return true;
}

namespace {
    // Nydus Canal ends, refreshed once per frame.
    std::vector<BWAPI::Unit> nydusCanals;
    int nydusFrame = -1;
}

bool Micro::UseNydus(BWAPI::Unit unit, BWAPI::Position destination) {
    if (!unit || unit->isFlying() || unit->isBurrowed() || !destination.isValid()) return false;
    const int frame = BWAPI::Broodwar->getFrameCount();
    if (nydusFrame != frame) {
        nydusFrame = frame;
        nydusCanals.clear();
        for (auto canal : BWAPI::Broodwar->self()->getUnits())
            if (canal->getType() == BWAPI::UnitTypes::Zerg_Nydus_Canal && canal->isCompleted() &&
                canal->getNydusExit() && canal->getNydusExit()->isCompleted()) nydusCanals.push_back(canal);
    }
    if (nydusCanals.empty()) return false;
    // Already walking into a canal.
    const auto command = unit->getLastCommand();
    if (command.getType() == BWAPI::UnitCommandTypes::Right_Click_Unit && command.getTarget() &&
        command.getTarget()->getType() == BWAPI::UnitTypes::Zerg_Nydus_Canal && !unit->isIdle() &&
        frame - unit->getLastCommandFrame() < 24 * 6) return true;
    if (unit->getLastCommandFrame() >= frame) return false;
    for (auto canal : nydusCanals) {
        if (!CombatPolicy::NydusShortcut(unit->getDistance(canal), canal->getNydusExit()->getDistance(destination),
            unit->getDistance(destination))) continue;
        return unit->rightClick(canal);
    }
    return false;
}

void Micro::ScourgeLoop(BWAPI::Unit scourge, const BWAPI::Unitset& threats, BWAPI::Position guard) {
    if (!scourge || scourge->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    BWAPI::Unitset candidates = scourge->getUnitsInRadius(12 * 32, BWAPI::Filter::IsEnemy);
    for (auto threat : threats) candidates.insert(threat);
    BWAPI::Unit best = nullptr;
    double bestScore = std::numeric_limits<double>::max();
    for (auto enemy : candidates) {
        const auto type = enemy->getType();
        if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || !enemy->isFlying() || type.isBuilding() ||
            type == BWAPI::UnitTypes::Protoss_Interceptor || type == BWAPI::UnitTypes::Protoss_Scarab || !scourge->canAttack(enemy)) continue;
        // Enough Scourge already flying at it to finish it: pick another target.
        int assigned = 0;
        for (auto ally : enemy->getUnitsInRadius(12 * 32, BWAPI::Filter::IsOwned))
            if (ally != scourge && ally->getType() == BWAPI::UnitTypes::Zerg_Scourge && ally->getOrderTarget() == enemy) ++assigned;
        if (assigned >= CombatPolicy::ScourgeNeeded(enemy->getHitPoints() + enemy->getShields())) continue;
        const int value = type.mineralPrice() + type.gasPrice() * 2;
        const double score = scourge->getDistance(enemy) - value * 0.5;
        if (score < bestScore) { bestScore = score; best = enemy; }
    }
    if (best) { SmartAttackUnit(scourge, best); return; }
    if (guard.isValid() && scourge->getDistance(guard) > 96) SmartMove(scourge, guard);
}

void Micro::DefilerLoop(BWAPI::Unit defiler, BWAPI::Position follow, BWAPI::Position home) {
    if (!defiler || !defiler->isCompleted() || defiler->isBurrowed()) return;
    const auto self = BWAPI::Broodwar->self();
    const int frame = BWAPI::Broodwar->getFrameCount();
    const auto command = defiler->getLastCommand();
    const int age = frame - defiler->getLastCommandFrame();
    if (age <= 0 || !defiler->isInterruptible() ||
        ((command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit || command.getType() == BWAPI::UnitCommandTypes::Use_Tech_Position) &&
         age <= BWAPI::Broodwar->getLatencyFrames() + 24)) return;

    BWAPI::Unitset enemies, allies;
    for (auto unit : defiler->getUnitsInRadius(12 * 32)) {
        if (!unit->exists() || !unit->isCompleted() || unit->getType().isBuilding()) continue;
        if (unit->getPlayer() == self) {
            if (!unit->isFlying() && !unit->getType().isWorker() && unit->getType().canAttack()) allies.insert(unit);
        } else if (self->isEnemy(unit->getPlayer()) && unit->isVisible()) enemies.insert(unit);
    }

    // Dark Swarm: our ground units under fire from ranged enemies stop taking ranged damage.
    const auto swarm = BWAPI::TechTypes::Dark_Swarm;
    if (defiler->getEnergy() >= swarm.energyCost() && self->hasResearched(swarm)) {
        BWAPI::Position bestSpot = BWAPI::Positions::None;
        int bestAllies = 0;
        for (auto ally : allies) {
            int shooters = 0, grouped = 0;
            for (auto enemy : enemies) {
                const auto weapon = enemy->getType().groundWeapon();
                const bool ranged = enemy->getType() == BWAPI::UnitTypes::Terran_Bunker ||
                    (weapon != BWAPI::WeaponTypes::None && weapon.maxRange() >= 64);
                if (ranged && enemy->getDistance(ally) <= std::max(weapon.maxRange(), 160) + 32) ++shooters;
            }
            if (shooters < 2) continue;
            for (auto other : allies) if (other->getDistance(ally) <= 96) ++grouped;
            if (grouped < 3 || grouped <= bestAllies || SpellReserved(swarm, nullptr, ally->getPosition())) continue;
            bool covered = false;
            for (auto spell : BWAPI::Broodwar->getUnitsInRadius(ally->getPosition(), 128))
                covered = covered || spell->getType() == BWAPI::UnitTypes::Spell_Dark_Swarm;
            if (covered) continue;
            bestAllies = grouped;
            bestSpot = ally->getPosition();
        }
        if (bestSpot.isValid() && defiler->useTech(swarm, bestSpot)) {
            MatchLog::Event("defiler_spell", "Dark_Swarm allies=" + std::to_string(bestAllies));
            return;
        }
    }

    // Plague on packed enemies, weighed like Ensnare so our own army is not caught in it.
    const auto plague = BWAPI::TechTypes::Plague;
    if (defiler->getEnergy() >= plague.energyCost() && self->hasResearched(plague)) {
        BWAPI::Unit target = nullptr;
        int bestScore = 0;
        for (auto candidate : enemies) {
            if (candidate->isPlagued() || SpellReserved(plague, nullptr, candidate->getPosition())) continue;
            int caught = 0, ours = 0;
            for (auto enemy : enemies) if (!enemy->isPlagued() && enemy->getDistance(candidate) <= 64) ++caught;
            for (auto ally : allies) if (ally->getDistance(candidate) <= 64) ++ours;
            const int score = CombatPolicy::PlagueScore(caught, ours);
            if (score > bestScore) { bestScore = score; target = candidate; }
        }
        if (target && defiler->useTech(plague, target->getPosition())) {
            MatchLog::Event("defiler_spell", "Plague targets=" + std::to_string(bestScore));
            return;
        }
    }

    // Consume a Zergling away from the fighting to refill energy.
    const auto consume = BWAPI::TechTypes::Consume;
    if (defiler->getEnergy() < plague.energyCost() && self->hasResearched(consume)) {
        BWAPI::Unit food = nullptr;
        for (auto ally : defiler->getUnitsInRadius(6 * 32, BWAPI::Filter::IsOwned)) {
            if (ally->getType() != BWAPI::UnitTypes::Zerg_Zergling || !ally->isCompleted() ||
                !ally->getUnitsInRadius(192, BWAPI::Filter::IsEnemy).empty()) continue;
            if (!food || defiler->getDistance(ally) < defiler->getDistance(food)) food = ally;
        }
        if (food && defiler->useTech(consume, food)) {
            MatchLog::Event("defiler_spell", "Consume");
            return;
        }
    }

    // Stay just behind the army, out of reach of enemy ground fire.
    BWAPI::Unit danger = nullptr;
    for (auto enemy : enemies) {
        const auto weapon = enemy->getType().groundWeapon();
        if (weapon != BWAPI::WeaponTypes::None && defiler->getDistance(enemy) <= weapon.maxRange() + 64) { danger = enemy; break; }
    }
    if (danger) { FallBack(defiler, danger, follow); return; }
    auto spot = follow.isValid() ? follow : home;
    const auto towardHome = home - spot;
    const double length = std::sqrt(double(towardHome.x) * towardHome.x + double(towardHome.y) * towardHome.y);
    if (length > 128) spot += BWAPI::Position(int(towardHome.x * 128 / length), int(towardHome.y * 128 / length));
    if (spot.isValid() && defiler->getDistance(spot) > 64) SmartMove(defiler, spot);
}

void Micro::InfestedTerranLoop(BWAPI::Unit unit, const BWAPI::Unitset& threats, BWAPI::Position follow, BWAPI::Position rally) {
    if (!unit || unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount()) return;
    const auto self = BWAPI::Broodwar->self();
    // The blast is spent where it does the most: packed enemy ground units, a sieged tank, static defense.
    BWAPI::Unit best = nullptr;
    int bestScore = 0;
    for (auto enemy : unit->getUnitsInRadius(10 * 32, BWAPI::Filter::IsEnemy)) {
        if (!enemy->exists() || !enemy->isVisible() || !enemy->isDetected() || enemy->isFlying()) continue;
        const auto type = enemy->getType();
        const bool staticDefense = type.isBuilding() && StaticReach(enemy, false) >= 0;
        if (type.isBuilding() && !staticDefense) continue;
        const bool highValue = type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode || type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
            type == BWAPI::UnitTypes::Protoss_High_Templar || type == BWAPI::UnitTypes::Protoss_Reaver ||
            type == BWAPI::UnitTypes::Protoss_Archon || type == BWAPI::UnitTypes::Zerg_Lurker || type == BWAPI::UnitTypes::Zerg_Defiler;
        int enemySupply = 0, ourSupply = 0;
        for (auto other : BWAPI::Broodwar->getUnitsInRadius(enemy->getPosition(), CombatPolicy::InfestedTerranBlast)) {
            if (!other->exists() || other->isFlying() || other->getType().isBuilding() || other == unit) continue;
            const int supply = std::max(1, other->getType().supplyRequired() / 2);
            if (other->getPlayer() == self) ourSupply += supply;
            else if (self->isEnemy(other->getPlayer())) enemySupply += supply;
        }
        const int score = CombatPolicy::InfestedTerranScore(enemySupply, staticDefense, highValue, ourSupply);
        if (score > bestScore) { bestScore = score; best = enemy; }
    }
    if (best) {
        BWAPI::Broodwar->drawTextMap(unit->getPosition(), "Infested: detonate");
        SmartAttackUnit(unit, best);
        return;
    }
    // Head for a raid on our bases; the blast goes off once the attackers bunch up in reach.
    BWAPI::Unit threat = nullptr;
    for (auto enemy : threats)
        if (!enemy->isFlying() && enemy->isDetected() && (!threat || unit->getDistance(enemy) < unit->getDistance(threat))) threat = enemy;
    if (threat) { SmartMove(unit, threat->getPosition()); return; }
    // Otherwise travel with the army, never ahead of it.
    const auto spot = GetMode() == MicroMode::Aggressive && follow.isValid() ? follow : rally;
    if (spot.isValid() && unit->getDistance(spot) > 128) SmartMove(unit, spot);
}
