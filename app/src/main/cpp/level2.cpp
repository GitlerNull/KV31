// level2.cpp - Level 2: заброшенный технический комплекс (бетонные служебные тоннели, трубы, тесные помещения).
// Архитектура:
//   * планировка и все числа - level2_layout.h;
//   * геометрия строится ОДИН РАЗ в CPU-кэш (level2_geom.*), по тайлам 8x8 м; за кадр выводятся только тайлы в радиусе видимости;
//   * видимая геометрия и коллизии берутся из одних и тех же данных (карта клеток, платформы пола, список преград);
//   * двери - единственная динамическая геометрия (створка поворачивается на петлях).
#include "level2.h"
#include "level2_layout.h"
#include "level2_geom.h"
#include "util.h"
#include "map_gen.h"
#include "rlgl.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace l2;
using namespace l2g;

namespace {

struct Plat { Rect r; float h; bool step; };
struct Box3 { float x0, z0, x1, z1, y0, y1; };
struct Cyl3 { float x, z, r, y0, y1; };

bool gActive = false, gBuilt = false;
bool gOpenCell[MAP_H][MAP_W];
float gBase[MAP_H][MAP_W], gCeil[MAP_H][MAP_W];
bool gDarkCell[MAP_H][MAP_W];
std::vector<Plat> gPlat;
std::vector<Box3> gBoxes;
std::vector<Cyl3> gCyls;
Scene gScene, gDoors;
Texture2D gTexObj[TEX_COUNT];
Model gGenerator{};
bool gGeneratorLoaded = false;

bool gFireOpen = false;       float gFireAngle = 0.0f;
bool gFinalOpening = false;   float gFinalTimer = 0.0f, gFinalAngle = 0.0f;
bool gFirePromptShown = false, gFinalPromptShown = false, gTransitionSent = false;
std::vector<Item> gItems;

// Генератор: фиксированное место в генераторном зале (габариты модели взяты из glTF: 0.82 x 0.58 x 0.56 м до масштаба).
constexpr float GEN_X = 86.0f, GEN_Z = 66.0f, GEN_SCALE = 2.15f, GEN_YAW_DEG = 90.0f;
constexpr float GEN_HALF_X = 0.282f * GEN_SCALE + 0.05f, GEN_HALF_Z = 0.4145f * GEN_SCALE + 0.05f;

inline bool InMap(int cx, int cz) { return cx >= 0 && cz >= 0 && cx < MAP_W && cz < MAP_H; }
inline bool CellOpenAt(int cx, int cz) { return InMap(cx, cz) && gOpenCell[cz][cx]; }
inline float Wc(int c) { return c * CELL; }
inline int CellIdx(float w) { return (int)floorf(w / CELL); }
Rgb RgbOf(int r, int g, int b) { return Rgb{(uint8_t)r, (uint8_t)g, (uint8_t)b}; }

// ------------------------------------------------------------------ пол
float FloorH(float x, float z, bool* isStep = nullptr) {
    for (int i = (int)gPlat.size() - 1; i >= 0; --i) {
        const Plat& p = gPlat[i];
        if (x >= p.r.x0 && x <= p.r.x1 && z >= p.r.z0 && z <= p.r.z1) { if (isStep) *isStep = p.step; return p.h; }
    }
    if (isStep) *isStep = false;
    return 0.0f;
}

void BuildGrid() {
    std::memset(gOpenCell, 0, sizeof gOpenCell);
    std::memset(gDarkCell, 0, sizeof gDarkCell);
    for (const CellRect& r : kOpen) for (int z = r.z0; z <= r.z1; ++z) for (int x = r.x0; x <= r.x1; ++x) gOpenCell[z][x] = true;
    for (const CellRect& r : kSolid) for (int z = r.z0; z <= r.z1; ++z) for (int x = r.x0; x <= r.x1; ++x) gOpenCell[z][x] = false;
    for (int z = 0; z < MAP_H; ++z) for (int x = 0; x < MAP_W; ++x) { gCeil[z][x] = CEIL_DEFAULT; gBase[z][x] = LIGHT_CORRIDOR; }
    for (const CeilZone& c : kCeil) for (int z = c.r.z0; z <= c.r.z1; ++z) for (int x = c.r.x0; x <= c.r.x1; ++x) gCeil[z][x] = c.h;
    const CellRect trunkZones[] = {{2, 19, 6, 23}, {7, 20, 46, 22}, {47, 19, 50, 23}};
    for (const CellRect& r : trunkZones) for (int z = r.z0; z <= r.z1; ++z) for (int x = r.x0; x <= r.x1; ++x) gBase[z][x] = LIGHT_TRUNK;
    for (int z = 8; z <= 18; ++z) gBase[z][44] = LIGHT_PASSAGE;
    for (const CellRect& r : kDark) for (int z = r.z0; z <= r.z1; ++z) for (int x = r.x0; x <= r.x1; ++x) { gDarkCell[z][x] = true; gBase[z][x] = LIGHT_DARK; }

    gPlat.clear();
    for (const Platform& p : kPlatforms) gPlat.push_back({p.r, p.h, false});
    for (const Flight& f : kFlights) {
        for (int i = 0; i < f.steps; ++i) {
            const float start = f.edge + f.dir * f.run * i;
            const float h = f.baseH + f.rise * (i + 1);
            Rect r;
            if (f.axis == 'z') { r.x0 = f.laneMin; r.x1 = f.laneMax; r.z0 = std::min(start, f.endCoord); r.z1 = std::max(start, f.endCoord); }
            else               { r.z0 = f.laneMin; r.z1 = f.laneMax; r.x0 = std::min(start, f.endCoord); r.x1 = std::max(start, f.endCoord); }
            gPlat.push_back({r, h, true});
        }
    }
}

// ------------------------------------------------------------------ свет (запечённый в вершины; голубая лампа - в шейдере)
float BaseLightAt(float wx, float wz) {
    const int cx = CellIdx(wx), cz = CellIdx(wz);
    float b = 0.0f;
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {   // точки на стене принадлежат соседней открытой клетке
        if (dx && dz) continue;
        const int x = cx + dx, z = cz + dz;
        if (CellOpenAt(x, z)) b = std::max(b, gBase[z][x]);
    }
    return b;
}
float LightFnImpl(float wx, float wz) { return Level2LightAt(wx, wz); }

// ------------------------------------------------------------------ коллизии
void AddBoxCollider(float x0, float z0, float x1, float z1, float y0, float y1) { gBoxes.push_back({x0, z0, x1, z1, y0, y1}); }
void AddCylCollider(float x, float z, float r, float y0, float y1) { gCyls.push_back({x, z, r, y0, y1}); }

inline bool HitsBox(float x, float z, float r, const Box3& b) {
    const float cx = std::clamp(x, b.x0, b.x1), cz = std::clamp(z, b.z0, b.z1);
    const float dx = x - cx, dz = z - cz;
    return dx * dx + dz * dz < r * r;
}
// Створка двери: отрезок от петли; радиус учитывает толщину створки.
inline void DoorSegment(float hingeZ, float angle, float* x0, float* z0, float* x1, float* z1) {
    *x0 = DOOR_HINGE_X; *z0 = hingeZ;
    *x1 = DOOR_HINGE_X + cosf(angle) * DOOR_W; *z1 = hingeZ - sinf(angle) * DOOR_W;
}
inline bool HitsSegment(float x, float z, float r, float ax, float az, float bx, float bz) {
    const float dx = bx - ax, dz = bz - az, ll = dx * dx + dz * dz;
    float t = ll > 1e-6f ? ((x - ax) * dx + (z - az) * dz) / ll : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float qx = ax + dx * t - x, qz = az + dz * t - z;
    const float rr = r + DOOR_T * 0.5f + 0.02f;
    return qx * qx + qz * qz < rr * rr;
}

// ------------------------------------------------------------------ построители геометрии
// Вертикальная панель вдоль отрезка (ax,az)-(bx,bz) с нормалью лица n. right = up x n, поэтому u растёт слева направо
// для наблюдателя перед панелью, v растёт вниз - картинка и текст никогда не зеркалятся и не переворачиваются.
void VPanel(Scene& sc, Tex t, float ax, float az, float bx, float bz, float y0, float y1, V3 n, Rgb tint,
            float shade, float lightMin = 0.0f, const float* uv = nullptr, int tag = -1) {
    const V3 up{0, 1, 0};
    const V3 right = Norm(Cross(up, n));
    V3 A{ax, 0, az}, B{bx, 0, bz};
    if (Dot(A, right) > Dot(B, right)) std::swap(A, B);
    const float len = Dot(B - A, right);
    const V3 tl{A.x, y1, A.z};
    float u0 = Dot(A, right) / TEX_REPEAT_M, u1 = u0 + len / TEX_REPEAT_M;
    float v0 = -y1 / TEX_REPEAT_M, v1 = -y0 / TEX_REPEAT_M;
    if (uv) { u0 = uv[0]; v0 = uv[1]; u1 = uv[2]; v1 = uv[3]; }
    sc.Panel(t, tl, right * len, V3{0, -(y1 - y0), 0}, n, tint, shade, lightMin, u0, v0, u1, v1, tag);
}

const Rgb CONCRETE = {176, 174, 166}, DADO = {84, 100, 94}, FLOOR_TINT = {196, 194, 188}, FLOOR_WET = {138, 142, 140},
          CEIL_TINT = {150, 148, 142}, METAL_DARK = {82, 90, 92}, METAL_MID = {118, 124, 122}, RUST = {138, 92, 62},
          STEEL_PLATE = {112, 116, 114}, HAZARD = {176, 150, 48}, BLACK_RUBBER = {34, 36, 36}, PAINT_RED = {140, 40, 34};

void Equip(Tex t, float x0, float y0, float z0, float x1, float y1, float z1, Rgb tint, bool collide, uint8_t mask = 0x3F) {
    gScene.Box(t, x0, y0, z0, x1, y1, z1, tint, 1.0f, mask);
    if (collide) AddBoxCollider(x0, z0, x1, z1, y0, y1);
}
void StandPipe(float x, float z, float r, float y0, float y1, Rgb tint, bool collide) {
    gScene.Pipe(V3{x, y0, z}, V3{x, y1, z}, r, tint, true, true, 12);
    if (collide) AddCylCollider(x, z, r * 1.55f, y0, y1);
}
void AddLampFixture(float x, float y, float z, float w, float d, float ceilY, Rgb glow = Rgb{244, 238, 214}) {   // каркасный светильник: тело, излучающая панель, штанги
    gScene.Box(TEX_METAL, x - w * 0.5f - 0.03f, y - 0.03f, z - d * 0.5f - 0.03f, x + w * 0.5f + 0.03f, y + 0.07f, z + d * 0.5f + 0.03f, METAL_DARK);
    const V3 p[4] = {{x - w * 0.5f, y - 0.034f, z - d * 0.5f}, {x - w * 0.5f, y - 0.034f, z + d * 0.5f},
                     {x + w * 0.5f, y - 0.034f, z + d * 0.5f}, {x + w * 0.5f, y - 0.034f, z - d * 0.5f}};
    const float us[4] = {0, 0, 1, 1}, vs[4] = {0, 1, 1, 0};
    gScene.Quad(TEX_NONE, p, us, vs, glow, 1.0f, 1.0f);
    for (int sx = -1; sx <= 1; sx += 2)
        gScene.Box(TEX_METAL, x + sx * w * 0.42f - 0.01f, y + 0.07f, z - 0.01f, x + sx * w * 0.42f + 0.01f, ceilY, z + 0.01f, METAL_DARK);
}

// ------------------------------------------------------------------ геометрия карты: пол, потолок, стены
void EmitFloor(int cx, int cz) {
    const float x0 = Wc(cx), x1 = x0 + CELL, z0 = Wc(cz), z1 = z0 + CELL;
    std::vector<float> xs{x0, x1}, zs{z0, z1};
    for (const Plat& p : gPlat) {   // координатное сжатие: клетка режется по рёбрам платформ/ступеней, попадающим внутрь
        if (p.r.x1 <= x0 || p.r.x0 >= x1 || p.r.z1 <= z0 || p.r.z0 >= z1) continue;
        if (p.r.x0 > x0 + 1e-3f && p.r.x0 < x1 - 1e-3f) xs.push_back(p.r.x0);
        if (p.r.x1 > x0 + 1e-3f && p.r.x1 < x1 - 1e-3f) xs.push_back(p.r.x1);
        if (p.r.z0 > z0 + 1e-3f && p.r.z0 < z1 - 1e-3f) zs.push_back(p.r.z0);
        if (p.r.z1 > z0 + 1e-3f && p.r.z1 < z1 - 1e-3f) zs.push_back(p.r.z1);
    }
    std::sort(xs.begin(), xs.end()); std::sort(zs.begin(), zs.end());
    const bool wet = (cx >= 5 && cx <= 15 && cz >= 6 && cz <= 11) || (cx >= 16 && cx <= 30 && cz >= 31 && cz <= 39);
    for (size_t i = 0; i + 1 < xs.size(); ++i) for (size_t j = 0; j + 1 < zs.size(); ++j) {
        const float a0 = xs[i], a1 = xs[i + 1], b0 = zs[j], b1 = zs[j + 1];
        if (a1 - a0 < 1e-3f || b1 - b0 < 1e-3f) continue;
        bool step = false;
        const float h = FloorH((a0 + a1) * 0.5f, (b0 + b1) * 0.5f, &step);
        Tex t = TEX_CONCRETE_A; Rgb tint = wet ? FLOOR_WET : FLOOR_TINT;
        if (step) { t = TEX_METAL; tint = STEEL_PLATE; }
        float u0 = a0 / TEX_REPEAT_M, v0 = b0 / TEX_REPEAT_M, u1 = a1 / TEX_REPEAT_M, v1 = b1 / TEX_REPEAT_M;
        if ((cx == 44 && cz == 19) || (cx == 44 && cz == 20)) {   // клетки со следами: тайл на всю клетку
            t = (cz == 19) ? TEX_FLOOR_BLOOD_A : TEX_FLOOR_BLOOD_B; tint = FLOOR_TINT;
            u0 = (a0 - x0) / CELL; u1 = (a1 - x0) / CELL; v0 = (b0 - z0) / CELL; v1 = (b1 - z0) / CELL;
        }
        gScene.Plane(t, V3{a0, h, b0}, V3{a0, h, b1}, V3{a1, h, b1}, V3{a1, h, b0}, tint, 1.0f, u0, v0, u1, v1);
        // Рисеры: рисует ВЕРХНЯЯ сторона перепада (дубликатов нет), по пробам через 10 см вдоль кромки.
        struct Edge { float ax, az, bx, bz, nx, nz; };
        const Edge edges[4] = {{a0, b0, a1, b0, 0, -1}, {a0, b1, a1, b1, 0, 1}, {a0, b0, a0, b1, -1, 0}, {a1, b0, a1, b1, 1, 0}};
        for (const Edge& e : edges) {
            const float len = hypotf(e.bx - e.ax, e.bz - e.az);
            const int n = std::max(1, (int)ceilf(len / 0.1f));
            float runStart = 0, runH = 1e9f; bool inRun = false;
            auto flush = [&](float endT) {
                if (!inRun) return;
                VPanel(gScene, step ? TEX_CONCRETE_B : TEX_CONCRETE_A,
                       e.ax + (e.bx - e.ax) * runStart, e.az + (e.bz - e.az) * runStart,
                       e.ax + (e.bx - e.ax) * endT, e.az + (e.bz - e.az) * endT, runH, h, V3{e.nx, 0, e.nz},
                       step ? RgbOf(128, 126, 120) : RgbOf(150, 148, 142), 0.9f);
                inRun = false;
            };
            for (int k = 0; k < n; ++k) {
                const float t = (k + 0.5f) / n;
                const float px = e.ax + (e.bx - e.ax) * t + e.nx * 0.02f, pz = e.az + (e.bz - e.az) * t + e.nz * 0.02f;
                float hn = 1e9f;
                if (CellOpenAt(CellIdx(px), CellIdx(pz))) hn = FloorH(px, pz);
                const bool need = hn < h - 0.005f;
                if (need && inRun && fabsf(hn - runH) < 0.005f) continue;
                flush((float)k / n);
                if (need) { inRun = true; runStart = (float)k / n; runH = hn; }
            }
            flush(1.0f);
        }
    }
}

void EmitCeiling(int cx, int cz) {
    const float x0 = Wc(cx), x1 = x0 + CELL, z0 = Wc(cz), z1 = z0 + CELL, y = gCeil[cz][cx];
    gScene.Plane(TEX_CONCRETE_B, V3{x0, y, z0}, V3{x0, y, z1}, V3{x1, y, z1}, V3{x1, y, z0}, CEIL_TINT, 0.78f,
                 x0 / TEX_REPEAT_M, z0 / TEX_REPEAT_M, x1 / TEX_REPEAT_M, z1 / TEX_REPEAT_M);
}

int ArrowIndexFor(int cx, int cz, Side s) {
    for (size_t i = 0; i < sizeof(kArrows) / sizeof(kArrows[0]); ++i)
        if (kArrows[i].cx == cx && kArrows[i].cz == cz && kArrows[i].side == s) return (int)i;
    return -1;
}

void EmitWallSegment(int cx, int cz, Side s) {
    const float x0 = Wc(cx), x1 = x0 + CELL, z0 = Wc(cz), z1 = z0 + CELL;
    float ax, az, bx, bz, shade; V3 n;
    switch (s) {
        case Side::N: ax = x0; az = z0; bx = x1; bz = z0; n = {0, 0, 1};  shade = 1.0f;  break;
        case Side::S: ax = x0; az = z1; bx = x1; bz = z1; n = {0, 0, -1}; shade = 1.0f;  break;
        case Side::W: ax = x0; az = z0; bx = x0; bz = z1; n = {1, 0, 0};  shade = 0.85f; break;
        default:      ax = x1; az = z0; bx = x1; bz = z1; n = {-1, 0, 0}; shade = 0.85f; break;
    }
    if (cx == 44 && cz == 8 && s == Side::N) return;       // конечная дверь: стена с проёмом строится отдельно
    float yLow = 1e9f;
    for (int k = 0; k <= 4; ++k) {
        const float t = k / 4.0f;
        yLow = std::min(yLow, FloorH(ax + (bx - ax) * t + n.x * 0.1f, az + (bz - az) * t + n.z * 0.1f));
    }
    const float ceilY = gCeil[cz][cx];
    const int ai = ArrowIndexFor(cx, cz, s);
    const bool bloodBay = (cx == 43 && cz == 20 && s == Side::N);
    if (ai >= 0 || bloodBay) {
        VPanel(gScene, TEX_CONCRETE_B, ax, az, bx, bz, yLow, ARROW_Y0, n, DADO, shade);
        VPanel(gScene, TEX_CONCRETE_A, ax, az, bx, bz, ARROW_Y1, ceilY, n, CONCRETE, shade);
        int slot = ATLAS_WALL_BLOOD, tag = 1000;
        if (ai >= 0) {
            const ArrowDef& a = kArrows[ai];
            const V3 right = Norm(Cross(V3{0, 1, 0}, n));
            const bool toRight = (right.x * a.dirX + right.z * a.dirZ) > 0.0f;
            const int variant = ai % 2;       // два варианта мазка на направление (экономит время генерации)
            slot = toRight ? (ATLAS_ARROW_R0 + variant) : (ATLAS_ARROW_L0 + variant);
            tag = ai;
        }
        float uv[4]; AtlasSlotRect(slot, &uv[0], &uv[1], &uv[2], &uv[3]);
        // Плесень/следы запечены в текстуру самой стены: нет смещения от поверхности, нет z-fighting, направление однозначно.
        VPanel(gScene, TEX_ATLAS, ax, az, bx, bz, ARROW_Y0, ARROW_Y1, n, CONCRETE, shade, 0.0f, uv, tag);
        return;
    }
    VPanel(gScene, TEX_CONCRETE_B, ax, az, bx, bz, yLow, DADO_H, n, DADO, shade);
    VPanel(gScene, TEX_CONCRETE_A, ax, az, bx, bz, DADO_H, ceilY, n, CONCRETE, shade);
    const float off = 0.012f;   // кромка окрашенной полосы
    const float mx0 = std::min(ax, bx), mx1 = std::max(ax, bx), mz0 = std::min(az, bz), mz1 = std::max(az, bz);
    if (n.z != 0) gScene.Box(TEX_METAL, mx0, DADO_H - 0.02f, n.z > 0 ? az : az - off, mx1, DADO_H + 0.02f, n.z > 0 ? az + off : az, METAL_DARK, 1.0f, 0x3F);
    else          gScene.Box(TEX_METAL, n.x > 0 ? ax : ax - off, DADO_H - 0.02f, mz0, n.x > 0 ? ax + off : ax, DADO_H + 0.02f, mz1, METAL_DARK, 1.0f, 0x3F);
}

void EmitSoffit(int cx, int cz) {   // перепад потолка между соседними открытыми клетками
    const float x0 = Wc(cx), x1 = x0 + CELL, z0 = Wc(cz), z1 = z0 + CELL;
    struct N { int dx, dz; float ax, az, bx, bz; V3 n; };
    const N ns[4] = {{0, -1, x0, z0, x1, z0, {0, 0, 1}}, {0, 1, x0, z1, x1, z1, {0, 0, -1}},
                     {-1, 0, x0, z0, x0, z1, {1, 0, 0}}, {1, 0, x1, z0, x1, z1, {-1, 0, 0}}};
    for (const N& q : ns) {
        const int ox = cx + q.dx, oz = cz + q.dz;
        if (!CellOpenAt(ox, oz)) continue;
        const float c1 = gCeil[cz][cx], c2 = gCeil[oz][ox];
        if (c1 >= c2 - 0.001f) continue;
        // грань на границе, лицом в более высокое помещение (нормаль направлена в соседнюю клетку)
        VPanel(gScene, TEX_CONCRETE_B, q.ax, q.az, q.bx, q.bz, c1, c2, V3{(float)q.dx, 0, (float)q.dz}, RgbOf(120, 118, 112), 0.75f);
    }
}

// ------------------------------------------------------------------ оборудование и помещения
float CeilAtW(float x, float z) { const int cx = CellIdx(x), cz = CellIdx(z); return InMap(cx, cz) ? gCeil[cz][cx] : CEIL_DEFAULT; }

// Вертикальная труба от пола до потолка с автоматическими высотами.
void RiserPipe(float x, float z, float r, Rgb tint, bool collide = true) {
    StandPipe(x, z, r, FloorH(x, z), CeilAtW(x, z) - 0.02f, tint, collide);
}
// Горизонтальные подвесы труб: тонкие тяги к потолку через ~2.4 м.
void Hangers(float x0, float z0, float x1, float z1, float y, float ceilY) {
    const float len = hypotf(x1 - x0, z1 - z0);
    const int n = std::max(1, (int)(len / 2.4f));
    for (int i = 1; i <= n; ++i) {
        const float t = (float)i / (n + 1), x = x0 + (x1 - x0) * t, z = z0 + (z1 - z0) * t;
        gScene.Box(TEX_METAL, x - 0.012f, y + 0.08f, z - 0.012f, x + 0.012f, ceilY, z + 0.012f, METAL_DARK);
    }
}
// Шкаф: корпус, линии створок, ручки и индикаторы.
void CabinetRow(float x0, float z, float depth, int count, float width, float height, bool frontPlusZ, Rgb tintA, Rgb tintB) {
    for (int i = 0; i < count; ++i) {
        const float a = x0 + i * width, b = a + width - 0.03f;
        const Rgb tint = (i & 1) ? tintB : tintA;
        gScene.Box(TEX_METAL, a, 0.0f, z, b, height, z + depth, tint);
        const float fz = frontPlusZ ? z + depth : z;           // плоскость лицевой стороны
        const float s = frontPlusZ ? 1.0f : -1.0f;
        gScene.Box(TEX_METAL, a + 0.04f, 0.12f, fz, b - 0.04f, height - 0.12f, fz + 0.012f * s, RgbOf(tint.r - 14, tint.g - 14, tint.b - 14));
        gScene.Box(TEX_METAL, b - 0.14f, 1.0f, fz + 0.012f * s, b - 0.10f, 1.18f, fz + 0.045f * s, RgbOf(30, 32, 32));
        gScene.Box(TEX_NONE, a + 0.18f, 1.62f, fz + 0.012f * s, a + 0.24f, 1.67f, fz + 0.03f * s, (i % 5 == 2) ? RgbOf(170, 44, 36) : RgbOf(52, 150, 72), 1.0f, 0x3F);
    }
}

void BuildTrunkAndLobby() {
    const float zN = Wc(20), zS = Wc(23);        // стены магистрали: z=40 и z=46
    for (float x = 14.0f; x < 93.9f; x += 8.0f) {   // пучок труб вдоль северной стены: цилиндры, фланцы на стыках, хомуты
        const float xe = std::min(x + 8.0f, 94.0f);
        gScene.Pipe(V3{x, 3.05f, zN + 0.21f}, V3{xe, 3.05f, zN + 0.21f}, 0.13f, METAL_MID, x > 14.0f, xe < 94.0f);
        gScene.Pipe(V3{x + 0.3f, 3.45f, zN + 0.12f}, V3{xe - 0.3f, 3.45f, zN + 0.12f}, 0.075f, RUST, true, true, 8);
    }
    for (float x = 15.2f; x < 94.0f; x += 2.4f) {   // кронштейны: пластина на стене + лапа к трубе
        gScene.Box(TEX_METAL, x - 0.03f, 2.85f, zN, x + 0.03f, 3.6f, zN + 0.06f, METAL_DARK);
        gScene.Box(TEX_METAL, x - 0.02f, 2.88f, zN + 0.06f, x + 0.02f, 3.05f, zN + 0.12f, METAL_DARK);
    }
    // кабельный лоток по южной стене: дно, борта, перекладины, подвесы, жгут кабелей
    gScene.Box(TEX_METAL, 14.0f, 3.10f, zS - 0.46f, 94.0f, 3.14f, zS - 0.04f, METAL_DARK);
    gScene.Box(TEX_METAL, 14.0f, 3.14f, zS - 0.46f, 94.0f, 3.20f, zS - 0.43f, METAL_DARK);
    gScene.Box(TEX_METAL, 14.0f, 3.14f, zS - 0.07f, 94.0f, 3.20f, zS - 0.04f, METAL_DARK);
    for (float x = 14.6f; x < 94.0f; x += 1.2f) gScene.Box(TEX_METAL, x, 3.14f, zS - 0.43f, x + 0.03f, 3.17f, zS - 0.07f, METAL_MID);
    for (float x = 15.0f; x < 94.0f; x += 2.4f) gScene.Box(TEX_METAL, x - 0.02f, 2.6f, zS - 0.05f, x + 0.02f, 3.10f, zS, METAL_DARK);
    gScene.Box(TEX_NONE, 14.0f, 3.14f, zS - 0.40f, 94.0f, 3.19f, zS - 0.10f, BLACK_RUBBER, 1.0f, 0x04);
    // колонны: прижаты к стене, нижняя полоса окрашена, плита на полу; коллизия совпадает с видимым объёмом
    const float northCols[] = {30, 46, 62, 78}, southCols[] = {24, 50, 70, 86};
    for (float x : northCols) {
        Equip(TEX_CONCRETE_A, x - 0.28f, 0, zN, x + 0.28f, 3.85f, zN + 0.56f, CONCRETE, true);
        gScene.Box(TEX_CONCRETE_B, x - 0.29f, 0, zN, x + 0.29f, 1.1f, zN + 0.57f, HAZARD);
        gScene.Box(TEX_METAL, x - 0.34f, 0, zN, x + 0.34f, 0.04f, zN + 0.62f, METAL_DARK);
    }
    for (float x : southCols) {
        Equip(TEX_CONCRETE_A, x - 0.28f, 0, zS - 0.56f, x + 0.28f, 3.85f, zS, CONCRETE, true);
        gScene.Box(TEX_CONCRETE_B, x - 0.29f, 0, zS - 0.57f, x + 0.29f, 1.1f, zS, HAZARD);
        gScene.Box(TEX_METAL, x - 0.34f, 0, zS - 0.62f, x + 0.34f, 0.04f, zS, METAL_DARK);
    }
    const float panelsN[] = {26, 52, 82}, panelsS[] = {36, 66};   // щитки на стенах
    for (float x : panelsN) {
        gScene.Box(TEX_METAL, x - 0.30f, 1.15f, zN, x + 0.30f, 1.95f, zN + 0.16f, RgbOf(96, 106, 100));
        gScene.Box(TEX_NONE, x - 0.04f, 1.75f, zN + 0.16f, x + 0.04f, 1.80f, zN + 0.175f, RgbOf(150, 40, 34));
        gScene.Box(TEX_NONE, x + 0.08f, 1.75f, zN + 0.16f, x + 0.16f, 1.80f, zN + 0.175f, RgbOf(60, 150, 70));
    }
    for (float x : panelsS) gScene.Box(TEX_METAL, x - 0.30f, 1.15f, zS - 0.16f, x + 0.30f, 1.95f, zS, RgbOf(96, 106, 100));
    const float lampsX[] = {20, 34, 48, 76, 86};   // светильники магистрали (на x=62 лампа перегорела: оставлен только корпус)
    for (float x : lampsX) AddLampFixture(x, 3.55f, 43.0f, 1.2f, 0.25f, 3.85f);
    gScene.Box(TEX_METAL, 62 - 0.63f, 3.52f, 43 - 0.155f, 62 + 0.63f, 3.62f, 43 + 0.155f, METAL_DARK);
    gScene.Box(TEX_METAL, 37.65f, 0.0f, 42.65f, 38.35f, 0.012f, 43.35f, METAL_DARK);   // решётка водостока

    // вестибюль: шкафчики, скамья, стояк, огнетушитель
    Equip(TEX_METAL, 4.0f, 0, 40.4f, 4.55f, 1.95f, 45.6f, RgbOf(92, 110, 104), true);
    for (int i = 1; i < 5; ++i) gScene.Box(TEX_NONE, 4.55f, 0.1f, 40.4f + i * 1.04f, 4.565f, 1.9f, 40.43f + i * 1.04f, RgbOf(30, 34, 34));
    for (int i = 0; i < 5; ++i) gScene.Box(TEX_METAL, 4.55f, 1.0f, 40.55f + i * 1.04f + 0.7f, 4.6f, 1.12f, 40.55f + i * 1.04f + 0.78f, RgbOf(26, 28, 28));
    Equip(TEX_METAL, 7.6f, 0, 46.9f, 9.6f, 0.45f, 47.35f, RgbOf(100, 78, 54), true);
    gScene.Box(TEX_METAL, 7.7f, 0, 47.0f, 7.8f, 0.45f, 47.1f, METAL_DARK); gScene.Box(TEX_METAL, 9.4f, 0, 47.0f, 9.5f, 0.45f, 47.1f, METAL_DARK);
    StandPipe(13.4f, 47.4f, 0.12f, 0.0f, 3.83f, METAL_MID, true);
    gScene.Cylinder(TEX_METAL, V3{11.0f, 0.4f, 47.88f}, V3{11.0f, 0.9f, 47.88f}, 0.07f, 10, PAINT_RED);
    gScene.Box(TEX_METAL, 10.97f, 0.9f, 47.83f, 11.03f, 1.0f, 47.93f, METAL_DARK);
    AddLampFixture(9.0f, 3.65f, 43.0f, 1.2f, 0.25f, 3.85f);
}

void BuildCorridors() {
    // спуск в насосную (N1): две тумбы сужают проход до 1.1 м, трубка вдоль стены
    Equip(TEX_METAL, 20.0f, 0, 31.0f, 20.45f, 1.9f, 32.2f, RgbOf(88, 98, 94), true);
    Equip(TEX_METAL, 21.55f, 0, 31.0f, 22.0f, 1.9f, 32.2f, RgbOf(88, 98, 94), true);
    gScene.Pipe(V3{20.07f, 2.4f, 24.2f}, V3{20.07f, 2.4f, 39.8f}, 0.045f, RUST, true, true, 8);
    AddLampFixture(21.0f, 2.8f, 34.0f, 0.7f, 0.2f, 3.0f);
    // спуск на юг (S1)
    gScene.Pipe(V3{43.93f, 2.5f, 46.2f}, V3{43.93f, 2.5f, 61.8f}, 0.06f, METAL_MID, true, true, 8);
    gScene.Pipe(V3{42.07f, 2.1f, 46.2f}, V3{42.07f, 2.1f, 59.8f}, 0.05f, PAINT_RED, true, true, 8);
    Equip(TEX_METAL, 42.0f, 0, 53.0f, 42.4f, 1.7f, 54.0f, RgbOf(100, 96, 70), true);   // узкое место
    AddLampFixture(43.0f, 2.8f, 54.0f, 0.7f, 0.2f, 3.0f);
    // кабельный коридор с изломами: трубы у стен, подвесы
    gScene.Pipe(V3{32.1f, 2.3f, 18.15f}, V3{39.9f, 2.3f, 18.15f}, 0.09f, METAL_MID, true, true);
    gScene.Pipe(V3{38.15f, 2.3f, 20.1f}, V3{38.15f, 2.3f, 27.9f}, 0.09f, RUST, true, true);
    gScene.Pipe(V3{40.1f, 2.3f, 26.15f}, V3{49.9f, 2.3f, 26.15f}, 0.09f, METAL_MID, true, true);
    AddLampFixture(36.0f, 2.5f, 19.0f, 0.7f, 0.2f, 2.7f);
    // ниши обслуживания: вентиль в нише, щиток
    gScene.Box(TEX_METAL, 28.2f, 0.9f, 47.7f, 29.8f, 1.8f, 48.0f, RgbOf(94, 104, 98));
    gScene.Cylinder(TEX_METAL, V3{28.6f, 1.1f, 47.0f}, V3{28.6f, 1.14f, 47.0f}, 0.12f, 10, PAINT_RED);
    gScene.Box(TEX_METAL, 62.2f, 1.0f, 47.6f, 63.8f, 1.9f, 48.0f, RgbOf(88, 98, 94));
    gScene.Box(TEX_METAL, 56.2f, 1.1f, 38.0f, 57.8f, 1.9f, 38.3f, RgbOf(96, 104, 98));
}

void BuildPumpRoom() {
    const float xs[] = {14.5f, 18.5f, 27.5f}, zc = 21.2f, zEnd[] = {12.5f, 12.5f, 16.4f};
    const float fy = -0.51f;
    for (int i = 0; i < 3; ++i) {
        const float x = xs[i];
        gScene.Box(TEX_CONCRETE_A, x - 0.9f, fy, zc - 0.8f, x + 0.9f, fy + 0.18f, zc + 0.8f, RgbOf(150, 148, 142));             // плита
        Equip(TEX_METAL, x - 0.65f, fy + 0.18f, zc - 0.6f, x + 0.65f, fy + 0.95f, zc + 0.6f, RgbOf(66, 92, 108), true);         // корпус насоса
        gScene.Cylinder(TEX_METAL, V3{x - 0.95f, fy + 0.62f, zc}, V3{x - 0.62f, fy + 0.62f, zc}, 0.26f, 12, RgbOf(66, 88, 72));  // двигатель
        gScene.Cylinder(TEX_METAL, V3{x - 0.62f, fy + 0.62f, zc}, V3{x - 0.5f, fy + 0.62f, zc}, 0.33f, 12, METAL_DARK);          // муфта
        AddBoxCollider(x - 0.98f, zc - 0.3f, x - 0.65f, zc + 0.3f, fy + 0.3f, fy + 0.95f);
        gScene.Pipe(V3{x + 0.2f, fy + 0.95f, zc}, V3{x + 0.2f, 2.4f, zc}, 0.08f, METAL_MID, true, false);
        gScene.Pipe(V3{x + 0.2f, 2.4f, zc}, V3{x + 0.2f, 2.4f, zEnd[i]}, 0.08f, METAL_MID, false, true);
        Hangers(x + 0.2f, zc, x + 0.2f, zEnd[i], 2.4f, 3.4f);
    }
    const float risersX[] = {13.0f, 16.8f, 25.0f, 29.5f};
    for (float x : risersX) RiserPipe(x, 23.55f, 0.14f, RUST);
    gScene.Pipe(V3{14.3f, -0.06f, 12.42f}, V3{25.7f, -0.06f, 12.42f}, 0.10f, METAL_MID, true, true);          // низкий коллектор
    AddBoxCollider(14.2f, 12.28f, 25.8f, 12.56f, -0.2f, 0.04f);
    const float valves[] = {17.0f, 21.0f, 24.0f};
    for (float x : valves) {
        gScene.Box(TEX_METAL, x - 0.1f, -0.2f, 12.32f, x + 0.1f, 0.05f, 12.52f, RUST);
        gScene.Cylinder(TEX_METAL, V3{x, 0.05f, 12.42f}, V3{x, 0.2f, 12.42f}, 0.015f, 6, METAL_DARK);
        gScene.Cylinder(TEX_METAL, V3{x, 0.2f, 12.42f}, V3{x, 0.23f, 12.42f}, 0.12f, 10, PAINT_RED);
    }
    gScene.Box(TEX_METAL, 15.7f, fy, 16.7f, 16.3f, fy + 0.012f, 17.3f, METAL_DARK);                               // решётка стока
    gScene.Pipe(V3{10.06f, 3.05f, 17.0f}, V3{21.94f, 3.05f, 17.0f}, 0.11f, METAL_MID, true, true);               // паропровод к перегородке
    gScene.Pipe(V3{24.06f, 3.05f, 17.0f}, V3{31.94f, 3.05f, 17.0f}, 0.11f, METAL_MID, true, true);
    Hangers(10.06f, 17.0f, 21.94f, 17.0f, 3.05f, 3.4f); Hangers(24.06f, 17.0f, 31.94f, 17.0f, 3.05f, 3.4f);
    AddLampFixture(22.0f, 3.2f, 18.0f, 0.8f, 0.2f, 3.4f);
}

void BuildSwitchgear() {
    const Rgb ca = RgbOf(92, 100, 102), cb = RgbOf(84, 96, 98);
    CabinetRow(52.0f, 10.0f, 0.6f, 20, 1.0f, 2.1f, true, ca, cb);
    AddBoxCollider(52.0f, 10.0f, 72.0f, 10.65f, 0, 2.1f);
    CabinetRow(54.0f, 13.6f, 1.6f, 10, 1.0f, 2.1f, true, cb, ca);                    // двойной ряд посередине зала
    AddBoxCollider(54.0f, 13.6f, 64.0f, 15.2f, 0, 2.1f);
    gScene.Box(TEX_METAL, 52.0f, 2.35f, 10.1f, 72.0f, 2.40f, 10.6f, METAL_DARK);         // лоток над рядом
    gScene.Box(TEX_METAL, 54.0f, 2.55f, 14.0f, 64.0f, 2.60f, 14.8f, METAL_DARK);
    for (int i = 0; i < 20; ++i) gScene.Box(TEX_NONE, 52.45f + i, 2.1f, 10.3f, 52.55f + i, 2.35f, 10.4f, BLACK_RUBBER);
    for (int i = 0; i < 10; ++i) gScene.Box(TEX_NONE, 54.45f + i, 2.1f, 14.35f, 54.55f + i, 2.55f, 14.45f, BLACK_RUBBER);
    // зал Б: стойки ИБП вдоль восточной стены, аккумуляторные шкафы вдоль западной
    Equip(TEX_METAL, 73.1f, 0, 22.0f, 73.9f, 2.0f, 30.0f, RgbOf(80, 90, 94), true);
    for (int i = 0; i < 4; ++i) gScene.Box(TEX_NONE, 73.1f, 1.6f, 22.4f + i * 2.0f, 73.09f, 1.65f, 22.7f + i * 2.0f, RgbOf(52, 150, 72), 1.0f, 0x3F);
    Equip(TEX_METAL, 62.0f, 0, 21.0f, 62.7f, 1.7f, 27.0f, RgbOf(100, 96, 74), true);
    // пультовая: стол, стул, стойка
    Equip(TEX_METAL, 54.6f, 0, 22.0f, 57.6f, 0.78f, 22.8f, RgbOf(110, 92, 66), true);
    gScene.Box(TEX_METAL, 55.2f, 0.78f, 22.1f, 55.9f, 1.15f, 22.3f, RgbOf(28, 30, 30));
    Equip(TEX_METAL, 55.5f, 0, 23.1f, 56.0f, 0.45f, 23.6f, RgbOf(60, 62, 62), true);
    Equip(TEX_METAL, 50.0f, 0, 25.0f, 50.7f, 2.0f, 30.0f, RgbOf(80, 90, 94), true);
    AddLampFixture(54.0f, 2.8f, 27.0f, 0.9f, 0.2f, 3.0f);
    AddLampFixture(58.0f, 3.2f, 15.0f, 0.9f, 0.2f, 3.4f, RgbOf(210, 214, 206));
    AddLampFixture(68.0f, 3.2f, 15.0f, 0.9f, 0.2f, 3.4f, RgbOf(210, 214, 206));
}

void BuildSouthHall() {
    const float fy = -0.51f;
    const float cx[] = {38.0f, 46.0f, 54.0f}, cz[] = {68.0f, 76.0f};
    for (float z : cz) for (float x : cx) {
        gScene.Cylinder(TEX_CONCRETE_A, V3{x, fy, z}, V3{x, 3.6f, z}, 0.30f, 12, CONCRETE, false, false);
        gScene.Cylinder(TEX_CONCRETE_B, V3{x, fy, z}, V3{x, fy + 0.12f, z}, 0.40f, 12, METAL_DARK);
        gScene.Cylinder(TEX_CONCRETE_B, V3{x, 3.45f, z}, V3{x, 3.6f, z}, 0.40f, 12, METAL_DARK);
        gScene.Cylinder(TEX_CONCRETE_B, V3{x, fy, z}, V3{x, 0.4f, z}, 0.31f, 12, HAZARD, false, false);
        AddCylCollider(x, z, 0.36f, fy, 3.6f);
    }
    // резервуар с куполом и поясами, коллизия по окружности
    const float tx = 52.0f, tz = 76.0f;
    gScene.Cylinder(TEX_METAL, V3{tx, fy, tz}, V3{tx, 2.6f, tz}, 1.15f, 16, RgbOf(122, 110, 92), true, true);
    gScene.Cylinder(TEX_METAL, V3{tx, 0.45f, tz}, V3{tx, 0.55f, tz}, 1.2f, 16, METAL_DARK, false, false);
    gScene.Cylinder(TEX_METAL, V3{tx, 1.5f, tz}, V3{tx, 1.6f, tz}, 1.2f, 16, METAL_DARK, false, false);
    AddCylCollider(tx, tz, 1.2f, fy, 2.6f);
    gScene.Pipe(V3{tx, 2.6f, tz}, V3{tx, 3.25f, tz}, 0.12f, METAL_MID, true, false);
    gScene.Pipe(V3{tx, 3.25f, tz}, V3{tx, 3.25f, 72.0f}, 0.12f, METAL_MID, true, true);
    gScene.Pipe(V3{tx, 3.25f, 72.0f}, V3{38.0f, 3.25f, 72.0f}, 0.12f, METAL_MID, true, true);
    Hangers(tx, 3.25f, 38.0f, 3.25f, 3.25f, 3.6f);
    Equip(TEX_METAL, 33.0f, fy, 70.0f, 34.2f, 1.9f, 73.0f, RgbOf(96, 104, 98), true);
    gScene.Pipe(V3{38.0f, fy + 1.0f, 79.8f}, V3{56.0f, fy + 1.0f, 79.8f}, 0.12f, RUST, true, true);
    AddBoxCollider(38.0f, 79.6f, 56.0f, 80.0f, fy + 0.88f, fy + 1.12f);
    AddLampFixture(46.0f, 3.4f, 70.0f, 1.0f, 0.22f, 3.6f);
}

void BuildGeneratorHall() {
    const float fy = 0.34f;
    const float gx[] = {73.5f, 79.0f};
    for (float x : gx) {
        Equip(TEX_METAL, x, fy, 54.2f, x + 3.5f, fy + 1.9f, 56.0f, RgbOf(64, 88, 76), true);
        gScene.Box(TEX_METAL, x + 0.2f, fy + 1.9f, 54.4f, x + 1.4f, fy + 2.15f, 55.8f, METAL_DARK);                 // радиатор
        gScene.Box(TEX_METAL, x + 2.0f, fy + 0.4f, 56.0f, x + 3.2f, fy + 1.4f, 56.012f, RgbOf(30, 34, 34));          // панель управления
        gScene.Pipe(V3{x + 2.6f, fy + 1.9f, 55.1f}, V3{x + 2.6f, 3.95f, 55.1f}, 0.16f, RUST, true, false, 12);      // выхлоп
        gScene.Box(TEX_METAL, x - 0.1f, fy, 54.1f, x + 3.6f, fy + 0.1f, 56.1f, METAL_DARK);                          // рама
    }
    const float drum[][2] = {{89.0f, 68.0f}, {89.9f, 69.0f}, {88.9f, 70.0f}, {90.3f, 70.5f}};
    const Rgb dcol[] = {RgbOf(140, 60, 40), RgbOf(62, 92, 130), RgbOf(140, 60, 40), RgbOf(70, 74, 72)};
    for (int i = 0; i < 4; ++i) {
        gScene.Cylinder(TEX_METAL, V3{drum[i][0], fy, drum[i][1]}, V3{drum[i][0], fy + 0.9f, drum[i][1]}, 0.30f, 12, dcol[i]);
        gScene.Cylinder(TEX_METAL, V3{drum[i][0], fy + 0.55f, drum[i][1]}, V3{drum[i][0], fy + 0.6f, drum[i][1]}, 0.315f, 12, METAL_DARK, false, false);
        AddCylCollider(drum[i][0], drum[i][1], 0.33f, fy, fy + 0.9f);
    }
    AddBoxCollider(GEN_X - GEN_HALF_X, GEN_Z - GEN_HALF_Z, GEN_X + GEN_HALF_X, GEN_Z + GEN_HALF_Z, fy, fy + 1.45f);   // модель генератора
    gScene.Box(TEX_NONE, 84.9f, fy, 65.97f, 80.5f, fy + 0.05f, 66.03f, BLACK_RUBBER);                              // кабели по полу (перешагиваются)
    gScene.Box(TEX_NONE, 80.47f, fy, 56.2f, 80.53f, fy + 0.05f, 66.03f, BLACK_RUBBER);
    gScene.Pipe(V3{73.0f, 3.7f, 64.0f}, V3{91.0f, 3.7f, 64.0f}, 0.15f, RUST, true, true);                         // верхняя магистраль
    Hangers(73.0f, 64.0f, 91.0f, 64.0f, 3.7f, 4.0f);
    Equip(TEX_METAL, 80.2f, fy, 78.6f, 81.4f, fy + 0.8f, 79.8f, RgbOf(110, 92, 64), true);                         // ящики в нише
    Equip(TEX_METAL, 84.6f, fy, 78.4f, 85.8f, fy + 0.7f, 79.6f, RgbOf(96, 84, 60), true);
    AddLampFixture(80.0f, 3.8f, 60.0f, 1.0f, 0.22f, 4.0f, RgbOf(214, 208, 190));
}

void BuildTransformerBay() {
    for (int k = 0; k < 2; ++k) {
        const float z0 = k == 0 ? 39.0f : 44.0f, z1 = z0 + 3.6f;
        Equip(TEX_METAL, 99.4f, 0, z0, 101.9f, 2.3f, z1, RgbOf(62, 74, 70), true);
        for (int i = 0; i < 14; ++i) gScene.Box(TEX_METAL, 99.0f, 0.3f + i * 0.14f, z0 + 0.25f, 99.4f, 0.36f + i * 0.14f, z1 - 0.25f, RgbOf(54, 64, 62));
        AddBoxCollider(99.0f, z0 + 0.25f, 99.4f, z1 - 0.25f, 0.3f, 2.2f);
        for (int b = 0; b < 3; ++b) gScene.Cylinder(TEX_METAL, V3{100.65f, 2.3f, z0 + 0.6f + b * 1.2f}, V3{100.65f, 2.85f, z0 + 0.6f + b * 1.2f}, 0.09f, 8, RgbOf(196, 190, 176));
    }
    gScene.Pipe(V3{94.1f, 3.0f, 38.18f}, V3{101.9f, 3.0f, 38.18f}, 0.12f, METAL_MID, true, true);
    gScene.Box(TEX_METAL, 94.0f, 1.0f, 47.8f, 95.6f, 1.9f, 48.0f, RgbOf(96, 104, 98));
    AddLampFixture(98.0f, 3.65f, 43.0f, 1.0f, 0.22f, 3.85f, RgbOf(200, 204, 196));
}

// ------------------------------------------------------------------ аварийная дверь и ниша
void BuildFireDoorNiche() {
    const float ceilN = 3.2f;
    // пилястры по бокам ниши: ниша сужается до 1.2 м, дверь утоплена на 2 м от плоскости магистрали
    Equip(TEX_CONCRETE_A, PASSAGE_X0, 0, FIRE_DOOR_Z, NICHE_X0, ceilN, Wc(20), CONCRETE, true);
    Equip(TEX_CONCRETE_A, NICHE_X1, 0, FIRE_DOOR_Z, PASSAGE_X1, ceilN, Wc(20), CONCRETE, true);
    gScene.Box(TEX_CONCRETE_B, PASSAGE_X0, 0, FIRE_DOOR_Z + 1.0f, NICHE_X0 + 0.005f, DADO_H, Wc(20) + 0.005f, DADO, 1.0f, 0x3F);
    // коробка: стойки, перемычка, стена над дверью. Створка и надписи - динамическая геометрия (BuildDoorGeometry).
    const float ox0 = DOOR_HINGE_X - 0.01f, ox1 = DOOR_HINGE_X + DOOR_OPEN_W - 0.01f;
    const float fz0 = FIRE_DOOR_Z - DOOR_FRAME_T * 0.5f, fz1 = FIRE_DOOR_Z + DOOR_FRAME_T * 0.5f;
    Equip(TEX_METAL, NICHE_X0, 0, fz0, ox0, DOOR_H + 0.05f, fz1, RgbOf(70, 78, 74), true);
    Equip(TEX_METAL, ox1, 0, fz0, NICHE_X1, DOOR_H + 0.05f, fz1, RgbOf(70, 78, 74), true);
    gScene.Box(TEX_METAL, NICHE_X0, DOOR_H + 0.05f, fz0, NICHE_X1, DOOR_H + 0.20f, fz1, RgbOf(70, 78, 74));
    gScene.Box(TEX_CONCRETE_A, NICHE_X0, DOOR_H + 0.20f, fz0, NICHE_X1, ceilN, fz1, CONCRETE);
    gScene.Box(TEX_METAL, ox0, 0, FIRE_DOOR_Z - 0.10f, ox1, 0.03f, FIRE_DOOR_Z + 0.10f, METAL_DARK);   // порог
    AddLampFixture(PASSAGE_CX, 2.95f, 39.2f, 0.5f, 0.16f, ceilN, RgbOf(236, 240, 232));              // аварийная лампа освещает табличку
    gScene.Box(TEX_METAL, PASSAGE_CX + 0.45f, 0.9f, Wc(20) - 0.14f, PASSAGE_CX + 0.55f, 2.9f, Wc(20) - 0.04f, METAL_DARK);   // кабель-канал на пилястре
}

void BuildPassage() {
    // облицовка стен прохода: тесный просвет 1.5 м; коллизия = видимые объёмы
    struct Seg { float z0, z1, h; };
    const Seg segs[] = {{FINAL_DOOR_Z - 0.05f, 22.0f, 3.9f}, {22.0f, 30.0f, 3.5f}, {30.0f, FIRE_DOOR_Z - DOOR_FRAME_T * 0.5f, 3.1f}};
    for (const Seg& s : segs) {
        Equip(TEX_CONCRETE_A, PASSAGE_X0, 0, s.z0, PASSAGE_X0 + PASSAGE_LINER, s.h, s.z1, RgbOf(150, 150, 144), true);
        Equip(TEX_CONCRETE_A, PASSAGE_X1 - PASSAGE_LINER, 0, s.z0, PASSAGE_X1, s.h, s.z1, RgbOf(150, 150, 144), true);
    }
    // окрашенный цоколь на облицовке и жёлтая кромка ступеней
    for (const Flight& f : kFlights) {
        if (f.laneMin != PASSAGE_X0) continue;
        for (int i = 0; i < f.steps; ++i) {
            const float edge = f.edge + f.dir * f.run * i, top = f.baseH + f.rise * (i + 1);
            gScene.Box(TEX_NONE, PASSAGE_X0 + PASSAGE_LINER, top, edge - 0.06f, PASSAGE_X1 - PASSAGE_LINER, top + 0.02f, edge, HAZARD);
        }
    }
    gScene.Pipe(V3{PASSAGE_X0 + PASSAGE_LINER + 0.06f, 2.45f, FINAL_DOOR_Z + 0.4f}, V3{PASSAGE_X0 + PASSAGE_LINER + 0.06f, 2.45f, FIRE_DOOR_Z - 0.4f}, 0.04f, RUST, true, true, 8);
    gScene.Box(TEX_METAL, PASSAGE_X1 - PASSAGE_LINER - 0.05f, 0.4f, 33.0f, PASSAGE_X1 - PASSAGE_LINER, 1.3f, 33.6f, RgbOf(96, 104, 98));   // щиток
    AddLampFixture(PASSAGE_CX, 3.0f, 33.5f, 0.5f, 0.18f, 3.1f, RgbOf(226, 232, 238));   // приглушённый почти белый свет
    AddLampFixture(PASSAGE_CX, 3.4f, 26.5f, 0.5f, 0.18f, 3.5f, RgbOf(226, 232, 238));
    AddLampFixture(PASSAGE_CX, 3.8f, 21.0f, 0.5f, 0.18f, 3.9f, RgbOf(226, 232, 238));
    // голубая лампа в дальнем конце: корпус + светящаяся панель; сам свет - локальный источник в шейдере
    AddLampFixture(BLUE_LAMP_X, 3.50f, BLUE_LAMP_Z, 0.5f, 0.2f, 3.9f, RgbOf(110, 170, 255));
}

void BuildFinalDoor() {
    const float y0 = FloorH(PASSAGE_CX, FINAL_DOOR_Z + 0.5f);         // 1.53
    const float topY = y0 + DOOR_H + 0.05f;
    const float ox0 = DOOR_HINGE_X - 0.01f, ox1 = DOOR_HINGE_X + DOOR_OPEN_W - 0.01f;
    const float ceilY = gCeil[8][44], wz = FINAL_DOOR_Z - 0.05f;      // плоскость торцевой стены прохода: z = 16.0
    const V3 nS{0, 0, 1};
    VPanel(gScene, TEX_CONCRETE_A, PASSAGE_X0, wz, ox0, wz, y0, ceilY, nS, CONCRETE, 1.0f);       // стена слева и справа от проёма
    VPanel(gScene, TEX_CONCRETE_A, ox1, wz, PASSAGE_X1, wz, y0, ceilY, nS, CONCRETE, 1.0f);
    VPanel(gScene, TEX_CONCRETE_A, ox0, wz, ox1, wz, topY, ceilY, nS, CONCRETE, 1.0f);            // над проёмом
    const float bz = wz - FINAL_RECESS_DEPTH;                                                      // ниша за дверью
    VPanel(gScene, TEX_CONCRETE_A, ox0, bz, ox0, wz, y0, topY, V3{1, 0, 0}, CONCRETE, 0.85f);
    VPanel(gScene, TEX_CONCRETE_A, ox1, bz, ox1, wz, y0, topY, V3{-1, 0, 0}, CONCRETE, 0.85f);
    gScene.Plane(TEX_CONCRETE_A, V3{ox0, topY, bz}, V3{ox0, topY, wz}, V3{ox1, topY, wz}, V3{ox1, topY, bz}, CEIL_TINT, 0.7f, 0, 0, 1, 1);
    gScene.Plane(TEX_CONCRETE_A, V3{ox0, y0, bz}, V3{ox0, y0, wz}, V3{ox1, y0, wz}, V3{ox1, y0, bz}, FLOOR_TINT, 0.9f, 0, 0, 1, 1);
    // тёплый жёлтый отсвет Level 0 за дверью (самосветящаяся панель в глубине ниши)
    VPanel(gScene, TEX_NONE, ox0, bz, ox1, bz, y0, topY, nS, RgbOf(232, 208, 112), 1.0f, 1.0f);
    // рама на лицевой стороне
    gScene.Box(TEX_METAL, ox0 - 0.10f, y0, wz, ox0, topY + 0.10f, wz + 0.10f, RgbOf(70, 78, 74));
    gScene.Box(TEX_METAL, ox1, y0, wz, ox1 + 0.10f, topY + 0.10f, wz + 0.10f, RgbOf(70, 78, 74));
    gScene.Box(TEX_METAL, ox0 - 0.10f, topY, wz, ox1 + 0.10f, topY + 0.10f, wz + 0.10f, RgbOf(70, 78, 74));
    gScene.Box(TEX_METAL, ox0, y0, wz + 0.04f, ox1, y0 + 0.03f, wz + 0.16f, METAL_DARK);           // порог
    AddBoxCollider(ox0 - 0.02f, bz, ox1 + 0.02f, wz + 0.005f, y0, topY);                           // проём закрыт, пока идёт открытие
}

void BuildAllStatic() {
    gScene.Init(WORLD_W, WORLD_D, LightFnImpl);
    gDoors.Init(WORLD_W, WORLD_D, LightFnImpl);
    gBoxes.clear(); gCyls.clear();
    for (int cz = 0; cz < MAP_H; ++cz) for (int cx = 0; cx < MAP_W; ++cx) {
        if (!gOpenCell[cz][cx]) continue;
        EmitFloor(cx, cz); EmitCeiling(cx, cz); EmitSoffit(cx, cz);
        if (!CellOpenAt(cx, cz - 1)) EmitWallSegment(cx, cz, Side::N);
        if (!CellOpenAt(cx, cz + 1)) EmitWallSegment(cx, cz, Side::S);
        if (!CellOpenAt(cx - 1, cz)) EmitWallSegment(cx, cz, Side::W);
        if (!CellOpenAt(cx + 1, cz)) EmitWallSegment(cx, cz, Side::E);
    }
    BuildTrunkAndLobby(); BuildCorridors(); BuildPumpRoom(); BuildSwitchgear(); BuildSouthHall(); BuildGeneratorHall();
    BuildTransformerBay(); BuildFireDoorNiche(); BuildPassage(); BuildFinalDoor();
}

// ------------------------------------------------------------------ двери: динамическая геометрия
// Створка поворачивается на петлях (ось Y): локальная ось x - вдоль створки, z - нормаль лицевой стороны (к игроку).
struct DoorBasis { V3 hinge, ex, ez; };
inline V3 DoorP(const DoorBasis& b, float lx, float y, float lz) {
    return V3{b.hinge.x + b.ex.x * lx + b.ez.x * lz, y, b.hinge.z + b.ex.z * lx + b.ez.z * lz};
}
void DoorBox(Scene& sc, const DoorBasis& b, float y0, float y1, float lx0, float lx1, float lz0, float lz1,
             Tex t, Rgb tint, uint8_t mask = 0x33) {
    const V3 up{0, 1, 0};
    const V3 down{0, -(y1 - y0), 0};
    if (mask & 0x10) sc.Panel(t, DoorP(b, lx0, y1, lz1), b.ex * (lx1 - lx0), down, b.ez, tint, 0.95f, 0, lx0 / 2, -y1 / 2, lx1 / 2, -y0 / 2);   // +z, лицо
    if (mask & 0x20) sc.Panel(t, DoorP(b, lx1, y1, lz0), b.ex * -(lx1 - lx0), down, b.ez * -1.0f, tint, 0.8f, 0, lx0 / 2, -y1 / 2, lx1 / 2, -y0 / 2);   // -z, оборот
    if (mask & 0x01) sc.Panel(t, DoorP(b, lx1, y1, lz1), b.ez * -(lz1 - lz0), down, b.ex, tint, 0.85f, 0, 0, -y1 / 2, (lz1 - lz0) / 2, -y0 / 2);      // +x, торец
    if (mask & 0x02) sc.Panel(t, DoorP(b, lx0, y1, lz0), b.ez * (lz1 - lz0), down, b.ex * -1.0f, tint, 0.85f, 0, 0, -y1 / 2, (lz1 - lz0) / 2, -y0 / 2); // -x, торец
    (void)up;
}
// Надпись: пластина-основание + текстурный квад на её лицевой плоскости, ориентация строго по базису створки.
void DoorSign(Scene& sc, const DoorBasis& b, Tex t, float lx0, float width, float yTop, float height, int tag) {
    const float lzFront = DOOR_T * 0.5f + 0.020f;
    DoorBox(sc, b, yTop - height, yTop, lx0 - 0.012f, lx0 + width + 0.012f, DOOR_T * 0.5f, lzFront - 0.002f, TEX_METAL, RgbOf(40, 44, 44), 0x01 | 0x02);
    // Panel(): TL = верхний левый (глядя на лицо), right = ex, down = -y => Cross(down,right) = ez = нормаль лица
    sc.Panel(t, DoorP(b, lx0, yTop, lzFront), b.ex * width, V3{0, -height, 0}, b.ez, RgbOf(255, 255, 255), 1.0f, 0.55f, 0, 0, 1, 1, tag);
}
void AddDoorLeaf(Scene& sc, float hingeZ, float angle, float baseY, bool fire) {
    DoorBasis b;
    b.hinge = V3{DOOR_HINGE_X, 0, hingeZ};
    b.ex = V3{cosf(angle), 0, -sinf(angle)};
    b.ez = V3{sinf(angle), 0, cosf(angle)};
    const Rgb leaf = fire ? RgbOf(104, 128, 114) : RgbOf(84, 100, 122);
    DoorBox(sc, b, baseY + 0.03f, baseY + DOOR_H, 0.0f, DOOR_W, -DOOR_T * 0.5f, DOOR_T * 0.5f, TEX_METAL, leaf, 0x33);
    DoorBox(sc, b, baseY + 0.03f, baseY + 0.20f, 0.0f, DOOR_W, DOOR_T * 0.5f, DOOR_T * 0.5f + 0.01f, TEX_METAL, RgbOf(60, 64, 62), 0x10);   // отбойная пластина
    const float signW = 0.78f, signH = 0.26f, signX = (DOOR_W - signW) * 0.5f;
    if (fire) {
        DoorSign(sc, b, TEX_SIGN_FIRE, signX, signW, baseY + 1.98f, signH, 100);
        DoorSign(sc, b, TEX_SIGN_KEEP, signX, signW, baseY + 1.62f, signH, 101);
        // рычажная ручка и замок
        DoorBox(sc, b, baseY + 0.90f, baseY + 1.08f, 0.74f, 0.88f, DOOR_T * 0.5f, DOOR_T * 0.5f + 0.03f, TEX_METAL, RgbOf(30, 32, 32), 0x3F);
        DoorBox(sc, b, baseY + 0.99f, baseY + 1.03f, 0.64f, 0.86f, DOOR_T * 0.5f + 0.03f, DOOR_T * 0.5f + 0.065f, TEX_METAL, RgbOf(22, 24, 24), 0x3F);
        DoorBox(sc, b, baseY + 0.80f, baseY + 0.88f, 0.78f, 0.86f, DOOR_T * 0.5f, DOOR_T * 0.5f + 0.02f, TEX_METAL, RgbOf(150, 138, 96), 0x3F);
    } else {
        DoorSign(sc, b, TEX_SIGN_LEVEL0, signX, signW, baseY + 1.81f, signH, 102);
        DoorBox(sc, b, baseY + 0.92f, baseY + 1.10f, 0.74f, 0.88f, DOOR_T * 0.5f, DOOR_T * 0.5f + 0.03f, TEX_METAL, RgbOf(30, 32, 32), 0x3F);
        DoorBox(sc, b, baseY + 0.99f, baseY + 1.03f, 0.64f, 0.86f, DOOR_T * 0.5f + 0.03f, DOOR_T * 0.5f + 0.065f, TEX_METAL, RgbOf(22, 24, 24), 0x3F);
    }
}
void BuildDoorGeometry() {
    gDoors.Clear();
    AddDoorLeaf(gDoors, FIRE_DOOR_Z, gFireAngle, FloorH(PASSAGE_CX, FIRE_DOOR_Z + 0.3f), true);
    AddDoorLeaf(gDoors, FINAL_DOOR_Z, gFinalAngle, FloorH(PASSAGE_CX, FINAL_DOOR_Z + 0.5f), false);
}

// ------------------------------------------------------------------ текстуры
Pixmap LoadBasePixmap(const char* path, int w, int h) {
    Pixmap pm; pm.Resize(w, h);
    Image img = LoadImage(path);
    if (img.data) {
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        ImageResize(&img, w, h);
        std::memcpy(pm.rgba.data(), img.data, (size_t)w * h * 4);
        UnloadImage(img);
    } else {
        for (int i = 0; i < w * h; ++i) { pm.rgba[i * 4] = 118; pm.rgba[i * 4 + 1] = 116; pm.rgba[i * 4 + 2] = 110; pm.rgba[i * 4 + 3] = 255; }
    }
    return pm;
}
// Основа тайла стены: ровно тот участок текстуры бетона, который показала бы обычная стена на этом месте
// (мировые УФ: u = координата/2 м - клетки кратны 2 м, поэтому тайл начинается с u=0; v = -y/2). Шва между стеной и тайлом нет.
Pixmap MakeWallBase(const char* path) {
    Image img = LoadImage(path);
    Pixmap out; out.Resize(WALL_TILE_W, WALL_TILE_H);
    if (!img.data) { for (int i = 0; i < WALL_TILE_W * WALL_TILE_H; ++i) { out.rgba[i * 4] = 118; out.rgba[i * 4 + 1] = 116; out.rgba[i * 4 + 2] = 110; out.rgba[i * 4 + 3] = 255; } return out; }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const unsigned char* s = (const unsigned char*)img.data;
    const int sw = img.width, sh = img.height;
    for (int r = 0; r < WALL_TILE_H; ++r) {
        const float y = ARROW_Y1 - (r + 0.5f) * (ARROW_Y1 - ARROW_Y0) / WALL_TILE_H;
        float v = -y / TEX_REPEAT_M; v -= floorf(v);
        const float fy = v * sh - 0.5f; const int y0 = (int)floorf(fy); const float ty = fy - y0;
        for (int c = 0; c < WALL_TILE_W; ++c) {
            const float fx = (c + 0.5f) / WALL_TILE_W * sw - 0.5f; const int x0 = (int)floorf(fx); const float tx = fx - x0;
            for (int k = 0; k < 3; ++k) {
                auto px = [&](int xx, int yy) { xx = ((xx % sw) + sw) % sw; yy = ((yy % sh) + sh) % sh; return (float)s[((size_t)yy * sw + xx) * 4 + k]; };
                const float a = px(x0, y0) * (1 - tx) + px(x0 + 1, y0) * tx, b = px(x0, y0 + 1) * (1 - tx) + px(x0 + 1, y0 + 1) * tx;
                out.rgba[((size_t)r * WALL_TILE_W + c) * 4 + k] = (uint8_t)(a * (1 - ty) + b * ty);
            }
            out.rgba[((size_t)r * WALL_TILE_W + c) * 4 + 3] = 255;
        }
    }
    UnloadImage(img);
    return out;
}
Texture2D TextureFromPixmap(const Pixmap& pm) {
    Image img{};
    const size_t bytes = (size_t)pm.w * pm.h * 4;
    img.data = std::malloc(bytes);
    std::memcpy(img.data, pm.rgba.data(), bytes);
    img.width = pm.w; img.height = pm.h; img.mipmaps = 1; img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}
void LoadAllTextures() {
    gTexObj[TEX_NONE] = Texture2D{};
    gTexObj[TEX_CONCRETE_A] = LoadTexture("level2/textures/Concrete046_Color.jpg");
    gTexObj[TEX_CONCRETE_B] = LoadTexture("level2/textures/Concrete048_Color.jpg");
    gTexObj[TEX_METAL] = LoadTexture("level2/textures/Metal024_Color.jpg");
    for (int t = TEX_CONCRETE_A; t <= TEX_METAL; ++t)
        if (gTexObj[t].id) { SetTextureWrap(gTexObj[t], TEXTURE_WRAP_REPEAT); SetTextureFilter(gTexObj[t], TEXTURE_FILTER_TRILINEAR); }

    // Атлас стен: чёрные плесневые стрелки (вправо/влево) и следы на стене. Основа - тот же бетон.
    Pixmap atlas; atlas.Resize(ATLAS_W, ATLAS_H);
    const Pixmap wallBase = MakeWallBase("level2/textures/Concrete046_Color.jpg");
    for (int slot = 0; slot < ATLAS_SLOTS; ++slot) {
        if (slot == ATLAS_ARROW_R2 || slot == ATLAS_ARROW_L2) continue;    // третьи варианты не используются
        Pixmap tile = wallBase;
        if (slot == ATLAS_WALL_BLOOD) ComposeWallBlood(tile, 9001u);
        else ComposeMoldArrow(tile, 4100u + 37u * slot, slot < ATLAS_ARROW_L0);
        int px, py; AtlasSlotOrigin(slot, &px, &py);
        Blit(atlas, tile, px, py);
    }
    gTexObj[TEX_ATLAS] = TextureFromPixmap(atlas);
    const Pixmap floorBase = LoadBasePixmap("level2/textures/Concrete046_Color.jpg", FLOOR_TILE, FLOOR_TILE);
    for (int v = 0; v < 2; ++v) {
        Pixmap tile = floorBase;
        ComposeFloorBlood(tile, v, 7700u + v);
        gTexObj[TEX_FLOOR_BLOOD_A + v] = TextureFromPixmap(tile);
    }
    for (int k = 0; k < 3; ++k) {
        Pixmap sign; MakeSign(sign, k);
        gTexObj[TEX_SIGN_FIRE + k] = TextureFromPixmap(sign);
    }
}
void FreeAllTextures() {
    for (int t = 0; t < TEX_COUNT; ++t) { if (gTexObj[t].id) UnloadTexture(gTexObj[t]); gTexObj[t] = Texture2D{}; }
}

void EnsureBuilt() {
    if (gBuilt) return;
    BuildGrid();
    BuildAllStatic();
    LoadAllTextures();
    gGenerator = LoadModel("level2/models/portable_generator/portable_generator_1k.gltf");
    gGeneratorLoaded = gGenerator.meshCount > 0;
    gBuilt = true;
}
void ReleaseAll() {
    if (gGeneratorLoaded) UnloadModel(gGenerator);
    gGenerator = Model{}; gGeneratorLoaded = false;
    FreeAllTextures();
    gScene = Scene{}; gDoors = Scene{};
    gBoxes.clear(); gBoxes.shrink_to_fit(); gCyls.clear(); gCyls.shrink_to_fit(); gPlat.clear();
    gBuilt = false;
}

// ------------------------------------------------------------------ отрисовка кэша
void EmitQuads(const std::vector<Vtx>& q) {
    const size_t n = q.size() / 4;
    size_t i = 0;
    while (i < n) {
        const size_t chunk = std::min<size_t>(n - i, 64);
        rlCheckRenderBatchLimit((int)chunk * 4);
        rlBegin(RL_QUADS);
        for (size_t j = 0; j < chunk; ++j) for (int k = 0; k < 4; ++k) {
            const Vtx& v = q[(i + j) * 4 + k];
            rlColor4ub(v.r, v.g, v.b, v.a);
            rlTexCoord2f(v.u, v.v);
            rlVertex3f(v.x - gOrg.x, v.y, v.z - gOrg.z);
        }
        rlEnd();
        i += chunk;
    }
}
void DrawScene(const Scene& sc, float px, float pz, float radius, int texFrom, int texTo) {
    for (int t = texFrom; t <= texTo; ++t) {
        rlSetTexture(gTexObj[t].id);
        for (int iz = 0; iz < sc.TilesZ(); ++iz) for (int ix = 0; ix < sc.TilesX(); ++ix) {
            const float dx = (ix + 0.5f) * TILE_M - px, dz = (iz + 0.5f) * TILE_M - pz;
            if (dx * dx + dz * dz > radius * radius) continue;
            const std::vector<Vtx>& q = sc.TileAt(ix, iz).b[t].quads;
            if (!q.empty()) EmitQuads(q);
        }
    }
    rlSetTexture(0);
}

} // namespace

