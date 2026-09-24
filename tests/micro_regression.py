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
using Unitset=std::vector<Unit>;
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
    void attack(Position) { ++attacks; }
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
void GroundArmyLoop(BWAPI::Unit,const BWAPI::Unitset&,BWAPI::Position,BWAPI::Position);
}
"""
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
    std::cout << "Ground combat and Lurker regressions passed.\n";
}
"""
with tempfile.TemporaryDirectory(prefix='ikkrius-micro-') as temporary:
    directory=Path(temporary); (directory/'micro.cpp').write_text(source)
    subprocess.run(['cl','/nologo','/EHsc','/std:c++20','micro.cpp','/Fe:micro.exe'],cwd=directory,check=True)
    subprocess.run([str(directory/'micro.exe')],check=True)
