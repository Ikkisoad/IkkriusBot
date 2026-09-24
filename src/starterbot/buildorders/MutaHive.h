#pragma once
#include "HiveTech.h"

// Share economy/recovery code while retaining a distinct strategy and statistics key.
class MutaHive final : public HiveTech {
public:
    static MutaHive& Instance() { static MutaHive instance; return instance; }
    std::string GetName() const override { return "MutaHive"; }
private:
    MutaHive() : HiveTech(true) {}
};