// ====================================================================== публичный интерфейс
void Level2SetActive(bool active) {
    if (active) EnsureBuilt();
    else if (gActive && gBuilt) ReleaseAll();      // возврат на Level 0: освобождаем текстуры и геометрию Level 2
    gActive = active;
}
bool Level2IsActive() { return gActive; }

void Level2Reset() {
    gFireOpen = false; gFireAngle = 0.0f;
    gFinalOpening = false; gFinalTimer = 0.0f; gFinalAngle = 0.0f;
    gFirePromptShown = false; gFinalPromptShown = false; gTransitionSent = false;
    gItems.clear();
    for (const ItemDef& d : kItems) gItems.push_back({d.cx, d.cz, d.type, false});
}
void Level2Load() { /* ресурсы создаются при входе на уровень (Level2SetActive(true)) */ }
void Level2Unload() { if (gBuilt) ReleaseAll(); gActive = false; }

bool Level2IsSolid(int cx, int cz) { return !CellOpenAt(cx, cz); }

bool Level2CircleHits(float x, float z, float r) {
    if (!gBuilt) return true;
    const int cx0 = CellIdx(x - r), cx1 = CellIdx(x + r), cz0 = CellIdx(z - r), cz1 = CellIdx(z + r);
    for (int cz = cz0; cz <= cz1; ++cz) for (int cx = cx0; cx <= cx1; ++cx) {
        if (CellOpenAt(cx, cz)) continue;
        const Box3 cell{Wc(cx), Wc(cz), Wc(cx) + CELL, Wc(cz) + CELL, 0, 0};
        if (HitsBox(x, z, r, cell)) return true;
    }
    const float fl = FloorH(x, z);
    for (const Box3& b : gBoxes) {
        if (!(b.y1 > fl + BODY_BOTTOM && b.y0 < fl + BODY_TOP)) continue;
        if (HitsBox(x, z, r, b)) return true;
    }
    for (const Cyl3& c : gCyls) {
        if (!(c.y1 > fl + BODY_BOTTOM && c.y0 < fl + BODY_TOP)) continue;
        const float dx = x - c.x, dz = z - c.z, rr = r + c.r;
        if (dx * dx + dz * dz < rr * rr) return true;
    }
    float ax, az, bx, bz;
    DoorSegment(FIRE_DOOR_Z, gFireAngle, &ax, &az, &bx, &bz);
    if (HitsSegment(x, z, r, ax, az, bx, bz)) return true;
    DoorSegment(FINAL_DOOR_Z, gFinalAngle, &ax, &az, &bx, &bz);
    if (HitsSegment(x, z, r, ax, az, bx, bz)) return true;
    return false;
}

