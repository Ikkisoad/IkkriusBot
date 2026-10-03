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
bool IsPayoffTarget(BWAPI::Unit enemy) { return enemy->type.worker; }
BWAPI::Unit fellBackFrom=nullptr;
std::vector<BWAPI::Unit> covered; // Enemies sitting under static defense left to Guardians.
bool AvoidsStaticDefense(BWAPI::Unit,BWAPI::Unit enemy) { return std::find(covered.begin(),covered.end(),enemy)!=covered.end(); }
void FallBack(BWAPI::Unit,BWAPI::Unit threat,BWAPI::Position) { fellBackFrom=threat; }
void GroundArmyLoop(BWAPI::Unit,const BWAPI::Unitset&,BWAPI::Position,BWAPI::Position);
bool nydusTaken=false;
bool UseNydus(BWAPI::Unit,BWAPI::Position) { return nydusTaken; }
}
"""
source+='BWAPI::Position siegeEscort{-999}, groundObjective{-999};\n'
source+='BWAPI::Position ControlPoint(BWAPI::Unit, BWAPI::Position rally) { return rally; }\n'
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
    // Group engagement policy: commit to winnable fights, trade units only for economy or tech, withdraw otherwise.
    using CombatPolicy::Engagement;
    assert(CombatPolicy::AssessEngagement(13,10)==Engagement::Commit);
    assert(CombatPolicy::AssessEngagement(12,10)==Engagement::Withdraw); // Close with nothing to gain: declined.
    assert(CombatPolicy::AssessEngagement(10,12)==Engagement::Withdraw);
    assert(CombatPolicy::AssessEngagement(12,10,true)==Engagement::HitAndRun); // Workers or tech in reach: worth it.
    assert(CombatPolicy::AssessEngagement(8,10,true)==Engagement::HitAndRun); // ...even at a loss.
    assert(CombatPolicy::AssessEngagement(7,10,true)==Engagement::Withdraw); // A rout is never worth it.
    assert(CombatPolicy::AssessEngagement(4,0)==Engagement::Commit);
    // The army only moves out with a clear edge; an even trade is a waste of units.
    assert(CombatPolicy::AttackWinnable(40,0) && CombatPolicy::AttackWinnable(40,30));
    assert(!CombatPolicy::AttackWinnable(40,35) && !CombatPolicy::AttackWinnable(40,50));
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
    // An even fight is declined, unless it reaches the enemy economy: then the ranged unit kites between shots.
    zealot.type.supply=2;
    Micro::moved={-999};
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::moved.x==-400 && Micro::fellBackFrom==nullptr);
    FakeUnit probe{{9,1,true},&enemy,150};
    hydra.neighbors={&hydra,&zealot,&probe};
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::fellBackFrom==&zealot);
    hydra.cooldown=0; Micro::attacked=nullptr;
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::attacked==&zealot);
    // Losing badly is not worth it even with workers in reach.
    zealot.type.supply=3; Micro::moved={-999}; Micro::fellBackFrom=nullptr;
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::moved.x==-400 && Micro::fellBackFrom==nullptr);
    hydra.neighbors={&hydra,&zealot}; zealot.type.supply=2;
    game.frame=100;
    // With Guardians available, targets under static defense are left to them and the army follows the siege.
    zealot.health=50; // A fight the group clearly wins.
    Micro::covered={&zealot}; Micro::attacked=nullptr; siegeEscort={700}; hydra.idle=true; game.frame+=1;
    Micro::GroundArmyLoop(&hydra,{}, {-400},{0});
    assert(Micro::attacked==nullptr && hydra.attackedAt.x==700);
    Micro::covered.clear(); siegeEscort={-999};
    // The army marches on the target; only units ahead of the main body wait for it (no collapse onto a midpoint).
    FakeUnit runner{{8,1},&game.player,1500}; runner.idle=true; runner.neighbors={&runner};
    game.frame+=1; Micro::GroundArmyLoop(&runner,{}, {-400},{0});
    assert(runner.attackedAt.x==0); // Far ahead of the body: wait for it.
    runner.x=-500; game.frame+=1; Micro::GroundArmyLoop(&runner,{}, {-400},{0});
    assert(runner.attackedAt.x==2000); // Behind the body: keep marching instead of walking to the midpoint.
    groundObjective={1200}; game.frame+=1; Micro::GroundArmyLoop(&runner,{}, {-400},{0});
    assert(runner.attackedAt.x==1200); // Nearest known enemy base first.
    const int attacksBefore=runner.attacks; Micro::nydusTaken=true; game.frame+=1;
    Micro::GroundArmyLoop(&runner,{}, {-400},{0});
    assert(runner.attacks==attacksBefore); // Riding the Nydus replaces the walk.
    Micro::nydusTaken=false; groundObjective={-999};
    struct P { int x=0, y=0; };
    const std::vector<P> army={{0,0},{2000,0},{2040,0},{2010,30},{1990,-20},{1000,0}};
    const P body=CombatPolicy::MainBody(army,384,P{});
    assert(body.x>1900 && body.x<2100); // Densest cluster, not the mean (about 1507).
    assert(CombatPolicy::MainBody(std::vector<P>{},384,P{7,7}).x==7);
    assert(CombatPolicy::WaitForMainBody(500,2000,1500) && !CombatPolicy::WaitForMainBody(2500,2000,500));
    assert(!CombatPolicy::WaitForMainBody(1900,2000,200)); // Near the body: no waiting.
    // Attack timing: go on a clear edge over the scouted army, and waiting lowers the required size.
    assert(CombatPolicy::AttackOnAdvantage(40,20,40,true)); // 20 supply vs 10: go before the 40-supply plan.
    assert(!CombatPolicy::AttackOnAdvantage(40,30,40,true));
    assert(!CombatPolicy::AttackOnAdvantage(16,4,40,true)); // Too small to be an army.
    assert(!CombatPolicy::AttackOnAdvantage(48,0,40,false) && CombatPolicy::AttackOnAdvantage(48,0,40,true));
    assert(CombatPolicy::PatientAttackSupply(60,0)==60 && CombatPolicy::PatientAttackSupply(60,120)==48);
    assert(CombatPolicy::PatientAttackSupply(60,3600)==30 && CombatPolicy::PatientAttackSupply(20,3600)==12);
    // Gas never starves minerals.
    assert(CombatPolicy::GasWorkerBudget(20,2,500,100)==6);
    assert(CombatPolicy::GasWorkerBudget(9,3,500,100)==1);
    assert(CombatPolicy::GasWorkerBudget(12,4,500,100)==4);
    assert(CombatPolicy::GasWorkerBudget(30,4,100,700)==4);
    assert(CombatPolicy::GasWorkerBudget(40,4,100,1200)==0);
    // Scourge only against air our anti-air cannot cover, in pairs.
    assert(CombatPolicy::ScourgeTarget(0,0,0)==0);
    assert(CombatPolicy::ScourgeTarget(1000,12,0)==10);
    assert(CombatPolicy::ScourgeTarget(1000,12,18)==0);
    assert(CombatPolicy::ScourgeTarget(100000,200,0)==24);
    assert(CombatPolicy::ScourgeNeeded(500)==5 && CombatPolicy::ScourgeNeeded(1)==1);
    // Defilers only very late with a real ground army.
    assert(CombatPolicy::DefilerTarget(15,380,200)==0 && CombatPolicy::DefilerTarget(22,250,200)==0);
    assert(CombatPolicy::DefilerTarget(22,320,120)==3 && CombatPolicy::DefilerTarget(30,200,400)==4);
    assert(CombatPolicy::DefilerTarget(30,300,20)==0);
    // Broodling fishing: valuable, close, lightly guarded.
    assert(CombatPolicy::BroodlingHuntScore(850,800,0)>0);
    assert(CombatPolicy::BroodlingHuntScore(150,100,0)==0 && CombatPolicy::BroodlingHuntScore(850,800,4)==0);
    assert(CombatPolicy::BroodlingHuntScore(850,800,0)>CombatPolicy::BroodlingHuntScore(850,800,2));
    assert(CombatPolicy::BroodlingHuntScore(850,CombatPolicy::BroodlingHuntRange+1,0)==0);
    // Nydus for ground-heavy compositions, taken only when it saves real distance.
    assert(CombatPolicy::WantsNydus(0.5,false,2) && !CombatPolicy::WantsNydus(0.4,false,3) && !CombatPolicy::WantsNydus(1.0,true,2));
    assert(CombatPolicy::NydusShortcut(100,300,3000) && !CombatPolicy::NydusShortcut(100,2800,3000) && !CombatPolicy::NydusShortcut(900,0,5000));
    // Drones join a defense the army cannot hold, only as many as it takes, and not into a hopeless fight.
    assert(CombatPolicy::DronesToDefend(0,0,10)==0 && CombatPolicy::DronesToDefend(10,20,10)==0);
    assert(CombatPolicy::DronesToDefend(12,0,12)==12); // Six Zerglings on an empty base: every Drone.
    assert(CombatPolicy::DronesToDefend(12,8,12)==8);  // Our Zerglings hold part of it.
    assert(CombatPolicy::DronesToDefend(1,0,12)==2);   // A lone attacking worker: a couple of Drones.
    assert(CombatPolicy::DronesToDefend(60,0,20)==0);  // A whole army: Drones cannot change the result.
    assert(CombatPolicy::DronesToDefend(12,4,0)==0);
    // Zerglings are grouped into stable, bounded squads, and re-plan in turns while out of contact.
    std::vector<P> lings;
    for(int i=0;i<30;++i) lings.push_back({i*4,0});
    lings.push_back({3000,0});
    const auto squads=CombatPolicy::Squads(lings,CombatPolicy::ZerglingSquadRadius,CombatPolicy::ZerglingSquadSize);
    assert(squads.size()==3 && squads[0].size()==24 && squads[1].size()==6 && squads[2].size()==1);
    assert(squads[0][0]==0 && squads[1][0]==24 && squads[2][0]==30);
    assert(CombatPolicy::Squads(std::vector<P>{},256,24).empty());
    int due=0;
    for(int f=0;f<CombatPolicy::SquadIdleOrderInterval*10;++f) due+=CombatPolicy::SquadOrdersDue(f,7);
    assert(due==10);
    // Static defense is only entered by an attack able to kill it, and a started assault is held while close.
    assert(!CombatPolicy::AssaultStaticDefense(false,false,100,12)); // Not attacking: never walk into it.
    assert(!CombatPolicy::AssaultStaticDefense(true,true,100,12));   // Guardians handle it.
    assert(!CombatPolicy::AssaultStaticDefense(true,false,12,12) && CombatPolicy::AssaultStaticDefense(true,false,16,12));
    assert(CombatPolicy::KeepAssault(11,12) && !CombatPolicy::KeepAssault(10,12));
    // Queens stay with the army and step up to cast range when a spell is ready.
    assert(CombatPolicy::QueenTrail(75,true)==0 && CombatPolicy::QueenTrail(75,false)==64 && CombatPolicy::QueenTrail(20,true)==160);
    // Infested Terrans only blow up on something worth it, never on our own army.
    assert(CombatPolicy::InfestedTerranScore(1,false,false,0)==0 && CombatPolicy::InfestedTerranScore(6,false,false,0)==6);
    assert(CombatPolicy::InfestedTerranScore(0,true,false,0)==8 && CombatPolicy::InfestedTerranScore(2,false,true,0)==10);
    assert(CombatPolicy::InfestedTerranScore(6,false,false,1)==0);
    // Openers: macro compositions may go hatch first; Hive-bound ones take an early third.
    assert(CombatPolicy::HatchFirst(false,0.75) && !CombatPolicy::HatchFirst(true,0.75) && !CombatPolicy::HatchFirst(false,0.2));
    assert(CombatPolicy::EarlyThird(true,false,2,24,24) && !CombatPolicy::EarlyThird(false,false,2,30,24));
    assert(!CombatPolicy::EarlyThird(true,true,2,30,24) && !CombatPolicy::EarlyThird(true,false,3,30,24) && !CombatPolicy::EarlyThird(true,false,2,23,24));
    std::cout << "Ground combat and Lurker regressions passed.\n";
}
"""
with tempfile.TemporaryDirectory(prefix='ikkrius-micro-') as temporary:
    directory=Path(temporary); (directory/'micro.cpp').write_text(source)
    subprocess.run(['cl','/nologo','/EHsc','/std:c++20','/I'+str(ROOT/'src/starterbot'),'micro.cpp','/Fe:micro.exe'],cwd=directory,check=True)
    subprocess.run([str(directory/'micro.exe')],check=True)
