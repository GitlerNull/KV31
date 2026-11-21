#include "level2_geom.h"
#include <algorithm>
#include <cstring>

namespace l2g {

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float SmoothStep(float e0, float e1, float x) {
    float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// ======================= Scene =======================
void Scene::Init(float worldW, float worldD, LightFn light) {
    tileW_ = worldW; tileD_ = worldD;
    tx_ = (int)ceilf(worldW / TILE_M);
    tz_ = (int)ceilf(worldD / TILE_M);
    tiles_.assign((size_t)tx_ * tz_, Tile{});
    light_ = light;
    quadCount_ = 0;
    records_.clear();
}

void Scene::Clear() {
    for (Tile& t : tiles_) for (Batch& b : t.b) b.quads.clear();
    quadCount_ = 0;
    records_.clear();
}

Tile& Scene::TileFor(const V3 p[4]) {
    float cx = (p[0].x + p[1].x + p[2].x + p[3].x) * 0.25f;
    float cz = (p[0].z + p[1].z + p[2].z + p[3].z) * 0.25f;
    int ix = std::clamp((int)floorf(cx / TILE_M), 0, tx_ - 1);
    int iz = std::clamp((int)floorf(cz / TILE_M), 0, tz_ - 1);
    return tiles_[(size_t)iz * tx_ + ix];
}

void Scene::Quad(Tex t, const V3 p[4], const float u[4], const float v[4], Rgb tint, float shade,
                 float lightMin, int tag) {
    Tile& tile = TileFor(p);
    Batch& b = tile.b[t];
    for (int i = 0; i < 4; ++i) {
        float l = light_ ? light_(p[i].x, p[i].z) * shade : shade;
        if (l < lightMin) l = lightMin;
        Vtx vt;
        vt.x = p[i].x; vt.y = p[i].y; vt.z = p[i].z; vt.u = u[i]; vt.v = v[i];
        vt.r = tint.r; vt.g = tint.g; vt.b = tint.b;
        vt.a = (uint8_t)(Clamp01(l) * 255.0f + 0.5f);
        b.quads.push_back(vt);
    }
    ++quadCount_;
    if (record_) {
        QuadRecord rec; rec.tex = t; rec.tag = tag;
        for (int i = 0; i < 4; ++i) { rec.p[i] = p[i]; rec.u[i] = u[i]; rec.v[i] = v[i]; }
        records_.push_back(rec);
    }
}

void Scene::Panel(Tex t, V3 tl, V3 right, V3 down, V3 front, Rgb tint, float shade, float lightMin,
                  float u0, float v0, float u1, float v1, int tag) {
    (void)front;   // нормаль определяется порядком вершин: Cross(down, right) == front, проверяется тестами
    V3 p[4] = {tl, tl + down, tl + down + right, tl + right};
    float u[4] = {u0, u0, u1, u1};
    float v[4] = {v0, v1, v1, v0};
    Quad(t, p, u, v, tint, shade, lightMin, tag);
}

void Scene::Plane(Tex t, V3 a, V3 b, V3 c, V3 d, Rgb tint, float shade, float u0, float v0, float u1, float v1) {
    V3 p[4] = {a, b, c, d};
    float u[4] = {u0, u0, u1, u1};
    float v[4] = {v0, v1, v1, v0};
    Quad(t, p, u, v, tint, shade, 0.0f, -1);
}

void Scene::Box(Tex t, float x0, float y0, float z0, float x1, float y1, float z1, Rgb tint,
                float shade, uint8_t mask) {
    const float sx = x1 - x0, sy = y1 - y0, sz = z1 - z0;
    const float inv = 1.0f / TEX_REPEAT_M;
    // Для каждой грани: нормаль n, право r (= up x n), вниз d. Мировые УФ: u = Dot(P, r)/2, v = Dot(P, d)/2.
    struct Face { V3 n, r, d, tl; float w, h; float faceShade; };
    const Face faces[6] = {
        {{ 1, 0, 0}, {0, 0, -1}, {0, -1, 0}, {x1, y1, z1}, sz, sy, 0.82f},
        {{-1, 0, 0}, {0, 0,  1}, {0, -1, 0}, {x0, y1, z0}, sz, sy, 0.86f},
        {{ 0, 1, 0}, {1, 0,  0}, {0, 0,  1}, {x0, y1, z0}, sx, sz, 1.00f},
        {{ 0,-1, 0}, {1, 0,  0}, {0, 0, -1}, {x0, y0, z1}, sx, sz, 0.55f},
        {{ 0, 0, 1}, {1, 0,  0}, {0, -1, 0}, {x0, y1, z1}, sx, sy, 0.92f},
        {{ 0, 0,-1}, {-1,0,  0}, {0, -1, 0}, {x1, y1, z0}, sx, sy, 0.88f},
    };
    for (int i = 0; i < 6; ++i) {
        if (!(mask & (1u << i))) continue;
        const Face& f = faces[i];
        float u0 = Dot(f.tl, f.r) * inv, v0 = Dot(f.tl, f.d) * inv;
        Panel(t, f.tl, f.r * f.w, f.d * f.h, f.n, tint, shade * f.faceShade, 0.0f,
              u0, v0, u0 + f.w * inv, v0 + f.h * inv);
    }
}

void Scene::Cylinder(Tex t, V3 a, V3 b, float r, int seg, Rgb tint, bool capA, bool capB, float shade) {
    V3 axis = b - a;
    float len = Len(axis);
    if (len < 1e-4f) return;
    axis = axis * (1.0f / len);
    V3 ref = fabsf(axis.y) < 0.85f ? V3{0, 1, 0} : V3{1, 0, 0};
    V3 u = Norm(Cross(axis, ref));
    V3 v = Cross(axis, u);
    const float circ = 2.0f * 3.14159265f * r;
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.0f * 3.14159265f * i / seg, a1 = 2.0f * 3.14159265f * (i + 1) / seg;
        V3 o0 = u * cosf(a0) + v * sinf(a0), o1 = u * cosf(a1) + v * sinf(a1);
        V3 p[4] = {a + o0 * r, b + o0 * r, b + o1 * r, a + o1 * r};
        float uu0 = circ * i / seg / TEX_REPEAT_M, uu1 = circ * (i + 1) / seg / TEX_REPEAT_M;
        float vv = len / TEX_REPEAT_M;
        float us[4] = {uu0, uu0, uu1, uu1};
        float vs[4] = {0, vv, vv, 0};
        // боковая грань светлее на стороне, обращённой вверх
        float up = 0.78f + 0.22f * Clamp01(0.5f * (o0.y + o1.y) * 0.5f + 0.5f);
        Quad(t, p, us, vs, tint, shade * up, 0.0f, -1);
    }
    for (int cap = 0; cap < 2; ++cap) {
        if ((cap == 0 && !capA) || (cap == 1 && !capB)) continue;
        V3 c = cap == 0 ? a : b;
        for (int i = 0; i < seg; ++i) {
            float a0 = 2.0f * 3.14159265f * i / seg, a1 = 2.0f * 3.14159265f * (i + 1) / seg;
            V3 p0 = c + (u * cosf(a0) + v * sinf(a0)) * r, p1 = c + (u * cosf(a1) + v * sinf(a1)) * r;
            V3 p[4] = {c, p0, p1, p1};
            float us[4] = {0.5f, 0.5f, 0.5f, 0.5f}, vs[4] = {0.5f, 0.5f, 0.5f, 0.5f};
            Quad(t, p, us, vs, tint, shade * 0.7f, 0.0f, -1);
        }
    }
}

void Scene::Pipe(V3 a, V3 b, float r, Rgb tint, bool flangeA, bool flangeB, int seg) {
    V3 axis = b - a;
    float len = Len(axis);
    if (len < 0.02f) return;
    V3 dir = axis * (1.0f / len);
    Cylinder(TEX_METAL, a, b, r, seg, tint);
    const Rgb collar = {(uint8_t)std::min(255, tint.r + 38), (uint8_t)std::min(255, tint.g + 38), (uint8_t)std::min(255, tint.b + 38)};
    // хомуты через ~2.4 м
    int clamps = (int)(len / 2.4f);
    for (int i = 1; i <= clamps; ++i) {
        float s = len * i / (clamps + 1);
        Cylinder(TEX_METAL, a + dir * (s - 0.025f), a + dir * (s + 0.025f), r * 1.14f, seg, collar, false, false);
    }
    // фланцы: ступенчатый диск + прокладка, не выступающий из трубы «в никуда»
    const Rgb gasket = {28, 30, 31};
    if (flangeA) {
        Cylinder(TEX_METAL, a, a + dir * 0.045f, r * 1.55f, seg, collar, true, true);
        Cylinder(TEX_METAL, a + dir * 0.045f, a + dir * 0.062f, r * 1.2f, seg, gasket, false, true);
    }
    if (flangeB) {
        Cylinder(TEX_METAL, b - dir * 0.045f, b, r * 1.55f, seg, collar, true, true);
        Cylinder(TEX_METAL, b - dir * 0.062f, b - dir * 0.045f, r * 1.2f, seg, gasket, true, false);
    }
}

// ======================= Шум и растровые примитивы =======================
static uint32_t HashU(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static float H2(int x, int y, uint32_t s) {
    return (HashU((uint32_t)x * 374761393U ^ (uint32_t)y * 668265263U ^ (s * 2246822519U + 3266489917U)) & 0xFFFFFF) / 16777216.0f;
}
static float VNoise(float x, float y, uint32_t s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = H2(xi, yi, s), b = H2(xi + 1, yi, s), c = H2(xi, yi + 1, s), d = H2(xi + 1, yi + 1, s);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
static float Fbm(float x, float y, uint32_t s, int oct = 4) {
    float amp = 0.5f, f = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < oct; ++i) {
        sum += amp * VNoise(x * f, y * f, s + 131u * i);
        norm += amp; f *= 2.03f; amp *= 0.5f;
    }
    return sum / norm;
}
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float Next() { s = HashU(s + 0x9E3779B9u); return (s & 0xFFFFFF) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * Next(); }
};

static float DistSeg(float px, float py, float ax, float ay, float bx, float by, float* t) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float k = l2 > 1e-6f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    k = Clamp01(k);
    if (t) *t = k;
    float qx = ax + dx * k - px, qy = ay + dy * k - py;
    return sqrtf(qx * qx + qy * qy);
}

