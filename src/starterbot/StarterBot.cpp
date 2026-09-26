#include "StarterBot.h"
#include "Tools.h"
#include "MatchLog.h"
#include "MapTools.h"
#include "buildorders/4Pool.h"
#include "../../visualstudio/BasesTools.h"
#include "../../visualstudio/src/starterbot/BuildOrder.h"
#include "Factory/BuildOrderFactory.h"
#include "micro.h"
#include "stats/stats.h"
#include <random>
#include <cstdlib>

StarterBot::StarterBot()
{
    
}

BuildOrder* currentBuildOrder;

int unusedSupplyAccepted = 1;
int mineralsFrame = 0;

// Called when the bot starts!
void StarterBot::onStart()
{
    Micro::SetMode(Micro::MicroMode::Aggressive);
    BasesTools::SetOurBasePosition();
    std::string oponentName = "";
    std::string oponentRace = "";
    for (auto player : BWAPI::Broodwar->getPlayers()) {
        if (player != BWAPI::Broodwar->self() && player != BWAPI::Broodwar->neutral()) {
            oponentName = player->getName();
            oponentRace = player->getRace().getName();
        }
    }
    // Adaptive learns compositions and step timings itself; the fixed builds remain for diagnostics.
    auto selectedBuildOrder = BuildOrderType::Adaptive;
    if (const char* requested = std::getenv("IKKRIUS_STRATEGY")) {
        if (std::string(requested) == "MutaHive") selectedBuildOrder = BuildOrderType::MutaHive;
        else if (std::string(requested) == "HiveTech") selectedBuildOrder = BuildOrderType::HiveTech;
    }
    BasesTools::Initialize();
    mineralsFrame = 0;
    currentBuildOrder = BuildOrderFactory::Create(selectedBuildOrder);
    BWAPI::Broodwar->printf("Strategy selected: %s", currentBuildOrder->GetName().c_str());
    std::cout << "Using strategy " << currentBuildOrder->GetName() << "\n";

    // Set our BWAPI options here    
	BWAPI::Broodwar->setLocalSpeed(0); //32
    BWAPI::Broodwar->setFrameSkip(0); //0
    
    // Enable the flag that tells BWAPI top let users enter input while bot plays
    BWAPI::Broodwar->enableFlag(BWAPI::Flag::UserInput);

    // Call MapTools OnStart
    m_mapTools.onStart();
    BasesTools::FindExpansions();

    MatchLog::Start(currentBuildOrder->GetName());
    currentBuildOrder->onStart();
}

// Called on each frame of the game
void StarterBot::onFrame()
{
    if (BWAPI::BWAPI_isDebug()) {
        BasesTools::DrawEnemyBases(BWAPI::Colors::Orange);
        BasesTools::DrawAllBases(BWAPI::Colors::Yellow);
        Tools::DrawUnitHealthBars();
        drawDebugInformation();
    }
    BasesTools::VerifyEnemyBases();
    m_mapTools.onFrame();
    buildAdditionalSupply();

    currentBuildOrder->Execute();
}


// Train more workers so we can gather more income
bool StarterBot::trainUnit(BWAPI::UnitType unit) {
    if (unit.whatBuilds().first == BWAPI::UnitTypes::Zerg_Larva) {
        return Tools::MorphLarva(unit);
    }
    const BWAPI::Unit myDepot = Tools::GetDepot();

    // if we have a valid depot unit and it's currently not training something, train a worker
    // there is no reason for a bot to ever use the unit queueing system, it just wastes resources
    if (myDepot) {
        return myDepot->train(unit);
    }
    return false;
}

// Build more supply if we are going to run out soon
void StarterBot::buildAdditionalSupply()
{
    if (BWAPI::Broodwar->self()->supplyTotal() >= 400) return;
    // Get the amount of supply supply we currently have unused
    const int unusedSupply = Tools::GetTotalSupply(true) - BWAPI::Broodwar->self()->supplyUsed();

    // If we have a sufficient amount of supply, we don't need to do anything
    if (unusedSupply >= unusedSupplyAccepted && unusedSupply != 1) { return; }

    // Otherwise, we are going to build a supply provider
    const BWAPI::UnitType supplyProviderType = BWAPI::Broodwar->self()->getRace().getSupplyProvider();

    const bool startedBuilding = trainUnit(supplyProviderType);
    if (startedBuilding)
    {
        BWAPI::Broodwar->printf("Started Building %s", supplyProviderType.getName().c_str());
    }
}

// Draw some relevent information to the screen to help us debug the bot
void StarterBot::drawDebugInformation()
{
    BWAPI::Broodwar->drawTextScreen(BWAPI::Position(10, 10), "Ikkrius\n");
    Tools::DrawUnitCommands();
    Tools::DrawUnitBoundingBoxes();
}

