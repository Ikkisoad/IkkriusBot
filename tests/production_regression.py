"""Compile focused production regressions using the real helper function bodies.

Run from a VS 2022 developer prompt: python tests/production_regression.py
The fake BWAPI world exercises decisions without needing a running StarCraft match.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(path, signature):
    text = (ROOT / path).read_text()
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

preamble = r'''
#include <cassert>
#include <vector>
#include <utility>
#include <iostream>
#include <string>
#include <algorithm>
#include "CombatPolicy.h"
namespace BWAPI {
struct TilePosition {
    int x=-1;
    bool isValid() const { return x>=0; }
    bool operator==(TilePosition other) const { return x==other.x; }
};
struct UnitType {
    int id=0, minerals=0, gas=0;
    bool operator==(UnitType other) const { return id==other.id; }
    bool operator!=(UnitType other) const { return id!=other.id; }
    int supplyRequired() const { return id==1 ? 1 : (id==13 ? 4 : (id==2 || id==14 ? 2 : 0)); }
    bool isBuilding() const { return id>=4 && id<=12; }
    bool canAttack() const { return id==1 || id==13 || id==14; }
    int maxHitPoints() const { return 120; }
    bool isTwoUnitsInOneEgg() const { return id==1; }
    bool isWorker() const { return id==2; }
    bool isResourceDepot() const { return id==4 || id==5 || id==6; }
    std::string getName() const { return std::to_string(id); }
    int supplyProvided() const { return id==16 ? 16 : (isResourceDepot() ? 2 : 0); }
    int mineralPrice() const { return minerals; }
    int gasPrice() const { return gas; }
};
namespace UnitTypes {
const UnitType Zerg_Zergling{1,50}, Zerg_Drone{2,50}, Zerg_Larva{3},
Zerg_Hatchery{4,300}, Zerg_Lair{5,150,100}, Zerg_Hive{6,200,150},
Zerg_Spawning_Pool{7,200}, Zerg_Extractor{8,50}, Zerg_Queens_Nest{9,150,100},
Zerg_Spire{10}, Zerg_Greater_Spire{11}, Zerg_Hydralisk_Den{12,100,50},
Zerg_Mutalisk{13,100,100}, Zerg_Hydralisk{14,75,25}, Zerg_Lurker{15,50,100}, Zerg_Overlord{16,100}, Zerg_Egg{17}, Zerg_Guardian{18,50,100}, Zerg_Devourer{19,150,50}, Zerg_Queen{20,100,100}, None{};
}
struct TechType {
    int id;
    operator int() const { return id; }
    int mineralPrice() const { return id==1 ? 200 : 100; }
    int gasPrice() const { return id==1 ? 200 : 100; }
};
namespace TechTypes { const TechType Lurker_Aspect{1}, Ensnare{2}, Spawn_Broodlings{3}; }
namespace UnitCommandTypes { enum { None, Build, Morph }; }
struct Player;
struct FakeUnit;
using Unit=FakeUnit*;
struct Unitset : std::vector<Unit> {
    using std::vector<Unit>::vector;
    bool contains(Unit u) const { return std::find(begin(),end(),u)!=end(); }
    void insert(Unit u) { if(!contains(u)) push_back(u); }
};
struct Command {
    int kind=0; UnitType type;
    int getType() const { return kind; }
    UnitType getUnitType() const { return type; }
};
using UnitCommand=Command;
struct FakeUnit {
    UnitType type, buildType;
    Command command;
    TilePosition tile{0};
    bool completed=true, idle=false, morphing=false, allowed=true, accepted=true;
    UnitType morphType;
    int lastFrame=-100, morphCalls=0;
    UnitType getType() const { return type; }
    UnitType getBuildType() const { return buildType; }
    Command getLastCommand() const { return command; }
    TilePosition getTilePosition() const { return tile; }
    bool isCompleted() const { return completed; }
    bool isIdle() const { return idle; }
    bool isMorphing() const { return morphing; }
    bool isLoaded() const { return false; }
    bool isUnderAttack() const { return false; }
    bool exists() const { return true; } bool isVisible() const { return true; }
    bool isFlying() const { return false; } Player* getPlayer() const { return nullptr; }
    int getHitPoints() const { return 120; }
    int getLastCommandFrame() const { return lastFrame; }
    bool canMorph(UnitType) const { return allowed; }
    bool morph(UnitType requested) { ++morphCalls; morphType=requested; return accepted; }
};
struct Player {
    Unitset units;
    int mineralStock=0, gasStock=0, totalSupply=18, usedSupply=0;
    bool researched=false, researching=false;
    bool hasResearched(int) const { return researched; }
    bool isResearching(int) const { return researching; }
    Unitset getUnits() const { return units; }
    int supplyTotal() const { return totalSupply; }
    int supplyUsed() const { return usedSupply; }
    bool isEnemy(Player*) const { return false; }
    int minerals() const { return mineralStock; }
    int gas() const { return gasStock; }
    TilePosition getStartLocation() const { return {0}; }
};
struct Game {
    Player player;
    int frame=100;
    Player* self() { return &player; }
    Unitset getAllUnits() const { return {}; }
    int getFrameCount() const { return frame; }
    int getLatencyFrames() const { return 3; }
    void printf(const char*) {}
} game;
Game* Broodwar=&game;
}
namespace MatchLog { void Event(const std::string&, const std::string&) {}
void Command(const std::string&, const std::string&, bool) {} }
namespace Tools {
int buildCalls=0, researchCalls=0;
bool ResearchTech(int) { ++researchCalls; return true; }
BWAPI::UnitType requested;
BWAPI::TilePosition queued;
bool buildAccepted=false;
int CountUnitsOfType(BWAPI::UnitType,const BWAPI::Unitset&,bool);
int CountUnitOfType(BWAPI::UnitType);
int GetTotalSupply(bool);
bool HasPendingConstruction(BWAPI::Unit);
std::pair<int,int> GetConstructionReserve();
bool MorphUnit(BWAPI::Unit,BWAPI::UnitType);
bool MorphLarva(BWAPI::UnitType);
bool TryBuildBuilding(BWAPI::UnitType,int,BWAPI::TilePosition);
BWAPI::TilePosition IsQueued(BWAPI::UnitType) { return queued; }
bool BuildBuildingOptimal(BWAPI::UnitType type,BWAPI::TilePosition) {
    ++buildCalls; requested=type; return buildAccepted;
}
}
namespace Micro {
std::vector<BWAPI::Unitset> testGroups;
std::vector<BWAPI::Unitset> GetHydraGroups(const BWAPI::Unitset&) { return testGroups; }
}
namespace BasesTools {
std::vector<BWAPI::TilePosition> allBasePositions{{0},{20},{40},{60},{80}};
int CountMiningSites();
BWAPI::TilePosition GetNextExpansionPosition() { return {20}; }
}
class HiveTech {
public:
    enum class Phase { SafeOpener, TechToLair, LairHarass, TechToHive, HiveAssault };
    Phase currentPhase=Phase::SafeOpener;
    bool builtSpawningPool=false, builtExtractor=false, hasNatural=false;
    bool builtQueensNest=false, builtHive=false;
    bool builtHydraliskDen=false, builtLair=false, builtSpire=false, builtGreaterSpire=false;
    bool m_airBuild=false;
    int m_reserveMinerals=0, m_reserveGas=0;
    int zerglingsTarget=6;
    void SpendArmyBudget(int, int, int = 0);
    BWAPI::Unitset m_pressureWave;
    void UpdatePressureWave(bool,int);
    void MaintainQueenSupport();
    void ExecuteAirTech();
    void ExecuteAirHarass();
    void ExecuteAirAssault();
    void ExecuteTechToLair();
    void ExecuteLairHarass();
    void ExecuteSafeOpener();
    void ExecuteTechToHive();
};
'''
checks = r'''
int main() {
    using namespace BWAPI;
    using namespace BWAPI::UnitTypes;
    FakeUnit ling1{Zerg_Zergling}, ling2{Zerg_Zergling}, egg;
    egg.buildType=Zerg_Zergling; egg.completed=false;
    assert(Tools::CountUnitsOfType(Zerg_Zergling,{&ling1,&ling2},true)==2);
    assert(Tools::CountUnitsOfType(Zerg_Zergling,{&ling1,&ling2,&egg},true)==4);
    assert(Tools::CountUnitsOfType(Zerg_Zergling,{&ling1,&ling2,&egg},false)==2);
    game.player.mineralStock=2000; game.player.gasStock=2000;
    FakeUnit larva{Zerg_Larva};
    larva.command={UnitCommandTypes::Morph,Zerg_Drone}; larva.lastFrame=100;
    assert(Tools::CountUnitsOfType(Zerg_Drone,{&larva},true)==1);
    assert(!Tools::MorphUnit(&larva,Zerg_Drone));
    assert(!Tools::MorphUnit(&larva,Zerg_Zergling)); // Do not overwrite a pending drone.
    game.frame=102;
    assert(!Tools::MorphUnit(&larva,Zerg_Drone));
    assert(!Tools::MorphUnit(&larva,Zerg_Zergling));
    game.frame=104; larva.accepted=false;
    assert(Tools::CountUnitsOfType(Zerg_Drone,{&larva},true)==0);
    assert(!Tools::MorphUnit(&larva,Zerg_Drone));
    larva.accepted=true;
    assert(Tools::MorphUnit(&larva,Zerg_Drone));
    assert(larva.morphCalls==2);
    game.player.mineralStock=200; game.player.gasStock=0; Tools::buildAccepted=true;
    assert(Tools::TryBuildBuilding(Zerg_Spawning_Pool,1,{0}));
    assert(Tools::buildCalls==1);
    assert(!Tools::TryBuildBuilding(Zerg_Spawning_Pool,1,{-1}));
    assert(!Tools::TryBuildBuilding(Zerg_Queens_Nest,1,{0}));
    assert(Tools::buildCalls==1);
    FakeUnit hatch{Zerg_Hatchery}; game.player.units={&hatch};
    game.player.mineralStock=300;
    assert(Tools::TryBuildBuilding(Zerg_Hatchery,1,{20}));
    assert(Tools::buildCalls==2); // Existing main must not suppress the natural.
    assert(Tools::TryBuildBuilding(Zerg_Hatchery,1,{0}));
    assert(Tools::buildCalls==2);
    Tools::buildAccepted=false;
    assert(!Tools::TryBuildBuilding(Zerg_Hatchery,1,{20}));
    assert(Tools::buildCalls==3);
    FakeUnit pool{Zerg_Spawning_Pool}; pool.completed=false;
    game.player.units={&pool};
    assert(Tools::TryBuildBuilding(Zerg_Spawning_Pool,1,{0}));
    assert(Tools::buildCalls==3); // Don't duplicate incomplete structures.

    HiveTech strategy;
    std::vector<FakeUnit> drones(9,FakeUnit{Zerg_Drone});
    game.player.units.clear();
    for(auto& drone:drones) game.player.units.push_back(&drone);
    larva.command={}; larva.lastFrame=-100; larva.morphCalls=0;
    game.player.units.push_back(&larva);
    game.player.mineralStock=150;
    strategy.ExecuteSafeOpener();
    assert(larva.morphCalls==0); // Save for pool instead of spending on drones.
    game.player.mineralStock=200;
    strategy.ExecuteSafeOpener();
    assert(Tools::requested==Zerg_Spawning_Pool);
    strategy.builtSpawningPool=true; strategy.builtExtractor=true;
    pool.completed=true;
    game.player.units.push_back(&pool);
    game.player.units.push_back(&ling1); game.player.units.push_back(&ling2);
    strategy.ExecuteSafeOpener();
    assert(larva.morphCalls==1); // Two living lings are not four or six.

    FakeUnit lair{Zerg_Lair}, nest{Zerg_Queens_Nest};
    game.player.units={&lair,&nest,&larva};
    game.player.mineralStock=200; game.player.gasStock=200;
    strategy.currentPhase=HiveTech::Phase::TechToHive;
    strategy.builtQueensNest=true; lair.accepted=false;
    int armyCalls=larva.morphCalls;
    strategy.ExecuteTechToHive();
    assert(!strategy.builtHive && lair.morphCalls==1);
    assert(larva.morphCalls==armyCalls); // Reserve Hive resources ahead of army.
    lair.accepted=true;
    strategy.ExecuteTechToHive();
    assert(lair.morphCalls==2); // Rejected morph is retried.
    strategy.builtHive=true; lair.type=Zerg_Hive; lair.completed=false;
    strategy.ExecuteTechToHive();
    assert(strategy.currentPhase==HiveTech::Phase::TechToHive);
    lair.completed=true;
    strategy.ExecuteTechToHive();
    assert(strategy.currentPhase==HiveTech::Phase::HiveAssault);
    FakeUnit larvaA{Zerg_Larva}, larvaB{Zerg_Larva}, larvaC{Zerg_Larva};
    game.player.units={&pool,&larvaA,&larvaB,&larvaC};
    game.player.mineralStock=400; game.player.gasStock=200;
    strategy.SpendArmyBudget(300,200);
    assert(larvaA.morphCalls + larvaB.morphCalls + larvaC.morphCalls == 2);
    assert(larvaA.morphType==Zerg_Zergling); // Spend excess while saving for an expansion.
    larvaA.morphCalls=larvaB.morphCalls=larvaC.morphCalls=0;
    game.player.mineralStock=300;
    strategy.SpendArmyBudget(300,200);
    assert(larvaA.morphCalls + larvaB.morphCalls + larvaC.morphCalls == 0);
    FakeUnit spire{Zerg_Spire}; game.player.units.push_back(&spire);
    game.player.mineralStock=500; game.player.gasStock=300;
    strategy.SpendArmyBudget(300,200);
    assert(larvaA.morphType==Zerg_Mutalisk);
    assert(larvaB.morphType==Zerg_Zergling && larvaC.morphType==Zerg_Zergling);
    larvaA.morphCalls=larvaB.morphCalls=larvaC.morphCalls=0;
    game.player.mineralStock=100; game.player.gasStock=100;
    strategy.SpendArmyBudget(0,0); // Emergency defense can use the reserved stockpile.
    assert(larvaA.morphType==Zerg_Mutalisk && larvaA.morphCalls==1);
    assert(larvaB.morphCalls + larvaC.morphCalls == 0);
    HiveTech opening;
    opening.builtExtractor=true;
    game.player.units={&hatch};
    game.player.mineralStock=1000; game.player.gasStock=1000;
    opening.ExecuteTechToLair();
    assert(Tools::requested==Zerg_Hydralisk_Den);
    assert(opening.m_reserveMinerals==100 && opening.m_reserveGas==50);
    opening.builtHydraliskDen=true;
    std::vector<FakeUnit> hydras(8,FakeUnit{Zerg_Hydralisk});
    for(int i=0;i<5;++i) game.player.units.push_back(&hydras[i]);
    int previousMorphs=hatch.morphCalls;
    opening.ExecuteTechToLair();
    assert(hatch.morphCalls==previousMorphs); // Field Hydras before spending on Lair.
    game.player.units.push_back(&hydras[5]);
    opening.ExecuteTechToLair();
    assert(hatch.morphCalls==previousMorphs+1 && hatch.morphType==Zerg_Lair);
    opening.builtLair=true; hatch.type=Zerg_Lair;
    opening.ExecuteTechToLair();
    assert(opening.currentPhase==HiveTech::Phase::LairHarass); // Army isn't gated on Lurker research.
    opening.ExecuteLairHarass();
    assert(Tools::researchCalls==0);
    game.player.units.push_back(&hydras[6]); game.player.units.push_back(&hydras[7]);
    opening.ExecuteLairHarass();
    assert(Tools::researchCalls==1 && opening.m_reserveGas==200);
    game.player.researching=true;
    opening.ExecuteLairHarass();
    assert(Tools::researchCalls==1); // Don't spam an active research order.
    game.player.researching=false; game.player.researched=true;
    opening.ExecuteLairHarass();
    int lurkerMorphs=0;
    for(auto& hydra:hydras) lurkerMorphs+=hydra.morphCalls;
    assert(lurkerMorphs==1); // Convert one Hydra at a time, retain the ranged army.
    FakeUnit builder{Zerg_Drone};
    builder.command={UnitCommandTypes::Build,Zerg_Hydralisk_Den};
    game.player.units={&builder,&larvaA};
    game.player.mineralStock=100; game.player.gasStock=50;
    int before=larvaA.morphCalls;
    assert(Tools::HasPendingConstruction(&builder));
    assert(!Tools::MorphUnit(&larvaA,Zerg_Zergling));
    assert(larvaA.morphCalls==before); // Leave the Den's cost for the walking builder.
    game.player.mineralStock=150;
    assert(Tools::MorphUnit(&larvaA,Zerg_Zergling));
    builder.idle=true; builder.lastFrame=game.frame;
    assert(Tools::HasPendingConstruction(&builder)); // Protect construction during latency.
    assert(Tools::CountUnitsOfType(Zerg_Hydralisk_Den,{&builder},true)==1);
    game.frame+=4;
    assert(!Tools::HasPendingConstruction(&builder)); // Release a failed idle order for retry.
    assert(Tools::GetConstructionReserve().first==0);
    larvaB.command={UnitCommandTypes::Morph,Zerg_Overlord}; larvaB.lastFrame=game.frame;
    game.player.units={&larvaB};
    assert(Tools::GetTotalSupply(true)==34); // Count the Overlord before its egg is visible.
    game.frame+=4;
    assert(Tools::GetTotalSupply(true)==18); // Expired rejected orders cannot hide a supply block.
    egg.type=Zerg_Egg; egg.buildType=Zerg_Overlord;
    game.player.units={&egg};
    assert(Tools::GetTotalSupply(true)==34);
    hatch.type=Zerg_Hatchery; hatch.command={UnitCommandTypes::Build,Zerg_Hatchery};
    game.player.units={&hatch};
    assert(Tools::GetTotalSupply(true)==18); // Don't double count completed depots' stale commands.
    builder.command={UnitCommandTypes::Build,Zerg_Hatchery}; builder.idle=false;
    game.player.units={&builder};
    assert(Tools::GetTotalSupply(true)==20);
    HiveTech airBuild; airBuild.m_airBuild=true; airBuild.builtExtractor=true;
    hatch.type=Zerg_Hatchery; hatch.command={};
    game.player.units={&hatch}; game.player.mineralStock=1000; game.player.gasStock=1000;
    airBuild.ExecuteAirTech();
    assert(hatch.morphType==Zerg_Lair); // No Hydra Den detour in the air build.
    airBuild.builtLair=true; hatch.type=Zerg_Lair;
    Tools::buildAccepted=true;
    airBuild.ExecuteAirTech();
    assert(Tools::requested==Zerg_Spire);
    spire.type=Zerg_Spire; game.player.units.push_back(&spire); airBuild.builtSpire=true;
    airBuild.ExecuteAirTech();
    assert(airBuild.currentPhase==HiveTech::Phase::LairHarass);
    std::vector<FakeUnit> mutas(12,FakeUnit{Zerg_Mutalisk}), airDrones(24,FakeUnit{Zerg_Drone});
    for(auto& muta:mutas) game.player.units.push_back(&muta);
    for(auto& drone:airDrones) game.player.units.push_back(&drone);
    airBuild.hasNatural=true;
    airBuild.ExecuteAirHarass();
    assert(airBuild.currentPhase==HiveTech::Phase::TechToHive);
    airBuild.ExecuteAirAssault();
    assert(spire.morphType==Zerg_Greater_Spire);
    airBuild.builtGreaterSpire=true; spire.type=Zerg_Greater_Spire;
    airBuild.ExecuteAirAssault();
    int guardians=0;
    for(auto& muta:mutas) if(muta.morphType==Zerg_Guardian) ++guardians;
    assert(guardians==1); // Actually issue one Guardian morph, not just select a ratio.
    game.player.units={&larvaA,&pool}; larvaA.command={}; larvaA.lastFrame=-100;
    int beforeReserve=larvaA.morphCalls;
    airBuild.SpendArmyBudget(0,0,18);
    assert(larvaA.morphCalls==beforeReserve); // Army cannot consume supply held for its Queen.
    HiveTech supportBuild;
    Micro::testGroups.resize(2);
    game.player.units={&larvaA}; game.player.mineralStock=1000; game.player.gasStock=1000;
    supportBuild.MaintainQueenSupport();
    assert(supportBuild.m_reserveMinerals==0); // Do not try Queen tech before Lair.
    supportBuild.builtLair=true;
    supportBuild.MaintainQueenSupport();
    assert(Tools::requested==Zerg_Queens_Nest && supportBuild.m_reserveGas==100);
    supportBuild.builtQueensNest=true;
    FakeUnit supportQueen{Zerg_Queen};
    game.player.units={&nest,&supportQueen,&larvaA};
    larvaA.command={}; larvaA.lastFrame=-100;
    int queenCalls=larvaA.morphCalls;
    game.player.researched=true;
    supportBuild.MaintainQueenSupport();
    assert(larvaA.morphCalls==queenCalls+1 && larvaA.morphType==Zerg_Queen);
    larvaA.command={UnitCommandTypes::Morph,Zerg_Queen}; larvaA.lastFrame=game.frame;
    supportBuild.MaintainQueenSupport();
    assert(larvaA.morphCalls==queenCalls+1); // The in-progress second Queen covers the second group.
    FakeUnit macro{Zerg_Hatchery}; macro.tile={10};
    FakeUnit natural{Zerg_Hatchery}; natural.tile={20}; natural.completed=false;
    game.player.units={&hatch,&macro,&natural};
    assert(BasesTools::CountMiningSites()==2); // Macro Hatcheries do not count; an expansion under construction does.
    game.player.units={&macro,&natural};
    assert(BasesTools::CountMiningSites()==1); // Destroyed depots leave no stale owned-site count.
    HiveTech pressure;
    game.player.totalSupply=400; game.player.usedSupply=400;
    game.player.mineralStock=3000; game.player.gasStock=0;
    FakeUnit guardian{Zerg_Guardian}, devourerUnit{Zerg_Devourer}, lurkerUnit{Zerg_Lurker};
    std::vector<FakeUnit> waveHydras(30,FakeUnit{Zerg_Hydralisk});
    game.player.units={&hatch,&pool,&nest,&larva,&supportQueen,&guardian,&devourerUnit,&lurkerUnit};
    FakeUnit pressureDen{Zerg_Hydralisk_Den}; game.player.units.push_back(&pressureDen);
    for(auto& u:waveHydras) game.player.units.push_back(&u);
    pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.empty()); // A mineral bank cannot replace gas units.
    game.player.gasStock=1200;
    pressure.UpdatePressureWave(false,300);
    assert(pressure.m_pressureWave.empty()); // Base threats suppress commitments.
    pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.size()==20); // Bounded to 20 displayed supply.
    for(auto u:pressure.m_pressureWave) assert(u->type==Zerg_Hydralisk);
    pressure.UpdatePressureWave(false,300);
    assert(pressure.m_pressureWave.empty()); // Defense interrupts an existing wave.
    pressure.UpdatePressureWave(true,300);
    assert(!pressure.m_pressureWave.empty());
    game.player.units.erase(std::find(game.player.units.begin(),game.player.units.end(),&pressureDen));
    pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.empty()); // Lost replacement tech cancels the wave.
    game.player.units.push_back(&pressureDen); pressure.UpdatePressureWave(true,300);
    assert(!pressure.m_pressureWave.empty());
    game.player.gasStock=0; pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.empty()); // Stop when replacements become unaffordable.
    game.player.gasStock=1200; game.player.usedSupply=380;
    pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.empty()); // Refill before launching another wave.
    game.player.usedSupply=400; game.player.totalSupply=380;
    pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.empty()); // Lost Overlords do not trigger a wave.
    game.player.totalSupply=400;
    game.player.units={&hatch,&pool,&larva,&spire};
    std::vector<FakeUnit> flock(12,FakeUnit{Zerg_Mutalisk});
    for(auto& u:flock) game.player.units.push_back(&u);
    pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.size()==4); // Keep eight Mutas for the air tech composition.
    game.player.usedSupply=348; pressure.UpdatePressureWave(true,300);
    assert(pressure.m_pressureWave.empty());
    assert(CombatPolicy::SurplusExpansion(1200,24,32));
    assert(!CombatPolicy::SurplusExpansion(1199,54,100));
    assert(!CombatPolicy::SurplusExpansion(3000,12,100));
    assert(!CombatPolicy::SurplusExpansion(3000,54,16));
    std::cout << "Surplus expansion and pressure-wave regressions passed.\n";
    std::cout << "Queen support production regressions passed.\n";
    std::cout << "Air strategy and Queen supply regressions passed.\n";
    std::cout << "Supply reservation regressions passed.\n";
    std::cout << "Construction reservation regressions passed.\n";
    std::cout << "Tech-order regressions passed.\n";
    std::cout << "Army budget regressions passed.\n";
    std::cout << "Production regressions passed (counting, budgets, retry, expansion, opener, Hive).\n";
}
'''
source = preamble
for signature in ['int Tools::CountUnitsOfType', 'int Tools::CountUnitOfType',
                  'bool Tools::HasPendingConstruction', 'std::pair<int, int> Tools::GetConstructionReserve', 'bool Tools::MorphUnit', 'bool Tools::MorphLarva', 'bool Tools::TryBuildBuilding', 'int Tools::GetTotalSupply']:
    source += '\n' + function('src/starterbot/Tools.cpp', signature)
for signature in ['void HiveTech::ExecuteSafeOpener', 'void HiveTech::ExecuteTechToHive', 'void HiveTech::SpendArmyBudget', 'void HiveTech::ExecuteTechToLair', 'void HiveTech::ExecuteLairHarass', 'void HiveTech::ExecuteAirTech', 'void HiveTech::ExecuteAirHarass', 'void HiveTech::ExecuteAirAssault', 'void HiveTech::MaintainQueenSupport', 'void HiveTech::UpdatePressureWave']:
    source += '\n' + function('src/starterbot/buildorders/HiveTech.cpp', signature)
source += '\n' + function('visualstudio/BasesTools.cpp', 'int BasesTools::CountMiningSites')
source += checks
with tempfile.TemporaryDirectory(prefix='ikkrius-regression-') as directory:
    directory = Path(directory)
    (directory / 'regression.cpp').write_text(source)
    subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++20', '/I'+str(ROOT/'src/starterbot'), 'regression.cpp', '/Fe:regression.exe'],
                   cwd=directory, check=True)
    subprocess.run([str(directory / 'regression.exe')], check=True)
