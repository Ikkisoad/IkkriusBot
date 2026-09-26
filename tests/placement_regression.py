"""Check that building placement keeps the hatchery-to-resource mining paths clear.

Run from a VS developer prompt: python tests/placement_regression.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / 'src/starterbot/Tools.cpp').read_text()
def function(signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

source = r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
namespace BWAPI {
struct TilePosition { int x=0, y=0; };
struct UnitType {
    int width=1, height=1;
    bool depot=false, refinery=false;
    int tileWidth() const { return width; }
    int tileHeight() const { return height; }
    bool isResourceDepot() const { return depot; }
    bool isRefinery() const { return refinery; }
};
struct FakeUnit {
    UnitType type;
    TilePosition tile;
    UnitType getType() { return type; }
    UnitType getInitialType() { return type; }
    TilePosition getTilePosition() { return tile; }
    TilePosition getInitialTilePosition() { return tile; }
};
using Unit=FakeUnit*;
using Unitset=std::set<Unit>;
struct Player {
    Unitset units;
    Unitset getUnits() { return units; }
};
struct Game {
    Player player;
    Unitset minerals, geysers;
    Player* self() { return &player; }
    const Unitset& getStaticMinerals() { return minerals; }
    const Unitset& getStaticGeysers() { return geysers; }
} game;
Game* Broodwar=&game;
}
'''
source += function('static bool BlocksResourceGathering')
source += r'''
int main() {
    using namespace BWAPI;
    const UnitType hatchType{4,3,true}, mineralType{2,1}, geyserType{4,2};
    const UnitType pool{3,2}, colony{2,2}, extractor{4,2,false,true};
    // Hatchery at (20,20)-(24,23), minerals to the left, geyser above.
    FakeUnit hatch{hatchType,{20,20}};
    FakeUnit mineralA{mineralType,{12,18}}, mineralB{mineralType,{12,22}};
    FakeUnit geyser{geyserType,{20,13}};
    FakeUnit farMineral{mineralType,{60,60}};
    game.player.units={&hatch};
    game.minerals={&mineralA,&mineralB,&farMineral};
    game.geysers={&geyser};

    assert(BlocksResourceGathering({16,20},pool)); // Between hatchery and minerals.
    assert(BlocksResourceGathering({21,16},colony)); // Between hatchery and geyser.
    assert(BlocksResourceGathering({13,20},colony)); // Inside the mineral line.
    assert(!BlocksResourceGathering({27,20},pool)); // Behind the hatchery.
    assert(!BlocksResourceGathering({20,25},pool)); // Below the hatchery, off every path.
    assert(!BlocksResourceGathering({58,58},pool)); // Minerals with no base of ours.
    assert(!BlocksResourceGathering({20,13},extractor)); // Extractors belong on the geyser.
    game.player.units={};
    assert(!BlocksResourceGathering({16,20},pool)); // No depot, no mining path to protect.
    std::cout << "Placement regressions passed.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='ikkrius-placement-') as directory:
    directory=Path(directory)
    (directory/'placement.cpp').write_text(source)
    subprocess.run(['cl','/nologo','/EHsc','/std:c++20','placement.cpp','/Fe:placement.exe'],
                   cwd=directory,check=True)
    subprocess.run([str(directory/'placement.exe')],check=True)