// Полилиния-штрих с переменной шириной. Ширина каждого сегмента считается один раз (Finalize), а не на каждый пиксель.
struct Stroke {
    std::vector<float> x, y, wt;
    float w0 = 1, w1 = 1;
    uint32_t seed = 0;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, maxw = 0;
    void Finalize() {
        const size_t n = x.size();
        wt.assign(n - 1, 1.0f);
        for (size_t i = 0; i + 1 < n; ++i) {
            const float s = (i + 0.5f) / (float)(n - 1);
            const float w = (w0 + (w1 - w0) * s) * (0.62f + 0.95f * VNoise(s * 7.0f, 3.3f, seed));
            wt[i] = std::max(w, 0.4f); maxw = std::max(maxw, wt[i]);
        }
        for (size_t i = 0; i < n; ++i) { minx = std::min(minx, x[i]); maxx = std::max(maxx, x[i]); miny = std::min(miny, y[i]); maxy = std::max(maxy, y[i]); }
    }
    // расстояние до штриха, нормированное на локальную ширину (1.0 = край штриха)
    float NormDist(float px, float py) const {
        float best = 1e9f;
        const size_t n = x.size();
        for (size_t i = 0; i + 1 < n; ++i) {
            const float d = DistSeg(px, py, x[i], y[i], x[i + 1], y[i + 1], nullptr);
            best = std::min(best, d / wt[i]);
        }
        return best;
    }
};

