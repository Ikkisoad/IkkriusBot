#pragma once
#pragma once
#include "../../../visualstudio/src/starterbot/BuildOrder.h"
#include <BWAPI.h>

class Overpool : public BuildOrder {
public:
    static Overpool& Instance();
    void Execute() override;
    void OnUnitCreate(BWAPI::Unit unit) override;
    void onUnitComplete(BWAPI::Unit unit) override;
    void onStart() override;
    std::string GetName() const override { return "Overpool"; }
private:
    Overpool() = default;
};