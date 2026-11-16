// map_gen.cpp - генерация мира по хешу координат. Чистая функция GenSolid(x,y) + кэш чанков 16x16.
// Структура: решётка комнат с периодом 4 клетки (3x3 внутри + стены/столбы по краям).
// Каждый сегмент стены: глухой / с дверью в 1 клетку / открытый. Блоки 3x3 комнат иногда - "залы" без стен.
#include "map_gen.h"
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>
#include <algorithm>

static std::unordered_map<uint64_t, Chunk> gChunks;
static std::unordered_set<uint64_t> gTaken;   // подобранные предметы (переживают выгрузку чанков)
static Chunk* gLast = nullptr;

static uint64_t Key(int a, int b) { return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; }

// обе комнаты лежат в одном блоке 3x3, который является залом?
static bool SameHall(int rx1, int ry1, int rx2, int ry2) {
    int bx = FloorDiv(rx1, 3), by = FloorDiv(ry1, 3);
    if (bx != FloorDiv(rx2, 3) || by != FloorDiv(ry2, 3)) return false;
    return Rand01(bx, by, 11) < 0.18f;
}

// клетка k (0..2) сегмента стены между комнатами; horiz - сегмент вдоль X
static bool WallSegment(int rx, int ry, int k, bool horiz, bool hallInside) {
    if (hallInside) return false;
    bool start = horiz ? (rx == 0 && (ry == 0 || ry == 1)) : (ry == 0 && (rx == 0 || rx == 1));
    if (start) return k != 1;                       // у стартовой комнаты всегда есть 4 двери
    float r = Rand01(rx, ry, horiz ? 1 : 2);
    if (r < 0.40f) return true;                     // глухая стена
    if (r < 0.80f) return k != (int)(Hash2(rx, ry, horiz ? 5 : 6) % 3);   // дверь в одну клетку
    return false;                                   // проём на всю ширину
}

static bool GenSolid(int x, int y) {
    // Фиксированная тёмная комната: room (1,0), внутренние клетки x=5..7, z=1..3.
    // Периметр закрыт, кроме одного входа со стороны стартовой комнаты (4,2).
    if (x >= 4 && x <= 8 && y >= 0 && y <= 4) {
        if (x == 4 && y == 2) return false;
        return x == 4 || x == 8 || y == 0 || y == 4;
    }
    int mx = PosMod(x, 4), my = PosMod(y, 4), rx = FloorDiv(x, 4), ry = FloorDiv(y, 4);
    if (mx == 0 && my == 0) {                       // столб на углу четырёх комнат
        bool hall = SameHall(rx - 1, ry - 1, rx, ry - 1) && SameHall(rx - 1, ry, rx, ry) && SameHall(rx - 1, ry - 1, rx - 1, ry);
        return hall ? Rand01(rx, ry, 7) < 0.35f : true;
    }
    if (my == 0) return WallSegment(rx, ry, mx - 1, true,  SameHall(rx, ry - 1, rx, ry));
    if (mx == 0) return WallSegment(rx, ry, my - 1, false, SameHall(rx - 1, ry, rx, ry));
    if (mx == 2 && my == 2 && !(rx == 0 && ry == 0)) return Rand01(rx, ry, 3) < 0.10f;   // колонна в центре комнаты
    return false;
}

// люк-выход: детерминирован по чанку, всегда в центре комнаты
static bool ExitOfChunk(int chx, int chz, int* ex, int* ez) {
    if (abs(chx) <= 1 && abs(chz) <= 1) return false;
    if (Rand01(chx, chz, 31) >= 0.04f) return false;
    uint32_t h = Hash2(chx, chz, 32);
    *ex = chx * CHUNK + (int)(h % 4) * 4 + 2;
    *ez = chz * CHUNK + (int)((h >> 4) % 4) * 4 + 2;
    return true;
}

static Chunk& MakeChunk(int chx, int chz) {
    Chunk& c = gChunks[Key(chx, chz)];
    c.x = chx; c.z = chz; c.items.clear();
    for (int z = 0; z < CHUNK; z++)
        for (int x = 0; x < CHUNK; x++) c.solid[z][x] = GenSolid(chx * CHUNK + x, chz * CHUNK + z);
    c.hasExit = ExitOfChunk(chx, chz, &c.exitX, &c.exitZ);
    if (c.hasExit) c.solid[c.exitZ - chz * CHUNK][c.exitX - chx * CHUNK] = false;
    for (int z = 0; z < CHUNK; z++)
        for (int x = 0; x < CHUNK; x++) {
            if (c.solid[z][x]) continue;
            int gx = chx * CHUNK + x, gz = chz * CHUNK + z;
            if (gx >= 5 && gx <= 7 && gz >= 1 && gz <= 3) continue;     // в тёмной комнате только фиксированные аптечки
            if (abs(gx - 2) + abs(gz - 2) < 6) continue;                 // не у точки старта
            if (c.hasExit && gx == c.exitX && gz == c.exitZ) continue;
            float r = Rand01(gx, gz, 21);
            ItemType t;
            if (r < 0.0040f) t = IT_WATER; else if (r < 0.0075f) t = IT_BATTERY; else if (r < 0.0095f) t = IT_MEDKIT; else continue;
            if (gTaken.count(Key(gx, gz))) continue;
            c.items.push_back({gx, gz, t, false});
        }
    // Три гарантированные аптечки в комнате с Гончей. gTaken сохраняет подбор
    // даже после выгрузки и повторной генерации чанка.
    const int medkitCells[3][2] = {{5, 2}, {6, 1}, {7, 2}};
    for (const auto& cell : medkitCells) {
        int gx = cell[0], gz = cell[1];
        if (FloorDiv(gx, CHUNK) == chx && FloorDiv(gz, CHUNK) == chz && !gTaken.count(Key(gx, gz))) {
            c.items.push_back({gx, gz, IT_MEDKIT, false});
        }
    }
    return c;
}