static void BlendPixel(uint8_t* px, float r, float g, float b, float a) {
    a = Clamp01(a);
    px[0] = (uint8_t)(px[0] * (1 - a) + r * a);
    px[1] = (uint8_t)(px[1] * (1 - a) + g * a);
    px[2] = (uint8_t)(px[2] * (1 - a) + b * a);
}

void Blit(Pixmap& dst, const Pixmap& src, int dx, int dy) {
    for (int y = 0; y < src.h; ++y) {
        int yy = dy + y;
        if (yy < 0 || yy >= dst.h) continue;
        for (int x = 0; x < src.w; ++x) {
            int xx = dx + x;
            if (xx < 0 || xx >= dst.w) continue;
            std::memcpy(dst.At(xx, yy), src.At(x, y), 4);
        }
    }
}

void AtlasSlotOrigin(int slot, int* px, int* py) {
    *px = (slot % ATLAS_COLS) * WALL_TILE_W;
    *py = (slot / ATLAS_COLS) * WALL_TILE_H;
}
void AtlasSlotRect(int slot, float* u0, float* v0, float* u1, float* v1) {
    int px, py; AtlasSlotOrigin(slot, &px, &py);
    const float hx = 0.5f / ATLAS_W, hy = 0.5f / ATLAS_H;   // полпикселя внутрь: без подтекания соседей
    *u0 = px / (float)ATLAS_W + hx;
    *v0 = py / (float)ATLAS_H + hy;
    *u1 = (px + WALL_TILE_W) / (float)ATLAS_W - hx;
    *v1 = (py + WALL_TILE_H) / (float)ATLAS_H - hy;
}

