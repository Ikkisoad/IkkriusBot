#pragma once
#include <BWAPI.h>
#include <string>

namespace MatchLog {
    void Start(const std::string& strategy);
    void End(const std::string& result);
    void Event(const std::string& kind, const std::string& detail);
    void Command(const std::string& action, const std::string& type, bool accepted);
    // Enemy race as BWAPI reports it, falling back to the race of enemy units seen this match.
    std::string EnemyRace();
    void ObserveEnemy(BWAPI::Unit unit);
    void UnitEvent(const std::string& kind, BWAPI::Unit unit);
    void Snapshot(const std::string& phase, const std::string& decision, int reserveMinerals = 0, int reserveGas = 0);
}
