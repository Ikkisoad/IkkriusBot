#include "MatchLog.h"
#include "Tools.h"
#include "micro.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
    std::ofstream output;
    std::map<std::string, int> lastCommandLog;
    int lastSnapshot = -120;
    int lastFrame = 0;
    std::string lastDecision;
    const char* revision = "surplus-pressure-v5";

    std::string JsonString(const std::string& value) {
        std::ostringstream out;
        out << '"';
        for (unsigned char c : value) {
            if (c == '"' || c == '\\') out << '\\' << c;
            else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
            else out << c;
        }
        out << '"';
        return out.str();
    }

    void Write(const std::string& kind, const std::string& fields) {
        if (!output) return;
        if (BWAPI::BroodwarPtr) lastFrame = BWAPI::Broodwar->getFrameCount();
        output << "{\"frame\":" << lastFrame
            << ",\"event\":" << JsonString(kind) << fields << "}\n";
        output.flush(); // Keep the last useful state even if the client disconnects.
    }
}

void MatchLog::Start(const std::string& strategy) {
    End("interrupted");
    output.clear();
    lastCommandLog.clear();
    lastSnapshot = -120;
    lastFrame = 0;
    lastDecision.clear();
    std::error_code error;
    auto directory = std::filesystem::current_path(error);
#ifdef _WIN32
    wchar_t modulePath[32768] = {};
    if (GetModuleFileNameW(nullptr, modulePath, 32768)) directory = std::filesystem::path(modulePath).parent_path();
#endif
    if (directory.filename() == "updated") directory = directory.parent_path();
    directory /= "logs";
    std::filesystem::create_directories(directory, error);
    static unsigned sequence = 0;
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto path = directory / ("match-" + std::to_string(stamp) + "-" + std::to_string(++sequence) + ".jsonl");
    if (!error) output.open(path);
    if (!output || error) {
        std::cout << "Match logging unavailable: " << directory.string() << "\n";
        return;
    }
    std::cout << "Match log: " << path.string() << "\n";
    auto enemy = BWAPI::Broodwar->enemy();
    Write("start", ",\"revision\":" + JsonString(revision) +
        ",\"build\":" + JsonString(__DATE__ " " __TIME__) +
        ",\"strategy\":" + JsonString(strategy) +
        ",\"map\":" + JsonString(BWAPI::Broodwar->mapFileName()) +
        ",\"map_hash\":" + JsonString(BWAPI::Broodwar->mapHash()) +
        ",\"opponent\":" + JsonString(enemy ? enemy->getName() : "unknown") +
        ",\"race\":" + JsonString(enemy ? enemy->getRace().getName() : "unknown"));
}

void MatchLog::End(const std::string& result) {
    if (!output.is_open()) return;
    Write("end", ",\"result\":" + JsonString(result));
    output.close();
}

void MatchLog::Event(const std::string& kind, const std::string& detail) {
    Write(kind, ",\"detail\":" + JsonString(detail));
}

void MatchLog::Command(const std::string& action, const std::string& type, bool accepted) {
    if (!output) return;
    const std::string reason = accepted ? "accepted" : BWAPI::Broodwar->getLastError().toString();
    const std::string key = action + ":" + type + ":" + reason;
    const int frame = BWAPI::Broodwar->getFrameCount();
    const auto previous = lastCommandLog.find(key);
    if (previous != lastCommandLog.end() && frame - previous->second < 120) return;
    lastCommandLog[key] = frame;
    Write("command", ",\"action\":" + JsonString(action) + ",\"type\":" + JsonString(type) +
        ",\"accepted\":" + (accepted ? "true" : "false") + ",\"reason\":" + JsonString(reason));
}

void MatchLog::UnitEvent(const std::string& kind, BWAPI::Unit unit) {
    if (!unit || !output) return;
    const bool ours = unit->getPlayer() == BWAPI::Broodwar->self();
    if (!ours && !BWAPI::Broodwar->self()->isEnemy(unit->getPlayer())) return;
    Write(kind, ",\"side\":" + JsonString(ours ? "self" : "enemy") +
        ",\"id\":" + std::to_string(unit->getID()) + ",\"type\":" + JsonString(unit->getType().getName()) +
        ",\"x\":" + std::to_string(unit->getPosition().x) + ",\"y\":" + std::to_string(unit->getPosition().y));
}

void MatchLog::Snapshot(const std::string& phase, const std::string& decision, int reserveMinerals, int reserveGas) {
    if (!output) return;
    const int frame = BWAPI::Broodwar->getFrameCount();
    if (frame - lastSnapshot < 120) return;
    lastSnapshot = frame;
    const auto state = phase + ":" + decision;
    if (state != lastDecision) {
        Event("decision", state);
        lastDecision = state;
    }
    auto self = BWAPI::Broodwar->self();
    int workers = 0, idleWorkers = 0, gasWorkers = 0, bases = 0, larvae = 0, armySupply = 0, idleArmy = 0;
    std::map<std::string, int> counts, visibleEnemies;
    for (auto enemy : BWAPI::Broodwar->getAllUnits()) {
        if (enemy->isVisible() && self->isEnemy(enemy->getPlayer())) ++visibleEnemies[enemy->getType().getName()];
    }
    for (auto unit : self->getUnits()) {
        ++counts[unit->getType().getName()];
        if (unit->getType().isWorker()) {
            ++workers;
            if (unit->isIdle()) ++idleWorkers;
            if (unit->isGatheringGas()) ++gasWorkers;
        }
        if (unit->getType().isResourceDepot()) ++bases;
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Larva) ++larvae;
        if (unit->isCompleted() && !unit->getType().isWorker() && !unit->getType().isBuilding() && unit->getType().canAttack()) {
            armySupply += unit->getType().supplyRequired();
            if (unit->isIdle()) ++idleArmy;
        }
    }
    std::ostringstream fields;
    fields << ",\"phase\":" << JsonString(phase) << ",\"decision\":" << JsonString(decision)
        << ",\"race\":" << JsonString(BWAPI::Broodwar->enemy() ? BWAPI::Broodwar->enemy()->getRace().getName() : "unknown")
        << ",\"minerals\":" << self->minerals() << ",\"gas\":" << self->gas()
        << ",\"supply_used\":" << self->supplyUsed() << ",\"supply_total\":" << self->supplyTotal()
        << ",\"supply_pending\":" << Tools::GetTotalSupply(true) - self->supplyTotal()
        << ",\"workers\":" << workers << ",\"idle_workers\":" << idleWorkers << ",\"gas_workers\":" << gasWorkers
        << ",\"bases\":" << bases << ",\"larvae\":" << larvae << ",\"army_supply\":" << armySupply
        << ",\"idle_army\":" << idleArmy << ",\"base_threats\":" << Micro::GetBaseThreats().size()
        << ",\"mode\":" << int(Micro::GetMode()) << ",\"units\":{";
    bool first = true;
    for (const auto& count : counts) {
        if (!first) fields << ',';
        first = false;
        fields << JsonString(count.first) << ':' << count.second;
    }
    fields << "},\"reserve_minerals\":" << reserveMinerals << ",\"reserve_gas\":" << reserveGas << ",\"visible_enemies\":{";
    first = true;
    for (const auto& count : visibleEnemies) {
        if (!first) fields << ',';
        first = false;
        fields << JsonString(count.first) << ':' << count.second;
    }
    fields << '}';
    Write("snapshot", fields.str());
}
