"""Exercise real Queen spells, Hydra grouping, Devourer orders and air composition."""
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
#include <set>
#include <string>
#include <vector>
#include "CombatPolicy.h"
namespace BWAPI {
struct Position {
    int x=0, y=0;
    int getApproxDistance(Position other) const { return int(std::hypot(x-other.x,y-other.y)); }
    bool isValid() const { return true; }
};
struct TechType {
    int id=0;
    int energyCost() const { return id==1 ? 150 : 75; }
    bool operator!=(TechType other) const { return id!=other.id; }
    bool operator==(TechType other) const { return id==other.id; }
};
namespace TechTypes { const TechType Spawn_Broodlings{1}, Ensnare{2}, Parasite{3}; }
struct UnitType {
    int id=0, minerals=50, gas=0, supply=2;
    bool building=false, attack=true;
    bool operator==(UnitType other) const { return id==other.id; }
    bool operator!=(UnitType other) const { return id!=other.id; }
    bool isBuilding() const { return building; }
    bool canAttack() const { return attack; }
    int mineralPrice() const { return minerals; }
    int gasPrice() const { return gas; }
    int supplyRequired() const { return supply; }
};
namespace UnitTypes {
const UnitType Zerg_Queen{1,100,100,4,false,false}, Zerg_Hydralisk{2,75,25,2},
Terran_Siege_Tank_Siege_Mode{3,150,100,4}, Terran_Siege_Tank_Tank_Mode{4,150,100,4},
Protoss_High_Templar{5,50,150,4}, Zerg_Defiler{6,50,150,4};
}
namespace UnitCommandTypes { enum { None, Use_Tech_Unit, Use_Tech_Position }; }
namespace Filter { const int IsEnemy=1; }
struct FakeUnit;
using Unit=FakeUnit*;
using Unitset=std::set<Unit>;
struct Player {
    bool hostile=false, researched=true;
    Unitset units;
    bool isEnemy(Player* other) { return other && other->hostile; }
    bool hasResearched(TechType) { return researched; }
    Unitset getUnits() { return units; }
};
struct Command {
    int kind=0;
    TechType tech;
    Unit target=nullptr;
    Position position;
    int getType() const { return kind; }
    TechType getTechType() const { return tech; }
    Unit getTarget() const { return target; }
    Position getTargetPosition() const { return position; }
};
int frame=100;
struct FakeUnit {
    UnitType type;
    Player* player=nullptr;
    int id=0;
    Position position;
    bool alive=true, visible=true, detected=true, flying=false, complete=true, morphing=false;
    bool ensnared=false, parasited=false, broodable=true, interruptible=true, canAttackAir=true;
    int energy=200, lastFrame=-100, spellCooldown=0, casts=0;
    Command command;
    Unitset neighbors;
    bool exists() { return alive; } bool isVisible() { return visible; }
    bool isDetected() { return detected; } bool isFlying() { return flying; }
    bool isCompleted() { return complete; } bool isMorphing() { return morphing; }
    bool isInterruptible() { return interruptible; }
    bool isEnsnared() { return ensnared; } bool isParasited() { return parasited; }
    int getID() { return id; } int getEnergy() { return energy; }
    int getLastCommandFrame() { return lastFrame; } int getSpellCooldown() { return spellCooldown; }
    Command getLastCommand() { return command; }
    UnitType getType() { return type; } Player* getPlayer() { return player; }
    Position getPosition() { return position; }
    int getDistance(Unit other) { return position.getApproxDistance(other->position); }
    Unitset getUnitsInRadius(int radius,int) { Unitset result; for(auto u:neighbors) if(u->player->hostile && getDistance(u)<=radius) result.insert(u); return result; }
    bool canAttack(Unit target) { return target->flying && canAttackAir; }
    bool canUseTech(TechType tech,Unit target) { return energy>=tech.energyCost() && (tech.id!=1 || target->broodable); }
    bool canUseTech(TechType tech,Position) { return energy>=tech.energyCost(); }
    bool useTech(TechType tech,Unit target) { ++casts; energy-=tech.energyCost(); lastFrame=frame; command={UnitCommandTypes::Use_Tech_Unit,tech,target,target->position}; return true; }
    bool useTech(TechType tech,Position target) { ++casts; energy-=tech.energyCost(); lastFrame=frame; command={UnitCommandTypes::Use_Tech_Position,tech,nullptr,target}; return true; }
};
struct Game {
    Player player;
    Player* self() { return &player; }
    int getFrameCount() { return frame; } int getLatencyFrames() { return 3; }
} game;
Game* Broodwar=&game;
}
namespace MatchLog { void Event(const std::string&,const std::string&) {} }
namespace Micro {
bool SpellReserved(BWAPI::TechType,BWAPI::Unit,BWAPI::Position);
bool QueenCastLoop(BWAPI::Unit,BWAPI::Unitset);
std::vector<BWAPI::Unitset> GetHydraGroups(const BWAPI::Unitset&);
void DevourerEscortLoop(BWAPI::Unit,BWAPI::Position);
BWAPI::Unit attacked=nullptr;
int moves=0;
void SmartAttackUnit(BWAPI::Unit,BWAPI::Unit target) { attacked=target; }
void SmartMove(BWAPI::Unit,BWAPI::Position) { ++moves; }
}
"""
for signature in ['std::vector<BWAPI::Unitset> Micro::GetHydraGroups', 'bool Micro::SpellReserved',
                  'bool Micro::QueenCastLoop', 'void Micro::DevourerEscortLoop']:
    source+=function(signature)
source+=r"""
int main() {
    using namespace BWAPI;
    Player enemy, ally; enemy.hostile=true;
    FakeUnit queen{UnitTypes::Zerg_Queen,&game.player,1};
    FakeUnit queen2{UnitTypes::Zerg_Queen,&game.player,2};
    game.player.units={&queen,&queen2};
    FakeUnit tank{UnitTypes::Terran_Siege_Tank_Siege_Mode,&enemy,10,{100,0}};
    FakeUnit friendlyTank{UnitTypes::Terran_Siege_Tank_Siege_Mode,&ally,11,{50,0}};
    assert(Micro::QueenCastLoop(&queen,{&tank,&friendlyTank}));
    assert(queen.command.tech==TechTypes::Spawn_Broodlings && queen.command.target==&tank);
    ++frame;
    assert(Micro::QueenCastLoop(&queen,{&tank}));
    assert(queen.casts==1); // Report the in-flight cast as handled, so escort movement cannot cancel it.
    queen2.energy=150;
    assert(!Micro::QueenCastLoop(&queen2,{&tank}));
    assert(queen2.casts==0); // Do not spend two Queens on one pending Broodlings target.
    frame+=40; tank.broodable=false;
    assert(!Micro::QueenCastLoop(&queen2,{&tank})); // BWAPI rejects invalid targets.

    FakeUnit marine1{{20},&enemy,20,{100,0}}, marine2{{20},&enemy,21,{120,0}}, marine3{{20},&enemy,22,{140,0}};
    queen.energy=75; queen.lastFrame=-100; queen.command={}; queen.casts=0;
    queen2.energy=75; queen2.lastFrame=-100; queen2.command={}; queen2.casts=0;
    assert(!Micro::QueenCastLoop(&queen,{&marine1,&marine2})); // Save energy for a real cluster.
    assert(Micro::QueenCastLoop(&queen,{&marine1,&marine2,&marine3}));
    assert(queen.command.tech==TechTypes::Ensnare && queen.casts==1);
    assert(!Micro::QueenCastLoop(&queen2,{&marine1,&marine2,&marine3}));
    assert(queen2.casts==0); // No duplicate area casts before the first spell lands.
    frame+=40; marine1.ensnared=marine2.ensnared=marine3.ensnared=true;
    assert(!Micro::QueenCastLoop(&queen2,{&marine1,&marine2,&marine3}));
    queen2.energy=74; marine1.ensnared=marine2.ensnared=marine3.ensnared=false;
    assert(!Micro::QueenCastLoop(&queen2,{&marine1,&marine2,&marine3}));

    std::vector<FakeUnit> hydras(25);
    Unitset units;
    for(int i=0;i<25;++i) { hydras[i].type=UnitTypes::Zerg_Hydralisk; hydras[i].id=100+i; hydras[i].position={i*8,0}; units.insert(&hydras[i]); }
    auto groups=Micro::GetHydraGroups(units);
    assert(groups.size()==3 && groups[0].size()==12 && groups[1].size()==12 && groups[2].size()==1);
    assert(CombatPolicy::QueenTarget(int(groups.size()),0)==3);
    hydras.back().morphing=true;
    assert(Micro::GetHydraGroups(units).size()==2); // Morphing Lurkers are no longer Hydra-group members.
    hydras.back().morphing=false; hydras.back().position={2000,0};
    assert(Micro::GetHydraGroups(units).size()==3); // A detached reinforcement group also requests support.
    assert(CombatPolicy::QueenTarget(3,17)==5);

    FakeUnit devourer{{30},&game.player,30};
    FakeUnit air{{31},&enemy,31,{100,0}}; air.flying=true;
    devourer.neighbors={&air,&marine1};
    Micro::DevourerEscortLoop(&devourer,{200,0});
    assert(Micro::attacked==&air && Micro::moves==0); // Attack before issuing an escort move.
    devourer.neighbors={&marine1}; Micro::attacked=nullptr;
    Micro::DevourerEscortLoop(&devourer,{200,0});
    assert(Micro::attacked==nullptr && Micro::moves==1);
    FakeUnit farAir{{31},&enemy,32,{-300,0}}; farAir.flying=true;
    devourer.neighbors={&farAir};
    Micro::DevourerEscortLoop(&devourer,{200,0});
    assert(Micro::attacked==nullptr && Micro::moves==2); // Never peel off from the Mutalisks alone.

    using CombatPolicy::AirMorph;
    assert(CombatPolicy::NextAirMorph(8,0,0,32)==AirMorph::None);
    assert(CombatPolicy::NextAirMorph(12,0,0,0)==AirMorph::Guardian);
    assert(CombatPolicy::NextAirMorph(12,0,0,16)==AirMorph::Devourer);
    assert(CombatPolicy::NextAirMorph(12,12,0,0)==AirMorph::Devourer);
    assert(CombatPolicy::NextAirMorph(12,12,1,0)==AirMorph::Devourer); // One Devourer per five Mutalisks.
    assert(CombatPolicy::NextAirMorph(12,12,2,0)==AirMorph::None);
    assert(CombatPolicy::DevourerTarget(10)==2 && CombatPolicy::DevourerTarget(4)==0);
    assert(!CombatPolicy::WantDevourer(5,0) && CombatPolicy::WantDevourer(6,0)); // Five Mutalisks must remain.
    assert(!CombatPolicy::WantDevourer(10,1) && CombatPolicy::WantDevourer(11,1));
    assert(CombatPolicy::NextAirMorph(20,12,6,100)==AirMorph::None);
    std::cout << "Queen spells, Hydra groups, Devourer orders and air composition passed.\n";
}
"""
with tempfile.TemporaryDirectory(prefix='ikkrius-queen-air-') as temporary:
    directory=Path(temporary); (directory/'queen_air.cpp').write_text(source)
    subprocess.run(['cl','/nologo','/EHsc','/std:c++20','/I'+str(ROOT/'src/starterbot'), 'queen_air.cpp','/Fe:queen_air.exe'],cwd=directory,check=True)
    subprocess.run([str(directory/'queen_air.exe')],check=True)
