"""Exercise Guardian siege spacing, the kiting range rule and Mutalisk worker raids without the game."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'src/starterbot/micro.cpp').read_text()
def function(signature):
    start=text.index(signature); end=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]
def between(first, last):
    return text[text.index(first):text.index(last)]

source=r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "CombatPolicy.h"
namespace BWAPI {
struct Position {
    int x=0, y=0;
    Position() {}
    Position(int x_, int y_) : x(x_), y(y_) {}
    bool isValid() const { return x!=-999; }
    int getApproxDistance(Position other) const { return int(std::hypot(x-other.x,y-other.y)); }
    Position& makeValid() { x=std::clamp(x,0,4095); y=std::clamp(y,0,4095); return *this; }
    bool operator==(Position other) const { return x==other.x && y==other.y; }
    bool operator!=(Position other) const { return !(*this==other); }
};
namespace Positions { const Position None{-999,-999}; }
struct TilePosition { Position position; explicit TilePosition(Position p) : position(p) {} };
struct WeaponType {
    int id=0, range=0;
    bool operator==(WeaponType other) const { return id==other.id; }
    bool operator!=(WeaponType other) const { return id!=other.id; }
};
namespace WeaponTypes { const WeaponType None{0,0}, Gauss_Rifle{1,128}, Glave_Wurm{2,96}, Acid_Spore{3,256},
    Halo_Rockets{4,192}, Long_Missiles{5,160}, Suicide{6,0}; }
struct UnitType {
    int id=0, supply=2, hp=100;
    bool building=false, worker=false, depot=false;
    WeaponType ground{}, air{};
    bool isBuilding() const { return building; }
    bool isWorker() const { return worker; }
    bool isResourceDepot() const { return depot; }
    int supplyRequired() const { return supply; }
    int maxHitPoints() const { return hp; }
    int maxShields() const { return 0; }
    WeaponType groundWeapon() const { return ground; }
    WeaponType airWeapon() const { return air; }
    bool operator==(UnitType other) const { return id==other.id; }
    bool operator!=(UnitType other) const { return id!=other.id; }
};
namespace UnitTypes {
const UnitType Terran_Bunker{1,0,350,true}, Protoss_Carrier{2,12,300},
    Zerg_Mutalisk{3,4,120,false,false,false,WeaponTypes::Glave_Wurm,WeaponTypes::Glave_Wurm},
    Zerg_Guardian{4,4,150,false,false,false,WeaponTypes::Acid_Spore},
    Terran_SCV{5,2,60,false,true}, Terran_Marine{6,2,40,false,false,false,WeaponTypes::Gauss_Rifle,WeaponTypes::Gauss_Rifle},
    Terran_Missile_Turret{7,0,200,true,false,false,WeaponTypes::None,WeaponTypes::Long_Missiles},
    Terran_Goliath{8,4,125,false,false,false,WeaponTypes::None,WeaponTypes::Halo_Rockets},
    Zerg_Scourge{9,1,25,false,false,false,WeaponTypes::None,WeaponTypes::Suicide},
    Terran_Command_Center{10,0,1500,true,false,true};
}
namespace Filter { const int IsEnemy=1; }
struct FakeUnit;
using Unit=FakeUnit*;
using Unitset=std::set<Unit>;
struct Player {
    bool hostile=false; int airBonus=0;
    bool isEnemy(Player* other) { return other && other->hostile; }
    int weaponMaxRange(WeaponType weapon) { return weapon.range + (weapon==WeaponTypes::Halo_Rockets ? airBonus : 0); }
};
std::vector<Unit> world;
struct FakeUnit {
    UnitType type; Player* player=nullptr; Position position; int id=0;
    int hp=-1, airCooldown=0, groundCooldown=0;
    bool alive=true, visible=true, complete=true, detected=true, flying=false;
    UnitType getType() { return type; }
    Player* getPlayer() { return player; }
    Position getPosition() { return position; }
    int getID() { return id; }
    bool exists() { return alive; } bool isVisible() { return visible; } bool isCompleted() { return complete; }
    bool isDetected() { return detected; } bool isFlying() { return flying; }
    int getHitPoints() { return hp<0 ? type.hp : hp; } int getShields() { return 0; }
    int getAirWeaponCooldown() { return airCooldown; } int getGroundWeaponCooldown() { return groundCooldown; }
    int getDistance(Unit other) { return position.getApproxDistance(other->position); }
    int getDistance(Position other) { return position.getApproxDistance(other); }
    bool canAttack(Unit other) { return other->flying ? type.air!=WeaponTypes::None : type.ground!=WeaponTypes::None; }
    Unitset getUnitsInRadius(int radius, int filter=0) {
        Unitset result;
        for (auto u : world) if (u!=this && u->alive && getDistance(u)<=radius && (!filter || u->player->hostile)) result.insert(u);
        return result;
    }
};
struct Game {
    Player player; int frame=100;
    Player* self() { return &player; }
    int getFrameCount() { return frame; }
    void drawTextMap(Position, const char*) {}
    Unitset getAllUnits() { return Unitset(world.begin(), world.end()); }
    Unit getUnit(int id) { for (auto u : world) if (u->id==id) return u; return nullptr; }
    bool isVisible(TilePosition) { return true; }
    Unitset getUnitsInRadius(Position center, int radius, int filter=0) {
        Unitset result;
        for (auto u : world) if (u->alive && u->position.getApproxDistance(center)<=radius && (!filter || u->player->hostile)) result.insert(u);
        return result;
    }
} game;
Game* Broodwar=&game;
}
std::vector<std::string> events;
namespace MatchLog { void Event(const std::string& kind, const std::string&) { events.push_back(kind); } }
namespace BasesTools {
BWAPI::Position enemyMain{3000,3000};
BWAPI::Position GetEnemyBasePosition() { return enemyMain; }
BWAPI::Position GetMainBasePosition() { return {100,100}; }
}
namespace Micro {
enum class MicroMode { Neutral, Aggressive, Defensive };
MicroMode mode=MicroMode::Defensive;
MicroMode GetMode() { return mode; }
BWAPI::Unit attacked=nullptr;
BWAPI::Position moved=BWAPI::Positions::None;
void Reset() { attacked=nullptr; moved=BWAPI::Positions::None; }
void SmartAttackUnit(BWAPI::Unit, BWAPI::Unit target) { attacked=target; }
void SmartMove(BWAPI::Unit, BWAPI::Position position) { moved=position; }
void ScoutAndWander(BWAPI::Unit) {}
void MutaliskRaidLoop(BWAPI::Unit, BWAPI::Position, BWAPI::Position, BWAPI::Position, bool);
void GuardianAssaultLoop(BWAPI::Unit, BWAPI::Unitset, BWAPI::Position);
void ResetCombatState();
}
namespace {
"""
for signature in ('BWAPI::Position UnitCenter(', 'int AirThreatRange(', 'bool IsStaticAntiAir(',
                  'int ThreatMargin(', 'BWAPI::Position AwayFrom('):
    source+=function(signature)+'\n'
