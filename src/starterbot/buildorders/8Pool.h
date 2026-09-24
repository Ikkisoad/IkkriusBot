#pragma once
#include "../../../visualstudio/src/starterbot/BuildOrder.h"
#include <BWAPI.h>

class EightPool : public BuildOrder {
public:
    static EightPool& Instance();
    void Execute() override;
    void OnUnitCreate(BWAPI::Unit unit) override;
    void onUnitComplete(BWAPI::Unit unit) override;
    void onStart() override;
    std::string GetName() const override { return "EightPool"; }
private:
    EightPool() = default;
    bool builtEightDrones = false;
	bool builtExtractor = false;
    bool builtSpawningPool = false;
};