#pragma once
#include <BWAPI.h>
#include "../BWEM/bwem.h"  

namespace BasesTools {
	bool IsAreaEnemyBase(BWAPI::Position position, int range);
	void Initialize();
	int CountMiningSites();
	bool IsAreaOurBase(BWAPI::Position position, int range);
	void CacheBWEMBases();
	const std::vector<BWAPI::Position>& GetAllOurBasePositions();
	void VerifyEnemyBases();
	const std::vector<BWAPI::Position>& GetAllBasePositions();
	void FindExpansionsV1();
	void FindExpansions();
	void DrawExpansions();
	void FindThirdBase();
	BWAPI::Position ReturnBasePosition(BWEM::Area area);
	std::vector<const BWEM::Base*> GetAccessibleNeighborBases(const BWAPI::TilePosition& tilePos);
	void DrawBases(const std::vector<const BWEM::Base*>& bases, BWAPI::Color color);
	BWAPI::Position FindUnexploredStarterPosition();
	BWAPI::TilePosition GetMainBasePosition();
	BWAPI::TilePosition GetNaturalBasePosition();
	BWAPI::TilePosition GetThirdBasePosition();
	BWAPI::TilePosition GetFourthBasePosition();
	BWAPI::TilePosition GetFifthBasePosition();
	std::vector<BWAPI::Position> GetBWEMBases();
	void OnUnitDestroyed(BWAPI::Unit unit);
	// Add getter and setter for enemy base position
	void SetEnemyBasePosition(const BWAPI::Position& pos);
	void SetOurBasePosition();
	BWAPI::Position GetEnemyBasePosition();
	BWAPI::Position GetNearestBasePosition(const BWAPI::Position& position);
	void DrawAllBases(BWAPI::Color color);
	void RemoveEnemyBasePosition(const BWAPI::Position pos);
	void DrawEnemyBases(BWAPI::Color color);
	BWAPI::TilePosition GetNextExpansionPosition();
}