// ======================= Плесневая стрелка =======================
void ComposeMoldArrow(Pixmap& base, uint32_t seed, bool pointRight) {
    const int W = base.w, H = base.h;
    Rng rng(seed);
    // Геометрия задаётся для направления вправо, для влево — отражается ТОЛЬКО скелет штрихов (сама текстура
    // генерируется заново, а не зеркалится): мазки получаются разными.
    const float cy = H * 0.5f + rng.Range(-22.0f, 22.0f);
    const float len = rng.Range(300.0f, 345.0f);
    const float tailX = W * 0.5f - len * 0.5f + rng.Range(-12.0f, 12.0f);
    const float tipX = tailX + len;
    const float bend = rng.Range(-26.0f, 26.0f);
    auto X = [&](float x) { return pointRight ? x : (W - x); };

    std::vector<Stroke> strokes;
    // Ствол: квадратичная кривая от хвоста к острию с лёгким изгибом.
    {
        Stroke s; s.seed = seed + 11; s.w0 = rng.Range(12.0f, 16.0f); s.w1 = rng.Range(14.0f, 19.0f);
        const int N = 30;
        for (int i = 0; i <= N; ++i) {
            float t = (float)i / N;
            float x = tailX + (tipX - tailX) * t;
            float y = cy + bend * 4.0f * t * (1.0f - t) + (VNoise(t * 6.0f, 1.7f, seed) - 0.5f) * 7.0f;
            s.x.push_back(X(x)); s.y.push_back(y);
        }
        s.Finalize();
        strokes.push_back(s);
    }
    // Два «крыла» головы: от острия назад под углом, к концу сужаются.
    const float headLen = rng.Range(105.0f, 130.0f);
    for (int side = -1; side <= 1; side += 2) {
        Stroke s; s.seed = seed + 21 + side; s.w0 = rng.Range(14.0f, 18.0f); s.w1 = rng.Range(3.2f, 5.2f);
        float ang = (side < 0 ? 1 : -1) * rng.Range(0.55f, 0.72f);
        float ex = tipX - cosf(ang) * headLen, ey = cy + bend * 0.0f + sinf(ang) * headLen * (side < 0 ? 1 : 1);
        ey = cy + (side < 0 ? -1.0f : 1.0f) * fabsf(sinf(ang)) * headLen;
        const int N = 16;
        for (int i = 0; i <= N; ++i) {
            float t = (float)i / N;
            float x = tipX + (ex - tipX) * t;
            float y = cy + (ey - cy) * t + (VNoise(t * 5.0f, 8.1f + side, seed) - 0.5f) * 6.0f;
            s.x.push_back(X(x)); s.y.push_back(y);
        }
        s.Finalize();
        strokes.push_back(s);
    }

    // Подтёки: короткие вертикальные «слёзы» из нескольких точек ствола.
    struct Drip { float x, y, len, w; };
    std::vector<Drip> drips;
    for (int i = 0; i < 4; ++i) {
        const Stroke& sh = strokes[0];
        size_t idx = (size_t)(rng.Range(0.15f, 0.9f) * (sh.x.size() - 1));
        drips.push_back({sh.x[idx] + rng.Range(-6, 6), sh.y[idx], rng.Range(22.0f, 78.0f), rng.Range(1.6f, 3.2f)});
    }
    // Спутниковые пятна.
    struct Blob { float x, y, r; };
    std::vector<Blob> blobs;
    for (int i = 0; i < 22; ++i) {
        const Stroke& sh = strokes[(size_t)(rng.Next() * 3) % 3];
        size_t idx = (size_t)(rng.Next() * (sh.x.size() - 1));
        float ang = rng.Range(0.0f, 6.2831f), dist = rng.Range(14.0f, 85.0f);
        blobs.push_back({sh.x[idx] + cosf(ang) * dist, sh.y[idx] + sinf(ang) * dist, rng.Range(1.6f, 6.5f)});
    }

    // Общая рамка влияния: пиксели вне неё не меняются (экономит большую часть работы).
    float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f, reach = 0;
    for (const Stroke& s : strokes) { bx0 = std::min(bx0, s.minx); bx1 = std::max(bx1, s.maxx); by0 = std::min(by0, s.miny); by1 = std::max(by1, s.maxy); reach = std::max(reach, s.maxw); }
    for (const Drip& d : drips) { bx0 = std::min(bx0, d.x - 8); bx1 = std::max(bx1, d.x + 8); by1 = std::max(by1, d.y + d.len + 6); }
    for (const Blob& b : blobs) { bx0 = std::min(bx0, b.x - b.r - 3); bx1 = std::max(bx1, b.x + b.r + 3); by0 = std::min(by0, b.y - b.r - 3); by1 = std::max(by1, b.y + b.r + 3); }
    const float margin = 4.6f * reach + 22.0f;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float fx = (float)x, fy = (float)y;
            if (fx < bx0 - margin || fx > bx1 + margin || fy < by0 - margin || fy > by1 + margin) continue;
            // слабое искажение координат даёт «живой» неровный край
            float wx = fx + (Fbm(fx * 0.035f, fy * 0.035f, seed + 3, 3) - 0.5f) * 20.0f;
            float wy = fy + (Fbm(fx * 0.035f + 9.0f, fy * 0.035f, seed + 4, 3) - 0.5f) * 20.0f;
            float best = 1e9f;
            for (const Stroke& s : strokes) best = std::min(best, s.NormDist(wx, wy));
            float edgeNoise = Fbm(fx * 0.16f, fy * 0.16f, seed + 7, 3) - 0.5f;
            float core = SmoothStep(0.0f, 0.42f, (1.08f - best) + edgeNoise * 0.95f);
            // подтёки и пятна
            float extra = 0.0f;
            for (const Drip& d : drips) {
                float t; float dd = DistSeg(wx, wy, d.x, d.y, d.x + (VNoise(d.y, 1, seed) - 0.5f) * 6.0f, d.y + d.len, &t);
                float w = d.w * (1.0f - 0.7f * t);
                extra = std::max(extra, SmoothStep(w + 1.2f, w * 0.5f, dd));
            }
            for (const Blob& b : blobs) {
                float dd = hypotf(wx - b.x, wy - b.y);
                extra = std::max(extra, SmoothStep(b.r + 1.5f, b.r * 0.4f, dd) * (0.55f + 0.45f * edgeNoise * 2.0f));
            }
            if (best > 4.6f && extra < 0.01f) continue;   // вне штриха, подтёков и каймы
            // споровая кайма: мелкая зернистая тень вокруг штриха
            float fringe = SmoothStep(3.1f, 1.0f, best) * SmoothStep(0.52f, 0.80f, VNoise(fx * 0.55f, fy * 0.55f, seed + 17) + 0.25f * edgeNoise) * 0.55f;
            float halo = 0.30f * SmoothStep(4.5f, 1.2f, best) * (0.5f + Fbm(fx * 0.05f, fy * 0.05f, seed + 29, 2));
            float alpha = Clamp01(std::max(std::max(core, extra), fringe)) * 0.96f;
            uint8_t* px = base.At(x, y);
            // сначала расплывшееся потемнение бетона
            BlendPixel(px, px[0] * 0.55f, px[1] * 0.58f, px[2] * 0.56f, halo);
            float tone = Fbm(fx * 0.09f, fy * 0.09f, seed + 41, 2);
            BlendPixel(px, 9.0f + 16.0f * tone, 12.0f + 16.0f * tone, 11.0f + 13.0f * tone, alpha);
        }
    }
}

