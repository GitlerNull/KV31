#pragma once
#include "player.h"

// Изолированный статический технический уровень. Level 0 остаётся в map_gen.
void Level2SetActive(bool active);
bool Level2IsActive();
void Level2Reset();
void Level2Load();
void Level2Unload();
bool Level2IsSolid(int cx, int cz);
bool Level2CircleHits(float x, float z, float r);
float Level2LightAt(float wx, float wz);
float Level2FloorAt(float wx, float wz);
bool Level2CanMoveTo(float x, float z, float radius, float currentFloor, float* newFloor);
bool Level2IsDark(float wx, float wz);
Vector2 Level2PatrolPoint(int index);
// Возвращает true один раз после срабатывания финального шлюза.
bool Level2Update(Player& player, float dt);
void Level2Draw(const Player& player, float time, float viewDistance);
void Level2DrawModels();