float Level2FloorAt(float wx, float wz) { return FloorH(wx, wz); }

bool Level2CanMoveTo(float x, float z, float radius, float currentFloor, float* newFloor) {
    if (Level2CircleHits(x, z, radius)) return false;
    const float h = FloorH(x, z);
    if (fabsf(h - currentFloor) > MAX_STEP_UP) return false;
    if (newFloor) *newFloor = h;
    return true;
}

float Level2LightAt(float wx, float wz) {
    float l = BaseLightAt(wx, wz);
    const bool inPassage = wx >= PASSAGE_X0 && wx <= PASSAGE_X1 && wz >= FINAL_DOOR_Z - 0.2f && wz <= FIRE_DOOR_Z - 0.1f;
    for (const l2::Lamp& lp : kLamps) {
        const bool lampInPassage = lp.x >= PASSAGE_X0 && lp.x <= PASSAGE_X1 && lp.z >= FINAL_DOOR_Z && lp.z <= FIRE_DOOR_Z - 0.1f;
        if (lampInPassage != inPassage) continue;       // свет не проходит сквозь стены прохода и закрытую дверь
        const float d = hypotf(wx - lp.x, wz - lp.z);
        if (d >= lp.radius) continue;
        const float t = 1.0f - d / lp.radius;
        l += lp.intensity * t * t;
    }
    return std::min(l, LIGHT_MAX);
}
bool Level2IsDark(float wx, float wz) {
    const int cx = CellIdx(wx), cz = CellIdx(wz);
    if (InMap(cx, cz) && gDarkCell[cz][cx]) return true;
    return Level2LightAt(wx, wz) < DARK_THRESHOLD;
}
int Level2PatrolCount() { return HIDE_COUNT; }
Vector2 Level2PatrolPoint(int index) {
    const int i = ((index % HIDE_COUNT) + HIDE_COUNT) % HIDE_COUNT;
    return {(kHideSpots[i][0] + 0.5f) * CELL, (kHideSpots[i][1] + 0.5f) * CELL};
}