// ======================= Следы крови =======================
static void Ellipse(Pixmap& img, float cx, float cy, float rx, float ry, float rot, float alphaMul, uint32_t seed, float cr, float cg, float cb) {
    const float cs = cosf(rot), sn = sinf(rot);
    int x0 = std::max(0, (int)(cx - std::max(rx, ry) - 4)), x1 = std::min(img.w - 1, (int)(cx + std::max(rx, ry) + 4));
    int y0 = std::max(0, (int)(cy - std::max(rx, ry) - 4)), y1 = std::min(img.h - 1, (int)(cy + std::max(rx, ry) + 4));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float dx = x - cx, dy = y - cy;
        float lx = (dx * cs + dy * sn) / rx, ly = (-dx * sn + dy * cs) / ry;
        float d = sqrtf(lx * lx + ly * ly);
        float n = Fbm(x * 0.09f, y * 0.09f, seed, 2) - 0.5f;
        float a = SmoothStep(1.0f, 0.55f, d + n * 0.55f) * alphaMul;
        if (a > 0.003f) BlendPixel(img.At(x, y), cr, cg, cb, a);
    }
}

static void DryBloodColor(float tone, float* r, float* g, float* b) {
    // тёмная запёкшаяся кровь: бордовый -> почти чёрно-коричневый, без яркого красного
    *r = 70.0f + 38.0f * tone; *g = 9.0f + 6.0f * tone; *b = 11.0f + 6.0f * tone;
}

