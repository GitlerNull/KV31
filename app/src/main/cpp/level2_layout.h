// level2_layout.h - все числа планировки Level 2 в одном месте.
// Система координат проекта (проверена по player.cpp/game.cpp): X - на восток, Z - на юг (+Z), Y - вверх;
// yaw=0 смотрит в +Z, yaw=PI/2 смотрит в +X (Forward = {sin(yaw), ., cos(yaw)}). Единицы - метры, клетка = CELL = 2 м.
#pragma once
#include "util.h"
#include "map_gen.h"

namespace l2 {

constexpr int   MAP_W = 64;
constexpr int   MAP_H = 44;
constexpr float WORLD_W = MAP_W * CELL;
constexpr float WORLD_D = MAP_H * CELL;

struct CellRect { int x0, z0, x1, z1; };           // включительно, в клетках
struct Rect { float x0, z0, x1, z1; };             // в метрах

// ---------- Планировка ----------
// Сначала вырезаются проходимые прямоугольники, затем «заглушки» (углы, перегородки), делающие помещения неправильными.
inline constexpr CellRect kOpen[] = {
    {2, 19, 6, 23},     // вестибюль (старт)
    {7, 20, 46, 22},    // центральная магистраль, 6 м
    {47, 19, 50, 23},   // трансформаторный отсек (тупик магистрали)
    {44, 19, 44, 19},   // ниша аварийной двери
    {44, 8, 44, 18},    // скрытый проход
    {10, 12, 10, 19},   // узкий спуск в насосную
    {5, 6, 15, 11},     // насосная
    {16, 9, 19, 9}, {19, 10, 19, 13}, {20, 13, 24, 13},   // кабельный коридор с изломами
    {25, 5, 36, 9},     // электрощитовая, зал А
    {31, 10, 36, 15},   // электрощитовая, зал Б
    {25, 11, 28, 15},   // пультовая
    {26, 10, 26, 10},   // проём пультовая - зал А
    {34, 16, 34, 19},   // выход из зала Б на магистраль
    {14, 23, 14, 24}, {28, 19, 28, 19}, {31, 23, 31, 24},   // ниши обслуживания
    {21, 23, 21, 30},   // узкий спуск на юг
    {16, 31, 30, 39},   // южный насосный зал
    {31, 35, 33, 35},   // переход в генераторный зал
    {34, 27, 45, 37},   // генераторный зал
    {39, 23, 39, 26},   // переход с магистрали в генераторный зал
    {40, 38, 42, 39},   // ниша генераторного зала
};
inline constexpr CellRect kSolid[] = {
    {5, 6, 6, 7}, {13, 6, 15, 7}, {11, 8, 11, 9},          // насосная: срезанные углы и перегородка
    {16, 31, 17, 32}, {29, 37, 30, 39},                    // южный зал
    {34, 27, 35, 28}, {44, 36, 45, 37},                    // генераторный зал
};

// ---------- Высоты потолка (клетки; последующие перекрывают предыдущие) ----------
struct CeilZone { CellRect r; float h; };
constexpr float CEIL_DEFAULT = 3.4f;
inline constexpr CeilZone kCeil[] = {
    {{2, 19, 6, 23}, 3.85f}, {{7, 20, 46, 22}, 3.85f}, {{47, 19, 50, 23}, 3.85f},
    {{10, 12, 10, 19}, 3.0f}, {{21, 23, 21, 30}, 3.0f},
    {{16, 9, 19, 9}, 2.7f}, {{19, 10, 19, 13}, 2.7f}, {{20, 13, 24, 13}, 2.7f},
    {{14, 23, 14, 24}, 3.0f}, {{28, 19, 28, 19}, 3.0f}, {{31, 23, 31, 24}, 3.0f},
    {{25, 11, 28, 15}, 3.0f}, {{26, 10, 26, 10}, 3.0f}, {{34, 16, 34, 19}, 3.0f},
    {{16, 31, 30, 39}, 3.6f}, {{31, 35, 33, 35}, 3.0f}, {{39, 23, 39, 26}, 3.0f},
    {{34, 27, 45, 37}, 4.0f}, {{40, 38, 42, 39}, 3.2f},
    {{44, 19, 44, 19}, 3.2f},
    {{44, 15, 44, 18}, 3.1f}, {{44, 11, 44, 14}, 3.5f}, {{44, 8, 44, 10}, 3.9f},
};

// ---------- Тёмные зоны (укрытия Физички, предупреждение HUD) ----------
inline constexpr CellRect kDark[] = {
    {5, 6, 15, 11}, {25, 5, 36, 9}, {31, 10, 36, 15}, {16, 31, 30, 39}, {34, 27, 45, 37},
    {40, 38, 42, 39}, {38, 20, 46, 22}, {47, 19, 50, 23}, {44, 19, 44, 19},
};
// Освещённость по умолчанию (0..1): магистраль освещена, служебные коридоры тусклые, тёмные зоны почти чёрные.
constexpr float LIGHT_DARK = 0.045f, LIGHT_TRUNK = 0.22f, LIGHT_CORRIDOR = 0.09f, LIGHT_PASSAGE = 0.15f, LIGHT_MAX = 0.72f;
constexpr float DARK_THRESHOLD = 0.12f;

struct Lamp { float x, z, radius, intensity; };
inline constexpr Lamp kLamps[] = {
    {9, 43, 7.0f, 0.26f}, {20, 43, 6.5f, 0.27f}, {34, 43, 6.5f, 0.27f}, {48, 43, 6.0f, 0.14f},
    {76, 43, 6.0f, 0.20f}, {86, 43, 5.0f, 0.10f},
    {89.0f, 39.2f, 5.2f, 0.42f},                                  // аварийная лампа над нишей
    {22, 18, 5.5f, 0.10f}, {54, 27, 5.5f, 0.24f}, {62, 14, 6.0f, 0.09f},
    {80, 60, 7.0f, 0.10f}, {46, 70, 7.0f, 0.10f}, {98, 43, 5.0f, 0.05f},
    {21, 34, 4.5f, 0.08f}, {43, 54, 4.5f, 0.08f},
    {89.0f, 33.5f, 6.0f, 0.30f}, {89.0f, 26.5f, 6.0f, 0.30f}, {89.0f, 21.5f, 4.5f, 0.18f},   // приглушённый белый свет прохода
};

// ---------- Пол: платформы (метры; последняя подходящая перекрывает предыдущие) ----------
struct Platform { Rect r; float h; };
inline constexpr Platform kPlatforms[] = {
    {{10, 12, 32, 24}, -0.51f},     // насосная: пол ниже магистрали
    {{32, 62, 62, 80}, -0.51f},     // южный зал
    {{68, 54, 92, 76}, 0.34f},      // генераторный зал приподнят
    {{80, 76, 86, 80}, 0.34f},      // его ниша
};
// Марш: ступени, идущие вдоль оси. Каждая ступень выше предыдущей на rise; нижняя кромка i-й ступени лежит на edge + dir*run*i.
struct Flight {
    char axis;            // 'x' или 'z' - ось движения
    int dir;              // +1 / -1
    float edge;           // координата начала первой ступени
    float laneMin, laneMax;   // ширина марша по другой оси
    float endCoord;       // где заканчивается область ступеней (на этой оси)
    int steps; float rise, run, baseH;
};
constexpr float STEP_RISE = 0.17f;      // подъём ступени (меньше порога движения 0.24 м)
constexpr float STEP_RUN = 0.30f;       // глубина ступени
inline constexpr Flight kFlights[] = {
    {'z', -1, 26.0f, 20, 22, 24.0f, 3, -STEP_RISE, STEP_RUN, 0.0f},        // спуск в насосную
    {'x', +1, 32.0f, 18, 20, 40.0f, 3, +STEP_RISE, STEP_RUN, -0.51f},      // насосная -> кабельный коридор
    {'z', +1, 60.0f, 42, 44, 62.0f, 3, -STEP_RISE, STEP_RUN, 0.0f},        // спуск в южный зал
    {'x', +1, 62.0f, 70, 72, 66.0f, 3, +STEP_RISE, STEP_RUN, -0.51f},      // южный зал -> переход
    {'x', +1, 66.0f, 70, 72, 68.0f, 2, +STEP_RISE, STEP_RUN, 0.0f},        // переход -> генераторный зал
    {'z', +1, 52.4f, 78, 80, 54.0f, 2, +STEP_RISE, STEP_RUN, 0.0f},        // магистраль -> генераторный зал
    // Скрытый проход за аварийной дверью: три коротких марша по три ступени, между ними площадки.
    {'z', -1, 31.2f, 88, 90, 16.0f, 3, +STEP_RISE, STEP_RUN, 0.0f},
    {'z', -1, 24.6f, 88, 90, 16.0f, 3, +STEP_RISE, STEP_RUN, 3 * STEP_RISE},
    {'z', -1, 18.9f, 88, 90, 16.0f, 3, +STEP_RISE, STEP_RUN, 6 * STEP_RISE},
};

// ---------- Аварийная дверь и скрытый проход ----------
constexpr float PASSAGE_CX = 89.0f;                // ось прохода и двери (клетка 44)
constexpr float PASSAGE_X0 = 88.0f, PASSAGE_X1 = 90.0f;
constexpr float PASSAGE_LINER = 0.25f;             // облицовка стен прохода: тесный просвет 1.5 м
constexpr float FIRE_DOOR_Z = 38.0f;               // плоскость двери (граница ниши и прохода)
constexpr float FINAL_DOOR_Z = 16.05f;             // плоскость конечной двери
constexpr float DOOR_W = 0.93f, DOOR_H = 2.10f, DOOR_T = 0.06f;      // створка; проём шире на 2 см (зазоры)
constexpr float DOOR_OPEN_W = DOOR_W + 0.02f;
constexpr float DOOR_HINGE_X = PASSAGE_CX - DOOR_W * 0.5f;     // петли на западной стороне
constexpr float DOOR_FRAME_T = 0.40f;              // толщина коробки (по Z)
constexpr float NICHE_X0 = 88.4f, NICHE_X1 = 89.6f; // ниша между пилястрами: 1.2 м
constexpr float DOOR_OPEN_TIME = 0.9f;             // секунд на полное открытие
constexpr float FINAL_DOOR_OPEN_TIME = 0.85f;
constexpr float DOOR_OPEN_ANGLE = 1.66f;           // ~95 град
constexpr float FIRE_DOOR_USE_RADIUS = 2.15f;
constexpr float FINAL_DOOR_USE_RADIUS = 2.35f;
constexpr float USE_MIN_FACING_COS = 0.55f;        // смотреть на дверь: не более ~57 град от направления взгляда
constexpr float FINAL_RECESS_DEPTH = 1.10f;

// Голубая лампа в конце прохода: локальный источник в шейдере. Свет ограничен объёмом прохода.
constexpr float BLUE_LAMP_X = PASSAGE_CX, BLUE_LAMP_Y = 3.45f, BLUE_LAMP_Z = 19.4f;
constexpr float BLUE_LAMP_RADIUS = 7.0f;
constexpr float BLUE_LAMP_COLOR[3] = {0.07f, 0.26f, 0.72f};
constexpr float BLUE_LIGHT_BOX[4] = {PASSAGE_X0, PASSAGE_X1, 15.0f, FIRE_DOOR_Z - 0.2f};   // xMin, xMax, zMin, zMax

// ---------- Стрелки из плесени ----------
enum class Side : uint8_t { N, S, W, E };   // стена открытой клетки: N = стена у z-края с меньшим z, нормаль +Z
struct ArrowDef { int cx, cz; Side side; int dirX, dirZ; };   // dir - единичное направление острия в мире (по оси X или Z)
inline constexpr ArrowDef kArrows[] = {
    {8, 22, Side::S, +1, 0},  {16, 20, Side::N, +1, 0},  {23, 22, Side::S, +1, 0},  {29, 20, Side::N, +1, 0},
    {37, 22, Side::S, +1, 0}, {41, 20, Side::N, +1, 0},  {45, 22, Side::S, +1, 0},
    {21, 26, Side::W, 0, +1},                                     // ложный указатель: в сторону южного зала
};
constexpr float ARROW_Y0 = 0.90f, ARROW_Y1 = 2.50f;               // 1.6 м: центр на высоте взгляда игрока
constexpr float DADO_H = 0.90f;                                   // окрашенная нижняя полоса стен

// ---------- Предметы: фиксированные клетки (центры клеток) ----------
enum Level2ItemId { ITEM_PUMP_WATER, ITEM_SWITCH_BATTERY, ITEM_CLOSET_MEDKIT, ITEM_SOUTH_BATTERY,
                    ITEM_GEN_MEDKIT, ITEM_TRUNK_WATER, ITEM_PASSAGE_BATTERY, ITEM_LOBBY_BATTERY, ITEM_COUNT };
struct ItemDef { int id; ItemType type; int cx, cz; };
inline constexpr ItemDef kItems[ITEM_COUNT] = {
    {ITEM_PUMP_WATER, IT_WATER, 9, 8},
    {ITEM_SWITCH_BATTERY, IT_BATTERY, 33, 7},
    {ITEM_CLOSET_MEDKIT, IT_MEDKIT, 26, 14},
    {ITEM_SOUTH_BATTERY, IT_BATTERY, 24, 36},
    {ITEM_GEN_MEDKIT, IT_MEDKIT, 40, 32},
    {ITEM_TRUNK_WATER, IT_WATER, 27, 21},
    {ITEM_PASSAGE_BATTERY, IT_BATTERY, 44, 12},
    {ITEM_LOBBY_BATTERY, IT_BATTERY, 3, 22},
};

// ---------- Укрытия Физички (центры клеток) ----------
inline constexpr int kHideSpots[][2] = {
    {8, 9}, {32, 7}, {23, 35}, {40, 31}, {48, 21}, {26, 7}, {41, 38}, {35, 13},
};
constexpr int HIDE_COUNT = (int)(sizeof(kHideSpots) / sizeof(kHideSpots[0]));

// ---------- Старт ----------
constexpr float START_X = 9.0f, START_Z = 42.0f;
constexpr float START_YAW = 1.5707963f;            // лицом на восток, вдоль магистрали

// ---------- Коллизии и игрок ----------
constexpr float MAX_STEP_UP = 0.24f;               // максимальный перепад пола за шаг (как в оригинале)
constexpr float BODY_BOTTOM = 0.22f, BODY_TOP = 1.75f;   // вертикальный диапазон тела: преграды вне него не блокируют
constexpr float CAMERA_EASE = 14.0f;               // сглаживание подъёма камеры по ступеням (1/с)

} // namespace l2
