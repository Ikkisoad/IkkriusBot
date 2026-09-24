// BuildOrderFactory.h
#pragma once
#include "../../../visualstudio/src/starterbot/BuildOrder.h" // Adjusted the include path to match the relative location
#include "../buildorders/4Pool.h"
#include "../buildorders/5Pool.h"
#include "../buildorders/6Pool.h"
#include "../buildorders/7Pool.h"
#include "../buildorders/8Pool.h"
#include "../buildorders/Genetic.h"
#include "../buildorders/Overpool.h"
#include "../buildorders/HiveTech.h"
#include "../buildorders/MutaHive.h"

enum class BuildOrderType { FourPool, FivePool, SixPool, SevenPool, EightPool, Genetic, Overpool, HiveTech, MutaHive };

class BuildOrderFactory {
public:
    static BuildOrder* Create(BuildOrderType type) {
        switch (type) {
            // Pool possibilities:
            // Extractor trick at 9/9 for extractor/drone/zergling
            case BuildOrderType::FourPool: return &FourPool::Instance();
            case BuildOrderType::FivePool: return &FivePool::Instance();
            case BuildOrderType::SixPool: return &SixPool::Instance();
            case BuildOrderType::SevenPool: return &SevenPool::Instance();
            case BuildOrderType::EightPool: return &EightPool::Instance();
            case BuildOrderType::Genetic: return &Genetic::Instance();
            case BuildOrderType::Overpool: return &Overpool::Instance();
            case BuildOrderType::MutaHive: return &MutaHive::Instance();
            case BuildOrderType::HiveTech: return &HiveTech::Instance();
            default: return nullptr;
        }
    }
};