void ComposeWallBlood(Pixmap& base, uint32_t seed) {
    Rng rng(seed);
    float r, g, b; DryBloodColor(0.35f, &r, &g, &b);
    const float hx = base.w * 0.30f, hy = base.h * 0.55f;
    // смазанный след: копии отпечатка, сдвигающиеся вниз-вправо с затуханием
    for (int i = 0; i < 18; ++i) {
        float t = i / 17.0f;
        float a = 0.55f * (1.0f - t) * (1.0f - t) + 0.03f;
        Ellipse(base, hx + t * 55.0f, hy + t * 120.0f, 38.0f, 46.0f, 0.2f, a, seed + 1, r, g, b);
    }
    // пальцы
    const float fingerAng[5] = {-0.95f, -0.45f, 0.0f, 0.42f, 0.9f};
    for (int i = 0; i < 5; ++i) {
        float fx = hx + sinf(fingerAng[i]) * 52.0f, fy = hy - cosf(fingerAng[i]) * 62.0f - (i == 0 ? -12.0f : 0.0f);
        Ellipse(base, fx, fy, 9.0f, 25.0f, fingerAng[i], 0.78f, seed + 2 + i, r, g, b);
        // подтёк из кончика пальца
        float dl = rng.Range(25.0f, 90.0f);
        for (float t = 0; t < dl; t += 1.0f)
            Ellipse(base, fx + sinf(fingerAng[i]) * 6.0f * 0.2f, fy + 20.0f + t, 2.2f * (1.0f - t / dl) + 0.6f, 2.0f, 0.0f, 0.5f, seed + 9 + i, r, g, b);
    }
    Ellipse(base, hx, hy, 40.0f, 48.0f, 0.1f, 0.82f, seed + 20, r, g, b);
}

void ComposeFloorBlood(Pixmap& base, int variant, uint32_t seed) {
    Rng rng(seed);
    float r, g, b; DryBloodColor(0.3f, &r, &g, &b);
    const int W = base.w, H = base.h;   // v=0 верх картинки = север (сторона двери)
    auto footprint = [&](float cx, float cy, float rot, float a) {
        // подошва + каблук; rot = направление «вперёд» (к северу = -y)
        Ellipse(base, cx, cy, 15.0f, 36.0f, rot, a, seed + 5, r, g, b);
        float hx = cx - sinf(rot) * 0.0f + sinf(rot) * 52.0f, hy = cy + cosf(rot) * 52.0f;
        Ellipse(base, hx, hy, 12.0f, 13.0f, rot, a * 0.9f, seed + 6, r, g, b);
    };
    if (variant == 0) {
        // ниша: полоса волочения с небольшим изгибом к двери и лужа у порога
        for (int y = H; y > 20; y -= 2) {
            float t = (float)y / H;
            float cx = W * 0.50f + sinf(t * 5.0f + 0.4f) * 28.0f * t;
            float w = 24.0f + 10.0f * sinf(t * 9.0f) + 12.0f * t;
            float a = 0.50f * (0.35f + 0.65f * t);
            Ellipse(base, cx, (float)y, w, 6.5f, 0.0f, a * 0.55f, seed + 3, r, g, b);
        }
        Ellipse(base, W * 0.50f, H * 0.10f, 62.0f, 38.0f, 0.1f, 0.78f, seed + 4, r, g, b);
        footprint(W * 0.34f, H * 0.62f, -0.10f, 0.62f);
        footprint(W * 0.67f, H * 0.80f, 0.12f, 0.55f);
        for (int i = 0; i < 14; ++i) Ellipse(base, rng.Range(W * 0.2f, W * 0.8f), rng.Range(H * 0.25f, H * 0.95f), rng.Range(2, 6), rng.Range(2, 6), 0, 0.7f, seed + 30 + i, r, g, b);
    } else {
        // клетка перед нишей: цепочка отпечатков, ведущая с юго-запада к двери, и редкие капли
        for (int i = 0; i < 5; ++i) {
            float t = i / 4.0f;
            float cx = W * (0.12f + 0.44f * t) + ((i & 1) ? 22.0f : -22.0f);
            float cy = H * (0.92f - 0.80f * t);
            footprint(cx, cy, 0.30f * (1.0f - t) - 0.06f, 0.55f * (0.55f + 0.45f * t));
        }
        for (int i = 0; i < 20; ++i) Ellipse(base, rng.Range(W * 0.1f, W * 0.7f), rng.Range(H * 0.05f, H * 0.95f), rng.Range(1.5f, 5.0f), rng.Range(1.5f, 5.0f), 0, 0.6f, seed + 50 + i, r, g, b);
    }
}

