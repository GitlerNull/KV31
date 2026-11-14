// map_gen.h - бесконечный процедурный лабиринт Уровня 0
#pragma once
#include <vector>
#include "util.h"

enum ItemType : uint8_t { IT_WATER, IT_BATTERY, IT_MEDKIT };
struct Item  { int cx, cz; ItemType type; bool taken; };
struct Chunk { int x, z; bool solid[CHUNK][CHUNK]; bool hasExit; int exitX, exitZ; std::vector<Item> items; };

inline int CellOf(float v) { return (int)floorf(v / CELL); }

bool   IsSolid(int cx, int cz);                       // клетка - стена?
bool   CircleHits(float x, float z, float r);         // круг пересекает стену?
bool   LineClear(Vector2 a, Vector2 b);               // прямая видимость по сетке
float  LightAt(float wx, float wz);                   // 0..1 - освещённость лампами в точке
float  LampLevel(int rx, int ry);                     // яркость лампы комнаты (rx,ry)
Chunk& GetChunk(int chx, int chz);                    // чанк генерируется на лету при первом обращении
void   MapUpdate(float px, float pz);                 // выгрузка далёких чанков
void   MapReset(uint32_t seed);                       // новый мир (новое зерно, очистка кэша)
bool   IsExitCell(int cx, int cz);
bool   NearestExit(float px, float pz, Vector2* out); // ближайший люк в радиусе 5 чанков (радиосигнал M.E.G.)
Item*  ItemNear(float wx, float wz, float r);
void   TakeItem(Item* it);