Item* Level2ItemNear(float wx, float wz, float radius) {
    for (Item& item : gItems) {
        if (item.taken) continue;
        const float ix = (item.cx + 0.5f) * CELL, iz = (item.cz + 0.5f) * CELL;
        if (hypotf(ix - wx, iz - wz) < radius) return &item;
    }
    return nullptr;
}
void Level2TakeItem(Item* item) { if (item) item->taken = true; }
const std::vector<Item>& Level2GetItems() { return gItems; }

static bool FacingPoint(const Player& p, float tx, float tz) {
    const float dx = tx - p.pos.x, dz = tz - p.pos.z, d = hypotf(dx, dz);
    if (d < 0.35f) return true;
    return (sinf(p.yaw) * dx + cosf(p.yaw) * dz) / d >= USE_MIN_FACING_COS;
}

bool Level2Interact(Player& p) {
    if (!gActive || gTransitionSent) return false;
    if (!gFireOpen && p.pos.z > FIRE_DOOR_Z && hypotf(p.pos.x - PASSAGE_CX, p.pos.z - FIRE_DOOR_Z) <= FIRE_DOOR_USE_RADIUS &&
        FacingPoint(p, PASSAGE_CX, FIRE_DOOR_Z)) {
        gFireOpen = true;
        Say(T("FIRE EXIT RELEASED. THE PASSAGE IS OPEN.", "АВАРИЙНЫЙ ВЫХОД ОТКРЫТ. ПРОХОД СВОБОДЕН."), 4.0f);
        Haptic(180);
        return true;
    }
    if (gFireOpen && !gFinalOpening && p.pos.z > FINAL_DOOR_Z && hypotf(p.pos.x - PASSAGE_CX, p.pos.z - FINAL_DOOR_Z) <= FINAL_DOOR_USE_RADIUS &&
        FacingPoint(p, PASSAGE_CX, FINAL_DOOR_Z)) {
        gFinalOpening = true;
        gFinalTimer = FINAL_DOOR_OPEN_TIME;
        Say(T("MANUAL EXIT OPENING...", "РУЧНОЕ ОТКРЫТИЕ ВЫХОДА..."), 3.0f);
        Haptic(220);
        return true;
    }
    return false;       // иначе кнопка «БРОСОК» работает как раньше (бутылка)
}