source+=between('// Mutalisk raid squad, kept stable across frames.', 'bool HarassAvoided(')
for signature in ('bool HarassAvoided(', 'void EndRaid(', 'RaidOrders UpdateRaidSquad('):
    source+=function(signature)+'\n'
source+='}\n'
source+='std::map<int,int> lurkerLastContact;\n'+function('void Micro::ResetCombatState')+'\n'
source+=function('void Micro::MutaliskRaidLoop')+'\n'
source+=function('void Micro::GuardianAssaultLoop')+'\n'
source+=r"""
int main() {
    using namespace BWAPI;
    using namespace CombatPolicy;
    using Micro::ResetCombatState;
    // Hit-and-run only against shorter range.
    assert(KiteWorthwhile(256,224) && !KiteWorthwhile(96,128) && !KiteWorthwhile(256,256));
    assert(HarassSquadSize(2,false)==0 && HarassSquadSize(5,false)==5 && HarassSquadSize(10,false)==6 && HarassSquadSize(10,true)==0);
    assert(HarassAbort(4,6) && !HarassAbort(12,6));
    assert(HarassNeedsRegen(40,120,false) && !HarassNeedsRegen(50,120,false));
    assert(HarassNeedsRegen(100,120,true) && !HarassNeedsRegen(110,120,true));

    Player enemy; enemy.hostile=true;
    Player& self=game.player;
    int nextId=1;
    auto make=[&](UnitType type, Player* owner, int x, int y) { auto u=new FakeUnit{type,owner,{x,y},nextId++}; world.push_back(u); return u; };

    // Guardian: fire from max range, then step out of anti-air reach while reloading.
    auto guardian=make(UnitTypes::Zerg_Guardian,&self,1000,1000); guardian->flying=true;
    auto marine=make(UnitTypes::Terran_Marine,&enemy,1150,1000);
    auto scv=make(UnitTypes::Terran_SCV,&enemy,1100,1000);
    Micro::Reset(); Micro::GuardianAssaultLoop(guardian,{}, {500,1000});
    assert(Micro::attacked==marine); // Anti-air first, even over a closer worker.
    guardian->groundCooldown=20;
    Micro::Reset(); Micro::GuardianAssaultLoop(guardian,{}, {500,1000});
    assert(Micro::attacked==nullptr && Micro::moved.x<1000); // Reposition away while on cooldown.
    marine->position={1300,1000};
    Micro::Reset(); Micro::GuardianAssaultLoop(guardian,{}, {500,1000});
    assert(Micro::attacked==nullptr && !Micro::moved.isValid()); // Safe: hold and let the shot finish.
    guardian->groundCooldown=0;
    enemy.airBonus=96; // Charon Boosters: Goliaths match Guardian range.
    auto goliath=make(UnitTypes::Terran_Goliath,&enemy,1200,1000);
    Micro::Reset(); Micro::GuardianAssaultLoop(guardian,{}, {500,1000});
    assert(Micro::moved==Position(500,1000) && Micro::attacked==nullptr); // Never hit-and-run an equal-range unit.
    goliath->alive=false; marine->alive=false; scv->alive=false; guardian->alive=false;

    // Mutalisks: prefer workers, never kite a longer-ranged unit, but do kite shorter-ranged ones.
    auto muta=make(UnitTypes::Zerg_Mutalisk,&self,2000,2000); muta->flying=true;
    auto worker=make(UnitTypes::Terran_SCV,&enemy,2050,2000);
    auto rifle=make(UnitTypes::Terran_Marine,&enemy,1900,2000);
    Micro::Reset(); Micro::MutaliskRaidLoop(muta,{2100,2000},{2000,2000},{100,100},false);
    assert(Micro::attacked==worker);
    muta->groundCooldown=15;
    Micro::Reset(); Micro::MutaliskRaidLoop(muta,{2100,2000},{2000,2000},{100,100},false);
    assert(Micro::attacked==worker && !Micro::moved.isValid()); // Outranged: commit rather than kite.
    rifle->alive=false;
    auto scourge=make(UnitTypes::Zerg_Scourge,&enemy,1950,2000); scourge->flying=true;
    Micro::Reset(); Micro::MutaliskRaidLoop(muta,{2100,2000},{2000,2000},{100,100},false);
    assert(Micro::moved.isValid() && Micro::moved.x>2000); // Shorter range: hit-and-run.
    scourge->alive=false; muta->groundCooldown=0;
    auto turret=make(UnitTypes::Terran_Missile_Turret,&enemy,2100,2100);
    auto exposed=make(UnitTypes::Terran_SCV,&enemy,1800,1800);
    Micro::Reset(); Micro::MutaliskRaidLoop(muta,{2100,2000},{2000,2000},{100,100},false);
    assert(Micro::attacked==exposed); // Skip workers under static anti-air.
    Micro::Reset(); Micro::MutaliskRaidLoop(muta,{2100,2000},{2000,2000},{100,100},true);
    assert(Micro::moved==Position(100,100)); // Retreat/regen goes home.
    for (auto u : {muta,worker,turret,exposed}) u->alive=false;

    // Squad: a small raid forms outside the main attack and aborts into heavy static defense.
    world.erase(std::remove_if(world.begin(),world.end(),[](Unit u){ return !u->alive; }),world.end());
    Unitset flock;
    for (int i=0;i<4;++i) { auto m=make(UnitTypes::Zerg_Mutalisk,&self,500+i*10,500); m->flying=true; flock.insert(m); }
    auto orders=UpdateRaidSquad(flock,{},{100,100});
    assert(orders.raiders.size()==4 && orders.target==BasesTools::enemyMain && !orders.retreat);
    Micro::mode=Micro::MicroMode::Aggressive;
    orders=UpdateRaidSquad(flock,{},{100,100});
    assert(orders.raiders.empty()); // The main attack takes the whole flock.
    Micro::mode=Micro::MicroMode::Defensive;
    orders=UpdateRaidSquad(flock,{},{100,100});
    make(UnitTypes::Terran_Missile_Turret,&enemy,560,500); make(UnitTypes::Terran_Missile_Turret,&enemy,560,560);
    orders=UpdateRaidSquad(flock,{},{100,100});
    assert(orders.retreat && std::count(events.begin(),events.end(),"harass_abort")==1);
    assert(HarassAvoided(BasesTools::enemyMain));
    ResetCombatState();
    world.erase(std::remove_if(world.begin(),world.end(),[](Unit u){ return u->type==UnitTypes::Terran_Missile_Turret; }),world.end());
    orders=UpdateRaidSquad(flock,{},{100,100});
    assert(!orders.retreat);
    int regenerating=0;
    for (auto m : flock) if (regenerating++<2) harassRegen.insert(m->getID());
    orders=UpdateRaidSquad(flock,{},{100,100});
    assert(orders.raiders.size()==4 && orders.retreat); // Two healthy raiders do not go in alone.
    std::cout << "Air micro regressions passed.\n";
}
"""

with tempfile.TemporaryDirectory(prefix='ikkrius-air-') as temporary:
    directory=Path(temporary); (directory/'air.cpp').write_text(source)
    include=str(ROOT/'src/starterbot')
    if shutil.which('cl'):
        subprocess.run(['cl','/nologo','/EHsc','/std:c++20','/I',include,'air.cpp','/Fe:air.exe'],cwd=directory,check=True)
        binary=directory/'air.exe'
    else:
        subprocess.run(['g++','-std=c++20','-I',include,'air.cpp','-o','air'],cwd=directory,check=True)
        binary=directory/'air'
    subprocess.run([str(binary)],check=True)
