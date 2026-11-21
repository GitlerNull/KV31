#pragma once
#include "player.h"
#include "map_gen.h"
#include <vector>

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
int Level2PatrolCount();                 // число укрытий Физички
Vector2 Level2PatrolPoint(int index);    // центр укрытия по индексу
// Взаимодействие кнопкой броска: открывает скрытую аварийную дверь или финальную дверь.
// Возвращает true, если нажатие было использовано дверью.
bool Level2Interact(Player& player);
// Предметы Level 2 изолированы от процедурных чанков Level 0.
Item* Level2ItemNear(float wx, float wz, float radius);
void Level2TakeItem(Item* item);
const std::vector<Item>& Level2GetItems();
// Возвращает true один раз после ручного открытия конечной двери.
bool Level2Update(Player& player, float dt);
void Level2Draw(const Player& player, float time, float viewDistance);
void Level2DrawModels();
// Локальный голубой источник в конце скрытого прохода (uniform'ы существующего шейдера).
// pos = {x,y,z,radius}, color = rgb, box = {xMin,xMax,zMin,zMax} - объём, в котором лампа светит. Вне Level 2 всё обнуляется.
bool Level2GetLocalLight(float pos[4], float color[3], float box[4]);