bool Level2Update(Player& p, float dt) {
    if (!gActive || gTransitionSent) return false;
    if (!gFireOpen && !gFirePromptShown && p.pos.z > FIRE_DOOR_Z && hypotf(p.pos.x - PASSAGE_CX, p.pos.z - FIRE_DOOR_Z) < 3.2f) {
        gFirePromptShown = true;
        Say(T("RECESSED FIRE EXIT. PRESS THROW TO OPEN.", "СКРЫТАЯ АВАРИЙНАЯ ДВЕРЬ. НАЖМИТЕ «БРОСОК», ЧТОБЫ ОТКРЫТЬ."), 4.0f);
    }
    if (gFireOpen && !gFinalPromptShown && hypotf(p.pos.x - PASSAGE_CX, p.pos.z - FINAL_DOOR_Z) < 3.6f) {
        gFinalPromptShown = true;
        Say(T("MANUAL EXIT DOOR. PRESS THROW TO RETURN.", "РУЧНАЯ ВЫХОДНАЯ ДВЕРЬ. НАЖМИТЕ «БРОСОК», ЧТОБЫ ВЕРНУТЬСЯ."), 4.0f);
    }
    if (gFireOpen) gFireAngle = std::min(DOOR_OPEN_ANGLE, gFireAngle + dt * DOOR_OPEN_ANGLE / DOOR_OPEN_TIME);
    if (gFinalOpening) {
        gFinalAngle = std::min(DOOR_OPEN_ANGLE, gFinalAngle + dt * DOOR_OPEN_ANGLE / FINAL_DOOR_OPEN_TIME);
        gFinalTimer -= dt;
        if (gFinalTimer <= 0.0f) { gTransitionSent = true; return true; }     // переход только после ручного нажатия
    }
    return false;
}

