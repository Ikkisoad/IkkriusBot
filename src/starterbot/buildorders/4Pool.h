#pragma once
#include "../../../visualstudio/src/starterbot/BuildOrder.h"
#include <BWAPI.h>

class FourPool : public BuildOrder {
public:
    static FourPool& Instance();
    void Execute() override;
    void OnUnitCreate(BWAPI::Unit unit) override;
    void onUnitComplete(BWAPI::Unit unit) override;
    bool isLethalTo(BWAPI::Unit myUnit, BWAPI::Unit enemy);
    void onStart() override;
    void attack();
    std::string GetName() const override { return "FourPool"; }
private:
    FourPool() = default;
};