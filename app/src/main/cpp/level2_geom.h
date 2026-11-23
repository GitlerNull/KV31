// level2_geom.h - кэш статической геометрии Level 2 и процедурные текстуры (стрелки из плесени, следы, надписи).
// Модуль НЕ зависит от raylib: геометрия строится один раз в CPU-буферы и затем рисуется из level2.cpp.
// Это же позволяет проверять ориентацию текста/стрелок и коллизии вне Android (tools/level2_check).
#pragma once
#include <cstdint>
#include <cmath>
#include <vector>

namespace l2g {

struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V3 a) { return sqrtf(Dot(a, a)); }
inline V3 Norm(V3 a) { float l = Len(a); return l > 1e-6f ? a * (1.0f / l) : V3{0, 0, 0}; }

// Вершина: rgb = оттенок материала, a = запечённая яркость (так устроен шейдер игры, см. game.cpp FS).
struct Vtx { float x, y, z, u, v; uint8_t r, g, b, a; };

// Слоты текстур. Конкретные GL-id подставляет level2.cpp при отрисовке.
enum Tex : uint8_t {
    TEX_NONE, TEX_CONCRETE_A, TEX_CONCRETE_B, TEX_METAL, TEX_ATLAS,
    TEX_FLOOR_BLOOD_A, TEX_FLOOR_BLOOD_B, TEX_SIGN_FIRE, TEX_SIGN_KEEP, TEX_SIGN_LURE, TEX_SIGN_EXIT, TEX_COUNT
};

struct Rgb { uint8_t r, g, b; };

constexpr float TILE_M = 8.0f;          // сторона тайла отсечения, м
constexpr float TEX_REPEAT_M = 2.0f;    // метров на один повтор бетонной/металлической текстуры

struct Batch { std::vector<Vtx> quads; };            // по 4 вершины на квад; порядок: левый-верх, левый-низ, правый-низ, правый-верх
struct Tile { Batch b[TEX_COUNT]; };

// Запись одного квада для проверок (ориентации текста/стрелок).
struct QuadRecord { Tex tex; V3 p[4]; float u[4], v[4]; int tag; };

class Scene {
public:
    typedef float (*LightFn)(float x, float z);
    // Необязательная поправка оттенка материала по положению вершины (голубой/красный отсвет вокруг лампы в проходе).
    typedef Rgb (*TintFn)(float x, float y, float z, Rgb in);
    void SetTintFn(TintFn f) { tint_ = f; }
    void Init(float worldW, float worldD, LightFn light);
    void Clear();
    int  TilesX() const { return tx_; }
    int  TilesZ() const { return tz_; }
    const Tile& TileAt(int ix, int iz) const { return tiles_[iz * tx_ + ix]; }
    size_t QuadCount() const { return quadCount_; }

    // Базовая операция. p[0..3] = TL, BL, BR, TR (вид спереди, против часовой). lightMin - нижний предел яркости (читаемость надписей).
    void Quad(Tex t, const V3 p[4], const float u[4], const float v[4], Rgb tint, float shade = 1.0f,
              float lightMin = 0.0f, int tag = -1);
    // Квад по двум осям: origin + s*uAxis + t*vAxis, s,t in [0..1]; ось v направлена вниз по картинке.
    // front = нормаль лицевой стороны. Порядок вершин гарантирует правильный winding и отсутствие зеркала.
    void Panel(Tex t, V3 topLeft, V3 right, V3 down, V3 front, Rgb tint, float shade, float lightMin,
               float u0, float v0, float u1, float v1, int tag = -1);
    void Box(Tex t, float x0, float y0, float z0, float x1, float y1, float z1, Rgb tint,
             float shade = 1.0f, uint8_t faceMask = 0x3F);   // биты: +X,-X,+Y,-Y,+Z,-Z
    // Цилиндр между точками (с торцами). seg >= 6.
    void Cylinder(Tex t, V3 a, V3 b, float r, int seg, Rgb tint, bool capA = true, bool capB = true, float shade = 1.0f);
    // Труба: цилиндр + хомуты + фланцы на торцах.
    void Pipe(V3 a, V3 b, float r, Rgb tint, bool flangeA, bool flangeB, int seg = 10);
    // Квад с произвольными УФ, ориентированный по плоскости стены/пола (для стен и полов).
    void Plane(Tex t, V3 a, V3 b, V3 c, V3 d, Rgb tint, float shade, float u0, float v0, float u1, float v1);