bool Level2GetLocalLight(float pos[4], float color[3], float box[4]) {
    for (int i = 0; i < 4; ++i) { pos[i] = 0.0f; box[i] = 0.0f; }
    for (int i = 0; i < 3; ++i) color[i] = 0.0f;
    if (!gActive || !gBuilt) return false;
    pos[0] = BLUE_LAMP_X - gOrg.x; pos[1] = BLUE_LAMP_Y; pos[2] = BLUE_LAMP_Z - gOrg.z; pos[3] = BLUE_LAMP_RADIUS;
    for (int i = 0; i < 3; ++i) color[i] = BLUE_LAMP_COLOR[i];
    box[0] = BLUE_LIGHT_BOX[0] - gOrg.x; box[1] = BLUE_LIGHT_BOX[1] - gOrg.x;
    box[2] = BLUE_LIGHT_BOX[2] - gOrg.z; box[3] = BLUE_LIGHT_BOX[3] - gOrg.z;
    return true;
}

void Level2Draw(const Player& p, float time, float viewDistance) {
    (void)time;
    if (!gActive || !gBuilt) return;
    const float radius = viewDistance * 1.45f + TILE_M;
    DrawScene(gScene, p.pos.x, p.pos.z, radius, TEX_NONE, TEX_COUNT - 1);
    // двери: створка и надписи. Надписи рисуются с отсечением обратных граней: с оборотной стороны они не видны (нет зеркала).
    BuildDoorGeometry();
    DrawScene(gDoors, p.pos.x, p.pos.z, radius, TEX_NONE, TEX_METAL);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    DrawScene(gDoors, p.pos.x, p.pos.z, radius, TEX_SIGN_FIRE, TEX_SIGN_LEVEL0);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
}

void Level2DrawModels() {
    if (!gActive || !gGeneratorLoaded) return;
    const Vector3 position = {GEN_X - gOrg.x, FloorH(GEN_X, GEN_Z) + 0.04f, GEN_Z - gOrg.z};
    DrawModelEx(gGenerator, position, Vector3{0, 1, 0}, GEN_YAW_DEG, Vector3{GEN_SCALE, GEN_SCALE, GEN_SCALE}, WHITE);
}

// ---- Хуки только для офлайн-проверок (tools/level2_check). В сборку игры не попадают: макрос не определён. ----
#ifdef LEVEL2_TESTING
const l2g::Scene& Level2TestScene() { return gScene; }
const l2g::Scene& Level2TestDoors() { BuildDoorGeometry(); return gDoors; }
void Level2TestRecord(bool on) { gScene.EnableRecording(on); gDoors.EnableRecording(on); }
unsigned Level2TestTextureId(int slot) { return gTexObj[slot].id; }
void Level2TestSetDoorAngles(float fire, float fin) { gFireAngle = fire; gFinalAngle = fin; }
#endif