Chunk& GetChunk(int chx, int chz) {
    auto it = gChunks.find(Key(chx, chz));
    return it != gChunks.end() ? it->second : MakeChunk(chx, chz);
}

bool IsSolid(int cx, int cz) {
    int chx = FloorDiv(cx, CHUNK), chz = FloorDiv(cz, CHUNK);
    Chunk* c = gLast;
    if (!c || c->x != chx || c->z != chz) { c = &GetChunk(chx, chz); gLast = c; }
    return c->solid[cz - chz * CHUNK][cx - chx * CHUNK];
}

bool CircleHits(float x, float z, float r) {
    int x0 = CellOf(x - r), x1 = CellOf(x + r), z0 = CellOf(z - r), z1 = CellOf(z + r);
    for (int j = z0; j <= z1; j++) for (int i = x0; i <= x1; i++) if (IsSolid(i, j)) return true;
    return false;
}

bool LineClear(Vector2 a, Vector2 b) {
    float dx = b.x - a.x, dz = b.y - a.y, d = sqrtf(dx * dx + dz * dz);
    int n = (int)(d / 0.5f) + 1;
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        if (IsSolid(CellOf(a.x + dx * t), CellOf(a.y + dz * t))) return false;
    }
    return true;
}

// Лампа в центре каждой комнаты: целые сектора 5x5 комнат иногда обесточены ("тёмные зоны")
float LampLevel(int rx, int ry) {
    if (rx == 1 && ry == 0) return 0.0f; // лампа в комнате с Гончей отсутствует
    if (rx == 0 && ry == 0) return 1.0f;
    int sx = FloorDiv(rx, 5), sy = FloorDiv(ry, 5);
    if (!(sx == 0 && sy == 0) && Rand01(sx, sy, 42) < 0.20f) return 0.0f;
    float r = Rand01(rx, ry, 41);
    if (r < 0.10f) return 0.0f;     // перегоревшая лампа
    if (r < 0.27f) return 0.38f;    // заметно тусклая комната
    return 1.0f;                    // нормально освещённая комната
}

// Билинейная интерполяция яркости четырёх ближайших ламп -> плавные градиенты света и тьмы
float LightAt(float wx, float wz) {
    int cx = CellOf(wx), cz = CellOf(wz);
    if (cx >= 5 && cx <= 7 && cz >= 1 && cz <= 3) return 0.07f; // постоянная темнота внутри комнаты
    float u = (wx / CELL - 2.5f) / 4.0f, v = (wz / CELL - 2.5f) / 4.0f;
    int i = (int)floorf(u), j = (int)floorf(v);
    float tx = u - i, ty = v - j;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    float a = LampLevel(i, j), b = LampLevel(i + 1, j), c = LampLevel(i, j + 1), d = LampLevel(i + 1, j + 1);
    float l = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
    return 0.07f + 0.93f * l;
}

void MapUpdate(float px, float pz, int keepChunks) {
    // Оставляем радиус прорисовки плюс один буферный чанк для предметов и границ.
    keepChunks = std::max(2, std::min(6, keepChunks));
    const size_t maxCached = (size_t)(2 * keepChunks + 1) * (size_t)(2 * keepChunks + 1) + 16;
    if (gChunks.size() <= maxCached) return;
    int pcx = FloorDiv(CellOf(px), CHUNK), pcz = FloorDiv(CellOf(pz), CHUNK);
    for (auto it = gChunks.begin(); it != gChunks.end();) {
        if (abs(it->second.x - pcx) > keepChunks || abs(it->second.z - pcz) > keepChunks) it = gChunks.erase(it); else ++it;
    }
    gLast = nullptr;
}

void MapReset(uint32_t seed) { gSeed = seed; gChunks.clear(); gTaken.clear(); gLast = nullptr; }

bool IsExitCell(int cx, int cz) {
    int ex, ez;
    return ExitOfChunk(FloorDiv(cx, CHUNK), FloorDiv(cz, CHUNK), &ex, &ez) && ex == cx && ez == cz;
}

bool NearestExit(float px, float pz, Vector2* out) {
    int pcx = FloorDiv(CellOf(px), CHUNK), pcz = FloorDiv(CellOf(pz), CHUNK);
    float best = 1e9f; bool found = false;
    for (int j = -5; j <= 5; j++) for (int i = -5; i <= 5; i++) {
        int ex, ez;
        if (!ExitOfChunk(pcx + i, pcz + j, &ex, &ez)) continue;
        Vector2 p = {(ex + 0.5f) * CELL, (ez + 0.5f) * CELL};
        float d = hypotf(p.x - px, p.y - pz);
        if (d < best) { best = d; *out = p; found = true; }
    }
    return found;
}

Item* ItemNear(float wx, float wz, float r) {
    int pcx = FloorDiv(CellOf(wx), CHUNK), pcz = FloorDiv(CellOf(wz), CHUNK);
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++)
        for (Item& it : GetChunk(pcx + i, pcz + j).items) {
            if (it.taken) continue;
            if (hypotf((it.cx + 0.5f) * CELL - wx, (it.cz + 0.5f) * CELL - wz) < r) return &it;
        }
    return nullptr;
}

void TakeItem(Item* it) { it->taken = true; gTaken.insert(Key(it->cx, it->cz)); }