// ======================= Векторный шрифт =======================
namespace {
struct Seg { float x0, y0, x1, y1; };
struct Glyph { char c; std::vector<Seg> s; };

// Контуры букв в коробке w = 0.6, h = 1.0 (y вниз). Ломаные записаны точками, соседние точки соединяются.
static void PolyToSegs(std::vector<Seg>& out, std::initializer_list<float> pts) {
    std::vector<float> p(pts);
    for (size_t i = 0; i + 3 < p.size(); i += 2) out.push_back({p[i], p[i + 1], p[i + 2], p[i + 3]});
}
static const std::vector<Glyph>& Glyphs() {
    static std::vector<Glyph> g;
    if (!g.empty()) return g;
    auto add = [&](char c, std::vector<std::initializer_list<float>> polys) {
        Glyph gl; gl.c = c;
        for (auto& p : polys) PolyToSegs(gl.s, p);
        g.push_back(gl);
    };
    add('F', {{0, 1, 0, 0, 0.55f, 0}, {0, 0.5f, 0.45f, 0.5f}});
    add('I', {{0.12f, 0, 0.48f, 0}, {0.30f, 0, 0.30f, 1}, {0.12f, 1, 0.48f, 1}});
    add('R', {{0, 1, 0, 0, 0.42f, 0, 0.55f, 0.08f, 0.55f, 0.38f, 0.42f, 0.5f, 0, 0.5f}, {0.28f, 0.5f, 0.58f, 1}});
    add('E', {{0.55f, 0, 0, 0, 0, 1, 0.55f, 1}, {0, 0.5f, 0.42f, 0.5f}});
    add('X', {{0, 0, 0.6f, 1}, {0.6f, 0, 0, 1}});
    add('T', {{0, 0, 0.6f, 0}, {0.3f, 0, 0.3f, 1}});
    add('K', {{0, 0, 0, 1}, {0.55f, 0, 0, 0.55f}, {0.15f, 0.42f, 0.6f, 1}});
    add('P', {{0, 1, 0, 0, 0.42f, 0, 0.55f, 0.08f, 0.55f, 0.4f, 0.42f, 0.5f, 0, 0.5f}});
    add('C', {{0.55f, 0.15f, 0.45f, 0.03f, 0.3f, 0, 0.12f, 0.04f, 0.02f, 0.2f, 0, 0.5f, 0.02f, 0.8f, 0.12f, 0.96f, 0.3f, 1, 0.45f, 0.97f, 0.55f, 0.85f}});
    add('L', {{0, 0, 0, 1, 0.55f, 1}});
    add('A', {{0, 1, 0.3f, 0, 0.6f, 1}, {0.12f, 0.65f, 0.48f, 0.65f}});
    add('V', {{0, 0, 0.3f, 1, 0.6f, 0}});
    add('0', {{0.3f, 0, 0.5f, 0.1f, 0.58f, 0.35f, 0.58f, 0.65f, 0.5f, 0.9f, 0.3f, 1, 0.1f, 0.9f, 0.02f, 0.65f, 0.02f, 0.35f, 0.1f, 0.1f, 0.3f, 0}});
    return g;
}
const float GLYPH_W = 0.6f, GLYPH_GAP = 0.30f, SPACE_W = 0.45f;
}

