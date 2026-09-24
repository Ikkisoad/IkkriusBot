"""Compile the real log writer and parse its output, including multiple matches."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/starterbot/MatchLog.cpp').read_text()
snapshot = source[source.index('void MatchLog::Snapshot'):]
source = source[:source.index('void MatchLog::UnitEvent')] + snapshot
source = source.replace('#include "MatchLog.h"', '').replace('#include "Tools.h"', '').replace('#include "micro.h"', '')
preamble = r"""
#include <string>
#include <vector>
namespace BWAPI {
struct UnitType {
    bool isWorker() { return false; } bool isBuilding() { return false; }
    bool isResourceDepot() { return false; } bool canAttack() { return false; }
    int supplyRequired() { return 0; } std::string getName() { return "fake"; }
    bool operator==(UnitType) { return true; }
};
namespace UnitTypes { const UnitType Zerg_Larva{}; }
struct Player;
struct UnitImpl {
    UnitType getType() { return {}; } bool isVisible() { return false; }
    bool isIdle() { return false; } bool isGatheringGas() { return false; }
    bool isCompleted() { return true; } Player* getPlayer() { return nullptr; }
};
using Unit=UnitImpl*;
using Unitset=std::vector<Unit>;
struct Race { std::string getName() { return "Terran"; } };
struct Player {
    Unitset getUnits() { return {}; } bool isEnemy(Player*) { return false; }
    int minerals() { return 1000; } int gas() { return 400; }
    int supplyUsed() { return 18; } int supplyTotal() { return 18; }
    std::string getName() { return "Opponent\"\\\n"; } Race getRace() { return {}; } };
struct Error { std::string toString() { return "Insufficient minerals"; } };
struct Game {
    int frame=0;
    Player player;
    int getFrameCount() { return frame; }
    Player* enemy() { return &player; }
    Player* self() { return &player; }
    Unitset getAllUnits() { return {}; }
    std::string mapFileName() { return "test\\map.scx"; }
    std::string mapHash() { return "hash"; }
    Error getLastError() { return {}; }
} game;
Game* Broodwar=&game;
Game* BroodwarPtr=&game;
}
namespace Tools { int GetTotalSupply(bool) { return 34; } }
namespace Micro { int GetMode() { return 2; } BWAPI::Unitset GetBaseThreats() { return {}; } }
namespace MatchLog {
void Snapshot(const std::string&,const std::string&,int,int);
void Start(const std::string&);
void End(const std::string&);
void Event(const std::string&,const std::string&);
void Command(const std::string&,const std::string&,bool);
}
"""
main = r"""
int main() {
    MatchLog::Start("HiveTech");
    MatchLog::Snapshot("TechToLair","reserve_next_tech",300,200);
    MatchLog::Snapshot("TechToLair","reserve_next_tech",300,200);
    MatchLog::Command("morph","Hydralisk",false);
    MatchLog::Command("morph","Hydralisk",false);
    BWAPI::game.frame=120;
    MatchLog::Snapshot("TechToLair","reserve_next_tech",300,200);
    MatchLog::Command("morph","Hydralisk",false);
    MatchLog::Command("morph","Hydralisk",true);
    MatchLog::Event("queen_spell","Ensnare queen=7 targets=4");
    MatchLog::Event("air_morph","Zerg_Guardian");
    MatchLog::Event("queen_support","hydra_groups=2 supported=2 queens=2");
    MatchLog::Event("lurker_burrow","9");
    MatchLog::End("loss");
    BWAPI::game.frame=0;
    MatchLog::Start("HiveTech");
    MatchLog::Event("diagnostic",std::string("quotes\" slash\\ newline\n tab\t control\1"));
    MatchLog::Command("morph","Hydralisk",false);
    BWAPI::BroodwarPtr=nullptr;
    MatchLog::End("disconnected_or_aborted");
}
"""
with tempfile.TemporaryDirectory(prefix='ikkrius-log-') as temporary:
    directory=Path(temporary)
    (directory/'logging.cpp').write_text(preamble + source + main)
    subprocess.run(['cl','/nologo','/EHsc','/std:c++20','logging.cpp','/Fe:logging.exe'],cwd=directory,check=True)
    subprocess.run([str(directory/'logging.exe')],cwd=directory,check=True)
    paths=sorted((directory/'logs').glob('*.jsonl'))
    assert len(paths)==2
    first, second=([json.loads(line) for line in path.read_text().splitlines()] for path in paths)
    assert first[0]['opponent']=='Opponent"\\\n'
    assert first[0]['map']=='test\\map.scx'
    assert len([r for r in first if r['event']=='command'])==3
    assert first[-1]['result']=='loss'
    snapshots=[r for r in first if r['event']=='snapshot']
    assert len(snapshots)==2 and snapshots[0]['reserve_minerals']==300
    assert snapshots[0]['supply_pending']==16 and snapshots[0]['visible_enemies']=={}
    assert second[1]['detail']=='quotes" slash\\ newline\n tab\t control\x01'
    assert second[-1]['result']=='disconnected_or_aborted'
    assert len([r for r in second if r['event']=='command'])==1
    spec=importlib.util.spec_from_file_location('analyzer', ROOT/'tools/analyze_match.py')
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    summary=module.summarize(paths[0])
    assert summary['result']=='loss' and summary['sampled_failures'][0]['samples']==2
    assert summary['estimated_supply_block_frames']==120
    assert summary['estimated_high_mineral_frames']==120
    assert summary['queen_spells_accepted']=={'Ensnare':1}
    assert summary['air_morphs_accepted']=={'Zerg_Guardian':1}
    assert summary['lurker_burrows']==1
    assert summary['last_queen_support']['detail']=='hydra_groups=2 supported=2 queens=2'
    with paths[1].open('a') as stream: stream.write('{"unfinished":')
    assert module.summarize(paths[1])['malformed_lines']==1
print('Logging and analyzer regressions passed.')