// Called whenever the game ends and tells you if you won or not
void StarterBot::onEnd(bool isWinner)
{
    std::string oponentName = "";
    std::string oponentRace = "";
    for (auto player : BWAPI::Broodwar->getPlayers()) {
        if (player != BWAPI::Broodwar->self() && player != BWAPI::Broodwar->neutral()) {
            oponentName = player->getName();
            oponentRace = player->getRace().getName();
        }
    }
    MatchLog::End(isWinner ? "win" : "loss");
    currentBuildOrder->onEnd(isWinner);
    std::cout << "We " << (isWinner ? "won!" : "lost!") << "\n";
    Stats::updateWinRateFile(oponentName, oponentRace, BWAPI::Broodwar->mapHash(), currentBuildOrder->GetName(), isWinner);
}

// Called whenever a unit is destroyed, with a pointer to the unit
void StarterBot::onUnitDestroy(BWAPI::Unit unit)
{
    MatchLog::UnitEvent("destroy", unit);
    currentBuildOrder->onUnitDestroy(unit);
}

// Called whenever a unit is morphed, with a pointer to the unit
// Zerg units morph when they turn into other units
void StarterBot::onUnitMorph(BWAPI::Unit unit)
{
    MatchLog::UnitEvent("morph", unit);
    currentBuildOrder->onUnitMorph(unit);
}

// Called whenever a text is sent to the game by a user
void StarterBot::onSendText(std::string text) 
{ 
    if (text == "m")
    {
        m_mapTools.toggleDraw();
    }
    if (text == "a") { //Attack
        attack();
    }
    if (text == "d") { //Defend
    }
    if (text == "e") { //Expand
    }
    if (text == "1") { //
        BWAPI::Broodwar->setLocalSpeed(0); //32
    }
    if (text == "2") { //
        BWAPI::Broodwar->setLocalSpeed(8); //32
    }
    if (text == "3") { //
        BWAPI::Broodwar->setLocalSpeed(32); //32
    }
    if (text == "4") { //
        BWAPI::Broodwar->setLocalSpeed(128); //32
    }
    if (text == "s") { //
        Micro::SetMode(Micro::MicroMode::Neutral);
    }
    currentBuildOrder->onSendText(text);
}

void StarterBot::attack() {
    BWAPI::Position enemyBase = BasesTools::GetEnemyBasePosition();
    if (enemyBase == BWAPI::Positions::None) {
        // fallback: attack nearest enemy unit as before
        const BWAPI::Unitset& myUnits = BWAPI::Broodwar->self()->getUnits();
        for (auto& unit : myUnits) {
            if (!unit->getType().isWorker() && unit->getType() != BWAPI::UnitTypes::Zerg_Overlord) {
                BWAPI::Unit nearestEnemy = nullptr;
                int minDistance = std::numeric_limits<int>::max();
                for (auto enemyUnit : BWAPI::Broodwar->getAllUnits()) {
                    if (enemyUnit->getPlayer() != BWAPI::Broodwar->self() && enemyUnit->getPlayer() != BWAPI::Broodwar->neutral()) {
                        int distance = unit->getDistance(enemyUnit);
                        if (distance < minDistance) {
                            minDistance = distance;
                            nearestEnemy = enemyUnit;
                        }
                    }
                }
                if (nearestEnemy) {
                    unit->attack(nearestEnemy->getPosition());
                }
            }
        }
    } else {
        // attack the known enemy base position
        const BWAPI::Unitset& myUnits = BWAPI::Broodwar->self()->getUnits();
        for (auto& unit : myUnits) {
            if (!unit->getType().isWorker() && unit->getType() != BWAPI::UnitTypes::Zerg_Overlord) {
                unit->attack(enemyBase);
            }
        }
    }
}

// Called whenever a unit is created, with a pointer to the destroyed unit
// Units are created in buildings like barracks before they are visible, 
// so this will trigger when you issue the build command for most units
void StarterBot::onUnitCreate(BWAPI::Unit unit)
{ 
    currentBuildOrder->OnUnitCreate(unit);
}

// Called whenever a unit finished construction, with a pointer to the unit
void StarterBot::onUnitComplete(BWAPI::Unit unit)
{
    MatchLog::UnitEvent("complete", unit);
	currentBuildOrder->onUnitComplete(unit);
}

// Called whenever a unit appears, with a pointer to the destroyed unit
// This is usually triggered when units appear from fog of war and become visible
void StarterBot::onUnitShow(BWAPI::Unit unit)
{ 
    if (BWAPI::Broodwar->self()->isEnemy(unit->getPlayer()) && unit->getType().isBuilding()) {
        BasesTools::SetEnemyBasePosition(unit->getPosition());
        MatchLog::UnitEvent("enemy_building_seen", unit);
    }
    currentBuildOrder->onUnitShow(unit);
}

// Called whenever a unit gets hidden, with a pointer to the destroyed unit
// This is usually triggered when units enter the fog of war and are no longer visible
void StarterBot::onUnitHide(BWAPI::Unit unit)
{
    currentBuildOrder->onUnitHide(unit);
}

// Called whenever a unit switches player control
// This usually happens when a dark archon takes control of a unit
void StarterBot::onUnitRenegade(BWAPI::Unit unit)
{
    currentBuildOrder->onUnitRenegade(unit);
}