float TextWidth(const char* text, float height) {
    float w = 0.0f;
    for (const char* c = text; *c; ++c) {
        w += (*c == ' ') ? SPACE_W * height : (GLYPH_W + GLYPH_GAP) * height;
    }
    return w > 0 ? w - GLYPH_GAP * height : 0.0f;
}

void DrawText(Pixmap& img, const char* text, float x, float y, float height, float stroke, Rgb color) {
    float penX = x;
    for (const char* c = text; *c; ++c) {
        if (*c == ' ') { penX += SPACE_W * height; continue; }
        const Glyph* gl = nullptr;
        for (const Glyph& g : Glyphs()) if (g.c == *c) { gl = &g; break; }
        if (gl) {
            for (const Seg& s : gl->s) {
                float ax = penX + s.x0 * height, ay = y + s.y0 * height, bx = penX + s.x1 * height, by = y + s.y1 * height;
                int xa = std::max(0, (int)(std::min(ax, bx) - stroke - 2)), xb = std::min(img.w - 1, (int)(std::max(ax, bx) + stroke + 2));
                int ya = std::max(0, (int)(std::min(ay, by) - stroke - 2)), yb = std::min(img.h - 1, (int)(std::max(ay, by) + stroke + 2));
                for (int py = ya; py <= yb; ++py) for (int px = xa; px <= xb; ++px) {
                    float d = DistSeg(px + 0.5f, py + 0.5f, ax, ay, bx, by, nullptr);
                    float cov = Clamp01(stroke * 0.5f - d + 0.5f);
                    if (cov > 0.0f) BlendPixel(img.At(px, py), color.r, color.g, color.b, cov);
                }
            }
        }
        penX += (GLYPH_W + GLYPH_GAP) * height;
    }
}

void MakeSign(Pixmap& out, int kind) {
    out.Resize(768, 256);
    const int W = out.w, H = out.h;
    Rgb bg, border, fg; const char* text; float th, st;
    if (kind == 0)      { bg = {22, 96, 54};  border = {236, 241, 232}; fg = {244, 247, 240}; text = "FIRE EXIT";  th = 118; st = 15; }
    else if (kind == 1) { bg = {216, 174, 26}; border = {20, 19, 16};   fg = {22, 20, 16};    text = "KEEP CLEAR"; th = 96;  st = 13; }
    else                { bg = {30, 44, 60};  border = {128, 188, 234}; fg = {152, 212, 255}; text = "LEVEL 0";    th = 108; st = 14; }
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        uint8_t* p = out.At(x, y);
        bool edge = x < 12 || y < 12 || x >= W - 12 || y >= H - 12;
        bool inner = (x >= 12 && x < 20) || (y >= 12 && y < 20) || (x >= W - 20 && x < W - 12) || (y >= H - 20 && y < H - 12);
        Rgb c = bg;
        if (kind == 1 && (y < 40 || y >= H - 40)) {   // чёрно-жёлтые диагональные полосы предупреждения
            int band = ((x + y) / 34) & 1;
            c = band ? Rgb{20, 19, 16} : bg;
        } else if (edge) c = kind == 1 ? Rgb{20, 19, 16} : border;
        else if (inner && kind != 1) c = border;
        float grain = 0.93f + 0.07f * VNoise(x * 0.4f, y * 0.4f, 77);   // лёгкая «эмалевая» зернистость
        p[0] = (uint8_t)(c.r * grain); p[1] = (uint8_t)(c.g * grain); p[2] = (uint8_t)(c.b * grain); p[3] = 255;
    }
    float tw = TextWidth(text, th);
    const float maxW = (float)W - 110.0f;               // текст обязан целиком помещаться внутри рамки таблички
    if (tw > maxW) { const float k = maxW / tw; th *= k; st *= k; tw = TextWidth(text, th); }
    DrawText(out, text, (W - tw) * 0.5f, (H - th) * 0.5f, th, st, fg);
}

} // namespace l2g
