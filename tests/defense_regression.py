"""Exercise the actual base-defense helpers without a StarCraft process.

Run from a VS developer prompt: python tests/defense_regression.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / 'src/starterbot/micro.cpp').read_text()
def function(signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

source = r'''
#include <cassert>
#include <set>
#include <cstdlib>
#include <iostream>
namespace BWAPI {
struct UnitType {
    int id=0;
    bool attack=false, caster=false, depot=false, worker=false, building=false;
    bool canAttack() const { return attack; }
    bool isSpellcaster() const { return caster; }
    bool isResourceDepot() const { return depot; }
    bool isWorker() const { return worker; }
    bool isBuilding() const { return building; }
    bool operator!=(UnitType other) const { return id!=other.id; }
};
namespace UnitTypes { const UnitType Terran_Bunker{1,false,false,false,false,true}; }
struct FakeUnit;
using Unit=FakeUnit*;
using Unitset=std::set<Unit>;
struct Player {
    Unitset units;
    bool hostile=false;
    bool isEnemy(Player* other) { return other->hostile; }
    Unitset getUnits() { return units; }
};
struct FakeUnit {
    UnitType type;
    Player* player=nullptr;
    int x=0;
    bool alive=true, visible=true, complete=true, morphing=false, loaded=false, flying=false;
    bool hitsGround=true, hitsAir=false;
    bool exists() { return alive; }
    bool isVisible() { return visible; }
    bool isCompleted() { return complete; }
    bool isMorphing() { return morphing; }
    bool isLoaded() { return loaded; }
    UnitType getType() { return type; }
    Player* getPlayer() { return player; }
    int getDistance(Unit other) { return std::abs(x-other->x); }
    bool canAttack(Unit other) { return type.attack && (other->flying ? hitsAir : hitsGround); }
};
struct Game {
    Player player;
    Unitset units;
    Player* self() { return &player; }
    Unitset getAllUnits() { return units; }
} game;
Game* Broodwar=&game;
}
namespace Micro {
BWAPI::Unitset GetBaseThreats();
bool DefendBases(BWAPI::Unit,const BWAPI::Unitset&);
BWAPI::Unit commanded=nullptr, target=nullptr;
void SmartAttackUnit(BWAPI::Unit unit,BWAPI::Unit enemy) { commanded=unit; target=enemy; }
}
'''.replace("\'", "'")
source += function('BWAPI::Unitset Micro::GetBaseThreats')
source += function('bool Micro::DefendBases')
source += r'''
int main() {
    using namespace BWAPI;
    Player enemy, ally; enemy.hostile=true;
    UnitType depotType{2,false,false,true,false,true};
    UnitType fighterType{3,true};
    UnitType workerType{4,true,false,false,true};
    FakeUnit main{depotType,&game.player,0}, natural{depotType,&game.player,2000};
    natural.complete=false;
    game.player.units={&main,&natural};
    FakeUnit raider{fighterType,&enemy,2200}, far{fighterType,&enemy,4000};
    FakeUnit invisible{fighterType,&enemy,2100}; invisible.visible=false;
    FakeUnit friendly{fighterType,&ally,2150};
    FakeUnit scout{{},&enemy,2000};
    FakeUnit bunker{UnitTypes::Terran_Bunker,&enemy,2250};
    game.units={&raider,&far,&invisible,&friendly,&scout,&bunker};
    auto threats=Micro::GetBaseThreats();
    assert(threats.size()==2 && threats.count(&raider) && threats.count(&bunker));
    FakeUnit defender{fighterType,&game.player,4000};
    assert(Micro::DefendBases(&defender,threats)); // Recall an army from across the map.
    assert(Micro::commanded==&defender && Micro::target==&bunker);
    FakeUnit worker{workerType,&game.player,2200};
    assert(!Micro::DefendBases(&worker,threats));
    defender.morphing=true;
    assert(!Micro::DefendBases(&defender,threats));
    defender.morphing=false;
    raider.flying=true;
    assert(!Micro::DefendBases(&defender,{&raider})); // Lings cannot defend against air.
    defender.hitsAir=true;
    assert(Micro::DefendBases(&defender,{&raider}));
    assert(Micro::target==&raider);
    assert(!Micro::DefendBases(&defender,{}));
    raider.alive=false; bunker.x=4000;
    assert(Micro::GetBaseThreats().empty()); // Resume normal orders once the raid ends.
    std::cout << "Defense regressions passed.\n";
}
'''.replace("\'", "'")
with tempfile.TemporaryDirectory(prefix='ikkrius-defense-') as directory:
    directory=Path(directory)
    (directory/'defense.cpp').write_text(source)
    subprocess.run(['cl','/nologo','/EHsc','/std:c++20','defense.cpp','/Fe:defense.exe'],
                   cwd=directory,check=True)
    subprocess.run([str(directory/'defense.exe')],check=True)
