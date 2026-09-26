"""Exercise actual Lurker and ground-army control without the game."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'src/starterbot/micro.cpp').read_text()
def function(signature):
    start=text.index(signature); end=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]
source=r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include "CombatPolicy.h"
namespace BWAPI {
struct Position {
    int x=0;
    bool isValid() const { return x!=-999; }
    int getApproxDistance(Position other) const { return std::abs(x-other.x); }
};
struct WeaponType {
    int range=0;
    int maxRange() const { return range; }
    bool operator==(WeaponType other) const { return range==other.range; }
    bool operator!=(WeaponType other) const { return range!=other.range; }
};
namespace Filter { const int IsEnemy=1; }
namespace WeaponTypes { const WeaponType None{}; }
struct UnitType {
    int id=0, supply=2; bool worker=false, building=false, attacking=true;
    WeaponType ground{128}, air{};
    bool isWorker() const { return worker; }
    bool isBuilding() const { return building; }
    bool canAttack() const { return attacking; }
    int supplyRequired() const { return supply; }
    int maxHitPoints() const { return 100; }
    int maxShields() const { return 0; }
    WeaponType groundWeapon() const { return ground; }
    WeaponType airWeapon() const { return air; }
    bool operator==(UnitType other) const { return id==other.id; }
};
namespace UnitTypes { const UnitType Zerg_Lurker{1,4,false,false,true,{192}}, Terran_Bunker{2,0,false,true}; }
namespace UnitCommandTypes { enum { None, Burrow, Unburrow, Attack_Move }; }
struct Command {
    int kind=0; Position position;
    int getType() const { return kind; }
    Position getTargetPosition() const { return position; }
};
struct Player { bool hostile=false; bool isEnemy(Player* other) { return other->hostile; } };
struct FakeUnit;
using Unit=FakeUnit*;
struct Unitset : std::vector<Unit> { using std::vector<Unit>::vector; void insert(Unit unit) { push_back(unit); } };
struct FakeUnit {
    UnitType type; Player* player=nullptr; int x=0;
    bool flying=false, detected=true, buried=false, idle=false;
    int lastFrame=-100, health=100, cooldown=0, burrows=0, unburrows=0, attacks=0;
    Command command; Unitset neighbors;
    UnitType getType() { return type; }
    bool exists() { return true; } bool isCompleted() { return true; }
    bool isFlying() { return flying; } bool isDetected() { return detected; }
    bool isVisible() { return true; } bool isBurrowed() { return buried; }
    bool isIdle() { return idle; }
    bool isMorphing() { return false; } bool isLoaded() { return false; }
    bool isInterruptible() { return true; } int getID() { return 1; }
    bool canBurrow() { return true; } bool canUnburrow() { return true; }
    int getDistance(Unit other) { return std::abs(x-other->x); }
    int getDistance(Position other) { return std::abs(x-other.x); }
    Position getPosition() { return {x}; }
    Unitset getUnitsInRadius(int radius, int filter=0) { Unitset result; for(auto u:neighbors) if(getDistance(u)<=radius && (!filter || u->player->hostile)) result.push_back(u); return result; }
    Player* getPlayer() { return player; }
    int getHitPoints() { return health; } int getShields() { return 0; }
    int getGroundWeaponCooldown() { return cooldown; }
    int getLastCommandFrame() { return lastFrame; }
    Command getLastCommand() { return command; }
    bool burrow() { ++burrows; return true; } bool unburrow() { ++unburrows; return true; }
    Position attackedAt;
    void attack(Position position) { ++attacks; attackedAt=position; }
};
struct Game {
    Player player; int frame=100;
    Player* self() { return &player; }
    int getFrameCount() { return frame; } int getLatencyFrames() { return 3; }
} game;
Game* Broodwar=&game;
}
namespace BasesTools { BWAPI::Position GetEnemyBasePosition() { return {2000}; } }
std::map<int,int> lurkerLastContact;
namespace MatchLog { void Event(const std::string&, const std::string&) {} }
namespace Micro {
void LurkerSupportLoop(BWAPI::Unit,const BWAPI::Unitset&,BWAPI::Position,BWAPI::Position);
enum class MicroMode { Defensive, Aggressive };
MicroMode GetMode() { return MicroMode::Aggressive; }
BWAPI::Unit attacked=nullptr;
BWAPI::Position moved{-999};
void SmartAttackUnit(BWAPI::Unit,BWAPI::Unit enemy) { attacked=enemy; }
void SmartMove(BWAPI::Unit,BWAPI::Position position) { moved=position; }
void ScoutAndWander(BWAPI::Unit) {}
void SmartAttackMove(BWAPI::Unit,BWAPI::Position);
BWAPI::Unit ChooseFocusTarget(BWAPI::Unit unit,const BWAPI::Unitset& candidates,bool) {
    BWAPI::Unit best=nullptr;
    for(auto enemy:candidates) if(!best || unit->getDistance(enemy)<unit->getDistance(best)) best=enemy;
    return best;
}
BWAPI::Unit fellBackFrom=nullptr;
std::vector<BWAPI::Unit> covered; // Enemies sitting under static defense left to Guardians.
bool AvoidsStaticDefense(BWAPI::Unit,BWAPI::Unit enemy) { return std::find(covered.begin(),covered.end(),enemy)!=covered.end(); }
void FallBack(BWAPI::Unit,BWAPI::Unit threat,BWAPI::Position) { fellBackFrom=threat; }
void GroundArmyLoop(BWAPI::Unit,const BWAPI::Unitset&,BWAPI::Position,BWAPI::Position);
}
"""
source+='BWAPI::Position siegeEscort{-999};\n'
source+=function('void Micro::SmartAttackMove')
source+=function('void Micro::LurkerSupportLoop')
source+=function('void Micro::GroundArmyLoop')
source+=r"""
int main() {
    using namespace BWAPI;
    Player enemy; enemy.hostile=true;
    FakeUnit lurker{UnitTypes::Zerg_Lurker,&game.player,0};
    FakeUnit building{{3,0,false,true,false},&enemy,100};
    lurker.neighbors={&lurker,&building};
    Micro::GroundArmyLoop(&lurker,{}, {-400},{0});
    assert(lurker.burrows==1); // Burrow even against a non-attacking building.
    lurker.buried=true;
    Micro::GroundArmyLoop(&lurker,{}, {-400},{0});
    assert(Micro::attacked==&building && lurker.unburrows==0);
    building.x=500;
    Micro::GroundArmyLoop(&lurker,{}, {-400},{0});
    assert(lurker.unburrows==0); // Hold deployment through brief gaps in contact.
    game.frame+=73;
    Micro::GroundArmyLoop(&lurker,{}, {-400},{0});
    assert(lurker.unburrows==1); // Rejoin the army once the engagement really ends.
    lurker.command.kind=UnitCommandTypes::Unburrow; lurker.lastFrame=game.frame-1;
    Micro::GroundArmyLoop(&lurker,{}, {-400},{0});
    assert(lurker.unburrows==1); // Respect burrow-animation/command latency.
    FakeUnit ling{{4,1},&game.player,0}, flyer{{5,4},&enemy,100};
    flyer.flying=true;
    Micro::attacked=nullptr;
    Micro::GroundArmyLoop(&ling,{&flyer},{-400},{0});
    assert(Micro::attacked==nullptr); // Never issue an impossible ground-to-air attack.
    FakeUnit bunker{UnitTypes::Terran_Bunker,&enemy,100};
    ling.neighbors={&ling,&bunker};
    Micro::GroundArmyLoop(&ling,{}, {-400},{0});
    assert(Micro::moved.x==-400); // Regroup rather than feed units to static defenses.
    Micro::attacked=nullptr;
    Micro::GroundArmyLoop(&ling,{&bunker}, {-400},{0});
    assert(Micro::attacked==&bunker); // Base defense takes priority over regrouping.
    ling.attacks=0;
    ling.command={UnitCommandTypes::Attack_Move,{2000}};
    Micro::SmartAttackMove(&ling,{2000}); assert(ling.attacks==0);
    ling.idle=true;
    Micro::SmartAttackMove(&ling,{2000}); assert(ling.attacks==1);
    // Group engagement policy: commit to winnable fights, only rarely gamble on close ones, withdraw otherwise.
    using CombatPolicy::Engagement;
    assert(CombatPolicy::AssessEngagement(13,10)==Engagement::Commit);
    assert(CombatPolicy::AssessEngagement(12,10)==Engagement::Withdraw); // Close: declined by default.
    assert(CombatPolicy::AssessEngagement(12,10,true)==Engagement::HitAndRun); // ...unless gambling.
    assert(CombatPolicy::AssessEngagement(10,12,true)==Engagement::Withdraw); // Losing fights are never gambled.
    assert(CombatPolicy::AssessEngagement(10,17,true)==Engagement::Withdraw);
    assert(CombatPolicy::AssessEngagement(4,0)==Engagement::Commit);
    int gambles=0;
    for(int window=0;window<1000;++window) gambles+=CombatPolicy::TakeCloseFight(window*CombatPolicy::CloseFightWindowFrames);
    assert(gambles>=80 && gambles<=220); // A low share of time windows.
    assert(CombatPolicy::TakeCloseFight(0)==CombatPolicy::TakeCloseFight(CombatPolicy::CloseFightWindowFrames-1)); // Stable per window.
    assert(CombatPolicy::AttackWinnable(40,0,false) && CombatPolicy::AttackWinnable(40,30,false));
    assert(!CombatPolicy::AttackWinnable(40,35,false) && CombatPolicy::AttackWinnable(40,35,true));
    assert(!CombatPolicy::AttackWinnable(40,50,true));
    assert(CombatPolicy::AttackLost(20,40) && !CombatPolicy::AttackLost(40,40));
    assert(!CombatPolicy::ShouldStepBack(Engagement::Commit,true,10,100,100)); // Winning groups keep shooting.
    assert(CombatPolicy::ShouldStepBack(Engagement::HitAndRun,true,10,100,100)); // Ranged units kite between shots.
    assert(!CombatPolicy::ShouldStepBack(Engagement::HitAndRun,true,0,100,100)); // ...and return to fire when ready.
    assert(!CombatPolicy::ShouldStepBack(Engagement::HitAndRun,false,10,100,100)); // Healthy melee stays in.
    assert(CombatPolicy::ShouldStepBack(Engagement::HitAndRun,false,10,30,100)); // Hurt melee rotates out.
    assert(CombatPolicy::ShouldStepBack(Engagement::Withdraw,false,0,100,100));
    assert(CombatPolicy::FocusScore(0,200,128,1.0,2) < CombatPolicy::FocusScore(0,150,128,1.0,0)); // Join allies' target.
    assert(CombatPolicy::FocusScore(0,100,128,0.3,0) < CombatPolicy::FocusScore(0,100,128,1.0,0)); // Finish weak units.
    assert(CombatPolicy::FocusScore(0,128,128,1.0,0) < CombatPolicy::FocusScore(1,64,128,0.2,0)); // Threats before workers.
    FakeUnit hydra{{6,1,false,false,true,{128}},&game.player,0}, zealot{{7,2},&enemy,100};
    hydra.neighbors={&hydra,&zealot}; hydra.cooldown=10;
    zealot.type.supply=3; // 2 vs 3 power: a losing fight.
    Micro::moved={-999};
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::moved.x==-400 && Micro::fellBackFrom==nullptr); // Withdraw from losing fights.
    // An even fight is declined, except in a gamble window where the ranged unit kites between shots.
    zealot.type.supply=2;
    int gambleFrame=0, safeFrame=0;
    while(!CombatPolicy::TakeCloseFight(gambleFrame)) gambleFrame+=CombatPolicy::CloseFightWindowFrames;
    while(CombatPolicy::TakeCloseFight(safeFrame)) safeFrame+=CombatPolicy::CloseFightWindowFrames;
    game.frame=safeFrame; Micro::moved={-999};
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::moved.x==-400 && Micro::fellBackFrom==nullptr);
    game.frame=gambleFrame;
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::fellBackFrom==&zealot);
    hydra.cooldown=0; Micro::attacked=nullptr;
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::attacked==&zealot);
    game.frame=100;
    // With Guardians available, targets under static defense are left to them and the army follows the siege.
    Micro::covered={&zealot}; Micro::attacked=nullptr; siegeEscort={700}; hydra.idle=true; game.frame+=1;
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::attacked==nullptr && hydra.attackedAt.x==700);
    Micro::covered.clear(); siegeEscort={-999};
    std::cout << "Ground combat and Lurker regressions passed.\n";
}
"""
with tempfile.TemporaryDirectory(prefix='ikkrius-micro-') as temporary:
    directory=Path(temporary); (directory/'micro.cpp').write_text(source)
    subprocess.run(['cl','/nologo','/EHsc','/std:c++20','/I'+str(ROOT/'src/starterbot'),'micro.cpp','/Fe:micro.exe'],cwd=directory,check=True)
    subprocess.run([str(directory/'micro.exe')],check=True)