    const std::vector<QuadRecord>& Records() const { return records_; }
    void EnableRecording(bool on) { record_ = on; }

private:
    Tile& TileFor(const V3 p[4]);
    float tileW_ = 0, tileD_ = 0; int tx_ = 0, tz_ = 0;
    std::vector<Tile> tiles_;
    LightFn light_ = nullptr;
    TintFn tint_ = nullptr;
    size_t quadCount_ = 0;
    bool record_ = false;
    std::vector<QuadRecord> records_;
};

// ---------- Процедурные текстуры ----------
struct Pixmap {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    void Resize(int W, int H) { w = W; h = H; rgba.assign((size_t)W * H * 4, 255); }
    uint8_t* At(int x, int y) { return &rgba[((size_t)y * w + x) * 4]; }
    const uint8_t* At(int x, int y) const { return &rgba[((size_t)y * w + x) * 4]; }
};

// Размеры тайлов атласа плесени/следов на стенах (один тайл = 2.0 x 1.6 м на стене).
constexpr int WALL_TILE_W = 512, WALL_TILE_H = 410;
constexpr int FLOOR_TILE = 512;
constexpr int ATLAS_W = 2048, ATLAS_H = 1024;
constexpr int ATLAS_COLS = 4;
// Слоты атласа: стрелки вправо (0..2), влево (3..5 - 3 в первой строке, 4..5 во второй), кровь на стене (6).
constexpr int ATLAS_ARROW_R0 = 0, ATLAS_ARROW_R1 = 1, ATLAS_ARROW_R2 = 2, ATLAS_ARROW_L0 = 3,
              ATLAS_ARROW_L1 = 4, ATLAS_ARROW_L2 = 5, ATLAS_WALL_BLOOD = 6, ATLAS_SMILEY = 7, ATLAS_SLOTS = 8;

void AtlasSlotRect(int slot, float* u0, float* v0, float* u1, float* v1);   // УФ-прямоугольник слота
void AtlasSlotOrigin(int slot, int* px, int* py);

// Накладывает на base (WALL_TILE_W x WALL_TILE_H) стрелку из чёрной плесени. pointRight: острие в сторону +u.
void ComposeMoldArrow(Pixmap& base, uint32_t seed, bool pointRight);
// Чёрный смайлик «=)» из плесени/краски на стене.
void ComposeMoldSmiley(Pixmap& base, uint32_t seed);
// Следы крови на стене: смазанный отпечаток ладони и подтёки.
void ComposeWallBlood(Pixmap& base, uint32_t seed);
// Следы на полу: variant 0 - ниша у двери (ведут на север, к двери), 1 - клетка перед нишей.
void ComposeFloorBlood(Pixmap& base, int variant, uint32_t seed);
// Таблички. Текст рисуется векторным шрифтом: без зависимости от шрифтов Android и без зеркальности.
void MakeSign(Pixmap& out, int kind);   // 0 = FIRE EXIT, 1 = KEEP CLEAR, 2 = «ВЫХОД ЗА ДВЕРЬЮ» (ложная дверь), 3 = «ВЫХОД»
// Текст в UTF-8 (латиница, цифры 0, кириллица из набора знаков ниже); векторный шрифт, поэтому текст никогда не зеркален.
void DrawText(Pixmap& img, const char* text, float x, float y, float height, float stroke, Rgb color);
float TextWidth(const char* text, float height);
void Blit(Pixmap& dst, const Pixmap& src, int dx, int dy);

} // namespace l2g
