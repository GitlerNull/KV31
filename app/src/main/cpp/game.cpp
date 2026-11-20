// game.cpp - главный цикл (while !WindowShouldClose), рендер мира, туман-шейдер, рассудок, HUD.
// Стены/пол/потолок рисуются квадами через rlgl (только видимые грани) с процедурными текстурами.
// В raylib нет rlEnableFog: туман и свет (лампы + фонарик) считаются в шейдере GLSL ES 1.00.
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "game.h"
#include "util.h"
#include "map_gen.h"
#include "player.h"
#include "entities.h"
#include "procgen.h"
#include "level2.h"

Vector3 gOrg = {0, 0, 0};
bool gRu = false;

namespace {
Font gFont;
ProcAssets gA;
Texture2D gPortrait{};
Texture2D gWallCaption{};
Shader sh;
int lView, lDir, lFlash, lFlick, lFogD, lFogC, lUnlit;
char gMsg[256]; float gMsgT = 0;
enum State { ST_MENU, ST_LEVEL_SELECT, ST_SETTINGS, ST_PLAY, ST_DEAD, ST_WON } state = ST_MENU;
struct GameSettings {
    int viewChunks = 2;                 // радиус в чанках: 1..3
    bool otherEntities = true;          // все сущности, кроме Физички
    float sensitivity = 0.0035f;        // текущее значение управления по умолчанию
};
GameSettings settings;
bool settingsDirty = false;
static constexpr int SETTINGS_MAGIC = 1263948337;
static constexpr int VIEW_CHUNKS_MIN = 1;
static constexpr int VIEW_CHUNKS_MAX = 3;
static constexpr float SENS_MIN = 0.0010f;
static constexpr float SENS_MAX = 0.0080f;
}

// vertex-цвет: rgb = оттенок, a = запечённая яркость ламп (смешивание отключено на время 3D-прохода)
static const char* VS = R"(#version 100
attribute vec3 vertexPosition; attribute vec2 vertexTexCoord; attribute vec4 vertexColor;
uniform mat4 mvp;
varying vec3 fragPos; varying vec2 fragUV; varying vec4 fragColor;
void main(){ fragPos = vertexPosition; fragUV = vertexTexCoord; fragColor = vertexColor; gl_Position = mvp*vec4(vertexPosition,1.0); }
)";
static const char* FS = R"(#version 100
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec3 fragPos; varying vec2 fragUV; varying vec4 fragColor;
uniform sampler2D texture0;
uniform vec3 viewPos; uniform vec3 camDir; uniform vec3 fogColor;
uniform float flash; uniform float flicker; uniform float fogDensity; uniform float unlit;
void main(){
    vec4 texel = texture2D(texture0, fragUV);
    if (texel.a < 0.05) discard;
    vec3 tex = texel.rgb * fragColor.rgb;
    float d = length(fragPos - viewPos);
    vec3 dir = (fragPos - viewPos) / max(d, 0.001);
    float cone = smoothstep(0.88, 0.965, dot(dir, camDir));
    float spot = flash * cone * (1.2 / (1.0 + 0.06*d*d));
    vec3 light = vec3(fragColor.a * flicker * 0.95) * vec3(1.0, 0.97, 0.85) + vec3(spot) * vec3(1.0, 0.96, 0.85);
    vec3 c = (unlit > 0.5) ? tex : tex * (light + 0.02);
    float k = d * fogDensity;
    c = mix(c, fogColor, clamp(1.0 - exp(-k*k), 0.0, 1.0));
    gl_FragColor = vec4(c, texel.a);
}
)";

void Say(const char* m, float s) { snprintf(gMsg, sizeof gMsg, "%s", m); gMsgT = s; }
static float Frand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.0f; }
static Color Col(int r, int g, int b, float L) { L = L < 0 ? 0 : L > 1 ? 1 : L; return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)(L * 255)}; }

// Сначала пробуем assets/font.ttf, затем системный шрифт Android с кириллицей.
static bool LoadUiFont() {
    int n = 0; unsigned char* d = LoadFileData("font.ttf", &n);
    bool raylibData = d != nullptr;
    if (!d || n <= 0) {
        if (d) UnloadFileData(d);
        d = LoadSystemFontData(&n);
        raylibData = false;
    }
    if (!d || n <= 0) { if (d) free(d); return false; }
    int cps[200], c = 0;
    for (int i = 32; i < 127; i++) cps[c++] = i;
    for (int i = 0x410; i <= 0x44F; i++) cps[c++] = i;
    cps[c++] = 0x401; cps[c++] = 0x451;
    gFont = LoadFontFromMemory(".ttf", d, n, 40, cps, c);
    if (raylibData) UnloadFileData(d); else free(d);
    if (gFont.texture.id == 0) return false;
    SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);
    return true;
}
static void Txt(const char* s, float x, float y, float sz, Color c, int align = 0) {
    Font f = gRu ? gFont : GetFontDefault();
    Vector2 m = MeasureTextEx(f, s, sz, sz / 12);
    if (align == 1) x -= m.x / 2; else if (align == 2) x -= m.x;
    DrawTextEx(f, s, Vector2{x, y}, sz, sz / 12, c);
}

static void LoadWallCaption() {
    if (!gRu) {
        TraceLog(LOG_WARNING, "Wall caption skipped: Russian font unavailable");
        return;
    }

    const char* caption = "ФИЗИКУ УЧИЛ?!";
    Image image = GenImageColor(1024, 192, BLANK);

    float fontSize = 128.0f;
    Vector2 size = MeasureTextEx(gFont, caption, fontSize, 2.0f);

    while (size.x > 960.0f && fontSize > 64.0f) {
        fontSize -= 2.0f;
        size = MeasureTextEx(gFont, caption, fontSize, 2.0f);
    }

    Vector2 pos = {
        (1024.0f - size.x) * 0.5f,
        (192.0f - fontSize) * 0.5f
    };

    ImageDrawTextEx(&image, gFont, caption, pos, fontSize, 2.0f, BLACK);
    gWallCaption = LoadTextureFromImage(image);
    UnloadImage(image);

    if (gWallCaption.id == 0)
        TraceLog(LOG_WARNING, "Wall caption texture could not be created");
}

// ---------- 3D ----------
static Vector3 Wv(float x, float y, float z) { return {x - gOrg.x, y, z - gOrg.z}; }   // плавающее начало координат

static void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float la, float lb, float lc, float ld, Color t, float us, float vs) {
    auto q8 = [](float l) { return (unsigned char)(fminf(1.0f, fmaxf(0.0f, l)) * 255); };
    rlCheckRenderBatchLimit(4);
    rlBegin(RL_QUADS);
    rlColor4ub(t.r, t.g, t.b, q8(la)); rlTexCoord2f(0, vs);  rlVertex3f(a.x, a.y, a.z);
    rlColor4ub(t.r, t.g, t.b, q8(lb)); rlTexCoord2f(us, vs); rlVertex3f(b.x, b.y, b.z);
    rlColor4ub(t.r, t.g, t.b, q8(lc)); rlTexCoord2f(us, 0);  rlVertex3f(c.x, c.y, c.z);
    rlColor4ub(t.r, t.g, t.b, q8(ld)); rlTexCoord2f(0, 0);   rlVertex3f(d.x, d.y, d.z);
    rlEnd();
}


// Отдельный quad с явно заданными UV — для непрерывной мировой раскладки обоев.
static void QuadUV(Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                   float la, float lb, float lc, float ld, Color t,
                   float ua, float va, float ub, float vb,
                   float uc, float vc, float ud, float vd) {
    auto q8 = [](float l) {
        return (unsigned char)(fminf(1.0f, fmaxf(0.0f, l)) * 255);
    };
    rlCheckRenderBatchLimit(4);
    rlBegin(RL_QUADS);
    rlColor4ub(t.r, t.g, t.b, q8(la)); rlTexCoord2f(ua, va); rlVertex3f(a.x, a.y, a.z);
    rlColor4ub(t.r, t.g, t.b, q8(lb)); rlTexCoord2f(ub, vb); rlVertex3f(b.x, b.y, b.z);
    rlColor4ub(t.r, t.g, t.b, q8(lc)); rlTexCoord2f(uc, vc); rlVertex3f(c.x, c.y, c.z);
    rlColor4ub(t.r, t.g, t.b, q8(ld)); rlTexCoord2f(ud, vd); rlVertex3f(d.x, d.y, d.z);
    rlEnd();
}

static int ViewRadiusCells() { return settings.viewChunks * CHUNK; } // один чанк = 16 клеток

static void DrawLevel(const Player& pl, float tanHalfFovX) {
    int pcx = CellOf(pl.pos.x), pcz = CellOf(pl.pos.z), R = ViewRadiusCells();
    Color white = {255, 255, 255, 255};
    int bx0 = FloorDiv(pcx - R, 3), bx1 = FloorDiv(pcx + R, 3), bz0 = FloorDiv(pcz - R, 3), bz1 = FloorDiv(pcz + R, 3);
    const float B = 3 * CELL;
    // Пол: существующая раскладка ковра без изменений.
    rlSetTexture(gA.carpet.id);
    for (int bz = bz0; bz <= bz1; bz++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            float x0 = bx * B, x1 = x0 + B;
            float z0 = bz * B, z1 = z0 + B;
            float tone = 0.975f + 0.025f * Rand01(bx, bz, 91);
            Color carpetTint = {
                (unsigned char)(255.0f * tone),
                (unsigned char)(255.0f * tone),
                (unsigned char)(255.0f * tone), 255
            };
            Quad(Wv(x0, 0.0f, z0), Wv(x1, 0.0f, z0),
                 Wv(x1, 0.0f, z1), Wv(x0, 0.0f, z1),
                 LightAt(x0, z0) * 0.9f, LightAt(x1, z0) * 0.9f,
                 LightAt(x1, z1) * 0.9f, LightAt(x0, z1) * 0.9f,
                 carpetTint, 3, 3);
        }
    }

    // Потолок разбит на фиксированные мировые секции по 3 x 3 м.
    // Освещение каждой вершины определяется её мировыми координатами,
    // поэтому перемещение границ области прорисовки не меняет яркость
    // всего потолка одним скачком.
    const float tileSize = 0.60f;
    const float ceilingY = WALL_H;
    rlSetTexture(gA.ceiling.id);
    const Color ceilingTint = {207, 202, 164, 255};

    for (int bz = bz0; bz <= bz1; ++bz) {
        for (int bx = bx0; bx <= bx1; ++bx) {
            const float x0 = bx * B;
            const float x1 = x0 + B;
            const float z0 = bz * B;
            const float z1 = z0 + B;

            const float u0 = x0 / tileSize;
            const float u1 = x1 / tileSize;
            const float v0 = -z0 / tileSize;
            const float v1 = -z1 / tileSize;

            QuadUV(Wv(x0, ceilingY, z0), Wv(x1, ceilingY, z0),
                   Wv(x1, ceilingY, z1), Wv(x0, ceilingY, z1),
                   LightAt(x0, z0) * 0.8f,
                   LightAt(x1, z0) * 0.8f,
                   LightAt(x1, z1) * 0.8f,
                   LightAt(x0, z1) * 0.8f,
                   ceilingTint,
                   u0, v0, u1, v0,
                   u1, v1, u0, v1);
        }
    }
    rlSetTexture(gA.wall.id);
    Vector3 viewForward = Forward(pl);
    const float horizontalLength = sqrtf(viewForward.x * viewForward.x +
                                         viewForward.z * viewForward.z);
    const float forwardX = viewForward.x / fmaxf(horizontalLength, 0.0001f);
    const float forwardZ = viewForward.z / fmaxf(horizontalLength, 0.0001f);
    const float sideX = -forwardZ;
    const float sideZ = forwardX;
    auto face = [&](float xa, float za, float xb, float zb, float shade) {
        float la = LightAt(xa, za) * shade, lb = LightAt(xb, zb) * shade;
        float ua = (fabsf(xb - xa) >= fabsf(zb - za) ? xa : za) / CELL;
        float ub = (fabsf(xb - xa) >= fabsf(zb - za) ? xb : zb) / CELL;
        // Обои 3:4: один тайл занимает 2 м по ширине и 2,667 м по высоте.
        // Это сохраняет исходные пропорции изображения без растяжения.
        float vb = -WALL_H / (CELL / 0.75f);
        QuadUV(Wv(xa, 0, za), Wv(xb, 0, zb),
               Wv(xb, WALL_H, zb), Wv(xa, WALL_H, za),
               la, lb, lb * 0.9f, la * 0.9f, white,
               ua, 0.0f, ub, 0.0f,
               ub, vb, ua, vb);
    };
    // Объёмный плинтус с фасеточным округлением верхней кромки.
    auto baseboard = [&](float xa, float za, float xb, float zb,
                         float nx, float nz, float shade,
                         bool capStart, bool capEnd) {
        const float profile[][2] = {
            {0.000f, 0.000f}, {0.045f, 0.000f},
            {0.045f, 0.034f}, {0.042f, 0.045f},
            {0.034f, 0.054f}, {0.023f, 0.060f},
            {0.010f, 0.060f}, {0.000f, 0.052f}
        };
        const int count = sizeof(profile) / sizeof(profile[0]);
        const float lightA = LightAt(xa, za) * shade;
        const float lightB = LightAt(xb, zb) * shade;
        const Color material = {112, 101, 83, 255};

        rlSetTexture(0);
        for (int i = 0; i < count - 1; ++i) {
            const float da = profile[i][0], ya = profile[i][1];
            const float db = profile[i + 1][0], yb = profile[i + 1][1];

            Vector3 a = Wv(xa + nx * da, ya, za + nz * da);
            Vector3 b = Wv(xb + nx * da, ya, zb + nz * da);
            Vector3 c = Wv(xb + nx * db, yb, zb + nz * db);
            Vector3 d = Wv(xa + nx * db, yb, za + nz * db);

            Quad(a, b, c, d, lightA, lightB, lightB, lightA,
                 material, 1.0f, 1.0f);
        }

        auto cap = [&](float x, float z, float light) {
            float centerD = 0.0f, centerY = 0.0f;
            for (int i = 0; i < count; ++i) {
                centerD += profile[i][0];
                centerY += profile[i][1];
            }
            centerD /= count;
            centerY /= count;

            Vector3 center = Wv(x + nx * centerD, centerY,
                                z + nz * centerD);

            for (int i = 0; i < count; ++i) {
                int next = (i + 1) % count;
                Vector3 a = Wv(x + nx * profile[i][0],
                               profile[i][1],
                               z + nz * profile[i][0]);
                Vector3 b = Wv(x + nx * profile[next][0],
                               profile[next][1],
                               z + nz * profile[next][0]);
                Quad(center, a, b, b, light, light, light, light,
                     material, 1.0f, 1.0f);
            }
        };

        if (capStart) cap(xa, za, lightA);
        if (capEnd) cap(xb, zb, lightB);

        rlSetTexture(gA.wall.id);
    };

    const float wallCullMargin = 8.0f;
    for (int cz = pcz - R; cz <= pcz + R; cz++) {
        for (int cx = pcx - R; cx <= pcx + R; cx++) {
            const float centerX = (cx + 0.5f) * CELL;
            const float centerZ = (cz + 0.5f) * CELL;
            const float dx = centerX - pl.pos.x;
            const float dz = centerZ - pl.pos.z;
            const float forwardDist = dx * forwardX + dz * forwardZ;
            const float sideDist = fabsf(dx * sideX + dz * sideZ);

            if (forwardDist < -wallCullMargin ||
                sideDist > fmaxf(0.0f, forwardDist) * tanHalfFovX + wallCullMargin) {
                continue;
            }

            if (!IsSolid(cx, cz)) continue;
            float x0 = cx * CELL, x1 = x0 + CELL, z0 = cz * CELL, z1 = z0 + CELL;

        if (!IsSolid(cx, cz - 1)) {
            face(x0, z0, x1, z0, 1.0f);
            baseboard(x0, z0, x1, z0, 0.0f, -1.0f, 1.0f, !IsSolid(cx - 1, cz), !IsSolid(cx + 1, cz));
        }
        if (!IsSolid(cx, cz + 1)) {
            face(x0, z1, x1, z1, 1.0f);
            baseboard(x0, z1, x1, z1, 0.0f, 1.0f, 1.0f, !IsSolid(cx - 1, cz), !IsSolid(cx + 1, cz));
        }
        if (!IsSolid(cx - 1, cz)) {
            face(x0, z0, x0, z1, 0.85f);
            baseboard(x0, z0, x0, z1, -1.0f, 0.0f, 0.85f, !IsSolid(cx, cz - 1), !IsSolid(cx, cz + 1));
        }
        if (!IsSolid(cx + 1, cz)) {
            face(x1, z0, x1, z1, 0.85f);
            baseboard(x1, z0, x1, z1, 1.0f, 0.0f, 0.85f, !IsSolid(cx, cz - 1), !IsSolid(cx, cz + 1));
        }
        }
    }
    // Портрет Физички: внешняя западная сторона стены тайника,
    // обращённая к стартовой комнате. Дверной проём остаётся свободным.
    if (gPortrait.id > 0 && IsSolid(4, 1)) {
        const float px = 4 * CELL - 0.015f;
        const float z0 = 1 * CELL + 0.20f;
        const float z1 = 2 * CELL - 0.20f;
        const float y0 = 0.45f;
        const float y1 = 2.48f;
        rlSetTexture(gPortrait.id);
        Quad(
            Wv(px, y0, z0), Wv(px, y0, z1),
            Wv(px, y1, z1), Wv(px, y1, z0),
            1.0f, 1.0f, 1.0f, 1.0f, white, 1, 1
        );

        // Чёрная надпись находится на той же стене, немного
        // ближе к камере, чтобы поверхности не конфликтовали.
        if (gWallCaption.id > 0) {
            const float tx = px - 0.012f;
            rlSetTexture(gWallCaption.id);
            rlEnableColorBlend();
            Quad(
                Wv(tx, 2.58f, z0), Wv(tx, 2.58f, z1),
                Wv(tx, 2.88f, z1), Wv(tx, 2.88f, z0),
                1.0f, 1.0f, 1.0f, 1.0f, white, 1, 1
            );
            rlDisableColorBlend();
        }
    }
    rlSetTexture(0);
}

static void DrawItems(const Player& pl, float t, bool glow) {
    const int R = ViewRadiusCells();
    const int cellX = CellOf(pl.pos.x), cellZ = CellOf(pl.pos.z);
    const int minX = FloorDiv(cellX - R, CHUNK);
    const int maxX = FloorDiv(cellX + R, CHUNK);
    const int minZ = FloorDiv(cellZ - R, CHUNK);
    const int maxZ = FloorDiv(cellZ + R, CHUNK);
    const float maxDistance = (float)R * CELL * 1.4142f;
    for (int cz = minZ; cz <= maxZ; ++cz) for (int cx = minX; cx <= maxX; ++cx)
        for (Item& it : GetChunk(cx, cz).items) {
            if (it.taken || it.cx < cellX - R || it.cx > cellX + R || it.cz < cellZ - R || it.cz > cellZ + R) continue;
            float x = (it.cx + 0.5f) * CELL, z = (it.cz + 0.5f) * CELL;
            float dx = x - pl.pos.x, dz = z - pl.pos.z;
            if (dx * dx + dz * dz > maxDistance * maxDistance) continue;
            float L = LightAt(x, z);
            Vector3 p = Wv(x, 0.35f + 0.05f * sinf(t * 2 + it.cx), z);
            if (!glow) {
                if (it.type == IT_WATER) DrawCylinder({p.x, p.y - 0.15f, p.z}, 0.07f, 0.07f, 0.3f, 8, Col(170, 215, 245, L));
                else if (it.type == IT_BATTERY) DrawCube(p, 0.1f, 0.2f, 0.1f, Col(60, 200, 70, L));
                else {
                    DrawCube(p, 0.3f, 0.15f, 0.22f, Col(240, 240, 240, L));
                    DrawCube(p, 0.2f, 0.155f, 0.06f, Col(220, 30, 30, L));
                    DrawCube(p, 0.06f, 0.155f, 0.2f, Col(220, 30, 30, L));
                }
            } else {   // маячок над предметом, чтобы его можно было найти в темноте
                Color g = it.type == IT_WATER ? Col(120, 190, 255, 1) : it.type == IT_BATTERY ? Col(80, 255, 100, 1) : Col(255, 90, 90, 1);
                DrawCube({p.x, p.y + 0.35f, p.z}, 0.05f, 0.05f, 0.05f, g);
            }
        }
}

static void DrawLevel2Items(const Player& pl, float t, bool glow) {
    const int R=ViewRadiusCells();
    const int cellX=CellOf(pl.pos.x), cellZ=CellOf(pl.pos.z);
    const float maxD=(float)R*CELL*1.4142f;
    for(const Item& it:Level2GetItems()) {
        if(it.taken || abs(it.cx-cellX)>R || abs(it.cz-cellZ)>R) continue;
        const float x=(it.cx+0.5f)*CELL, z=(it.cz+0.5f)*CELL;
        if(hypotf(x-pl.pos.x,z-pl.pos.z)>maxD) continue;
        const float floorY=Level2FloorAt(x,z);
        Vector3 p=Wv(x,floorY+0.34f+0.045f*sinf(t*2.0f+it.cx),z);
        const float L=LightAt(x,z);
        if(!glow) {
            if(it.type==IT_WATER) DrawCylinder({p.x,p.y-0.14f,p.z},0.075f,0.075f,0.28f,10,Col(155,207,242,L));
            else if(it.type==IT_BATTERY) DrawCube(p,0.11f,0.20f,0.11f,Col(70,205,95,L));
            else {
                DrawCube(p,0.29f,0.14f,0.21f,Col(235,235,228,L));
                DrawCube(p,0.20f,0.15f,0.055f,Col(210,40,42,L));
                DrawCube(p,0.055f,0.15f,0.19f,Col(210,40,42,L));
            }
        } else {
            Color c=it.type==IT_WATER?Col(120,190,255,1.0f):it.type==IT_BATTERY?Col(90,255,120,1.0f):Col(255,100,100,1.0f);
            DrawCube({p.x,p.y+0.34f,p.z},0.055f,0.055f,0.055f,c);
        }
    }
}

static void DrawExit(const Player& pl, bool glow) {
    const int R = ViewRadiusCells();
    const int cellX = CellOf(pl.pos.x), cellZ = CellOf(pl.pos.z);
    const int minX = FloorDiv(cellX - R, CHUNK);
    const int maxX = FloorDiv(cellX + R, CHUNK);
    const int minZ = FloorDiv(cellZ - R, CHUNK);
    const int maxZ = FloorDiv(cellZ + R, CHUNK);
    const float maxDistance = (float)R * CELL * 1.4142f;
    for (int cz = minZ; cz <= maxZ; ++cz) for (int cx = minX; cx <= maxX; ++cx) {
        Chunk& c = GetChunk(cx, cz);
        if (!c.hasExit || c.exitX < cellX - R || c.exitX > cellX + R || c.exitZ < cellZ - R || c.exitZ > cellZ + R) continue;
        float x = (c.exitX + 0.5f) * CELL, z = (c.exitZ + 0.5f) * CELL;
        float dx = x - pl.pos.x, dz = z - pl.pos.z;
        if (dx * dx + dz * dz > maxDistance * maxDistance) continue;
        if (!glow) DrawCube(Wv(x, 0.01f, z), 1.3f, 0.02f, 1.3f, Col(40, 46, 40, LightAt(x, z)));   // сам люк
        else {
            Color g = Col(40, 255, 120, 1);
            DrawCube(Wv(x, 0.03f, z - 0.7f), 1.5f, 0.03f, 0.08f, g); DrawCube(Wv(x, 0.03f, z + 0.7f), 1.5f, 0.03f, 0.08f, g);
            DrawCube(Wv(x - 0.7f, 0.03f, z), 0.08f, 0.03f, 1.5f, g); DrawCube(Wv(x + 0.7f, 0.03f, z), 0.08f, 0.03f, 1.5f, g);
            for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) DrawCube(Wv(x + 0.7f * sx, 1.3f, z + 0.7f * sz), 0.08f, 2.6f, 0.08f, g);
            DrawCube(Wv(x, 2.7f, z), 0.9f, 0.3f, 0.05f, g);
        }
    }
}

static void DrawLamps(const Player& pl, float flick) {
    const int R = ViewRadiusCells();
    const int rx0 = FloorDiv(CellOf(pl.pos.x) - R, 4);
    const int rx1 = FloorDiv(CellOf(pl.pos.x) + R, 4);
    const int rz0 = FloorDiv(CellOf(pl.pos.z) - R, 4);
    const int rz1 = FloorDiv(CellOf(pl.pos.z) + R, 4);
    rlSetTexture(0);

    // Горизонтальный сектор камеры: невидимые лампы не отправляются в GPU.
    const Vector3 forward = Forward(pl);
    const float horizontalLength = sqrtf(forward.x * forward.x + forward.z * forward.z);
    const bool useFrustum = horizontalLength > 0.05f;
    const float forwardX = useFrustum ? forward.x / horizontalLength : 0.0f;
    const float forwardZ = useFrustum ? forward.z / horizontalLength : 1.0f;
    const float sideX = -forwardZ;
    const float sideZ = forwardX;
    const float aspect = (float)GetScreenWidth() /
                         fmaxf((float)GetScreenHeight(), 1.0f);
    // Консервативный запас соответствует максимальному вертикальному FOV игры.
    const float tanHalfFovX = 0.90f * aspect * 1.15f;
    const float cullMargin = 3.0f;

    auto panel = [&](float cx, float cz, float y,
                     float halfX, float halfZ, Color color) {
        Quad(Wv(cx - halfX, y, cz - halfZ), Wv(cx + halfX, y, cz - halfZ),
             Wv(cx + halfX, y, cz + halfZ), Wv(cx - halfX, y, cz + halfZ),
             1.0f, 1.0f, 1.0f, 1.0f, color, 1.0f, 1.0f);
    };

    for (int rz = rz0; rz <= rz1; ++rz) {
        for (int rx = rx0; rx <= rx1; ++rx) {
            const float lv = LampLevel(rx, rz);
            if (lv <= 0.01f) continue;

            const float cx = (rx * 4 + 2.5f) * CELL;
            const float cz = (rz * 4 + 2.5f) * CELL;
            if (useFrustum) {
                const float dx = cx - pl.pos.x;
                const float dz = cz - pl.pos.z;
                const float forwardDist = dx * forwardX + dz * forwardZ;
                const float sideDist = fabsf(dx * sideX + dz * sideZ);
                if (forwardDist < -cullMargin ||
                    sideDist > fmaxf(0.0f, forwardDist) * tanHalfFovX + cullMargin)
                    continue;
            }

            const float k = fminf(1.0f, lv * flick);
            const float y = WALL_H - 0.084f;
            const Color trim = {
                (unsigned char)(76.0f * lv),
                (unsigned char)(73.0f * lv),
                (unsigned char)(62.0f * lv), 255
            };
            const Color diffuser = {
                (unsigned char)(155.0f * k),
                (unsigned char)(151.0f * k),
                (unsigned char)(128.0f * k), 255
            };
            const Color tube = {
                (unsigned char)(255.0f * k),
                (unsigned char)(249.0f * k),
                (unsigned char)(224.0f * k), 255
            };

            // Вместо 7 объёмных коробок: одна рамка, рассеиватель и две трубки.
            // Слои разнесены по Y, чтобы не бороться за одинаковую глубину.
            panel(cx, cz, y,          0.70f, 0.39f, trim);
            panel(cx, cz, y - 0.010f, 0.59f, 0.285f, diffuser);
            panel(cx, cz - 0.145f, y - 0.020f, 0.51f, 0.025f, tube);
            panel(cx, cz + 0.145f, y - 0.020f, 0.51f, 0.025f, tube);
        }
    }
}

static float Flicker(float t, float insane) {
    // Постоянная яркость: убраны синусоидальные колебания и случайные перебои.
    (void)t;
    (void)insane;
    return 1.0f;
}

static void RenderWorld(const Player& pl, float t, float insane, bool entitiesOn, bool otherEntitiesEnabled) {
    gOrg = {floorf(pl.pos.x / CELL) * CELL, 0, floorf(pl.pos.z / CELL) * CELL};
    Vector3 eye = EyePos(pl), f = Forward(pl);
    Camera3D cam{};
    // Камера не получает случайных координат каждый кадр: это исключает
    // постоянную микродрожь, которая меняла расстояние до фрагментов и туман.
    cam.position = Wv(eye.x, eye.y, eye.z);
    cam.target = {cam.position.x + f.x, cam.position.y + f.y, cam.position.z + f.z};
    cam.up = {0, 1, 0}; cam.fovy = 75.0f; cam.projection = CAMERA_PERSPECTIVE;

    float flick = Flicker(t, insane);
    float flash = (pl.flash && pl.battery > 0) ? ((pl.battery < 15 && Rand01((int)(t * 14), 3, 98) < 0.35f) ? 0.2f : 1.0f) : 0.0f;
    const float viewMeters = (float)ViewRadiusCells() * CELL;
    // Цвет тумана — глобальная константа. Он не зависит от освещённости
    // клетки под игроком, иначе весь мир меняет оттенок при перемещении.
    float fogD = (Level2IsActive() ? 1.28f : 1.15f) / (viewMeters * 1.4142f), unlit = 0.0f;
    const Vector3 fogC = Level2IsActive() ? Vector3{0.105f, 0.135f, 0.16f}
                                          : Vector3{0.25f, 0.225f, 0.12f};
    SetShaderValue(sh, lView, &cam.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, lDir, &f, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, lFogC, &fogC, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, lFlash, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, lFlick, &flick, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, lFogD, &fogD, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, lUnlit, &unlit, SHADER_UNIFORM_FLOAT);

    float farClip = fmaxf(70.0f, viewMeters * 1.7f);
    rlSetClipPlanes(0.08, farClip);                   // дальняя плоскость следует за радиусом прорисовки
    BeginMode3D(cam);
    rlDisableColorBlend(); rlDisableBackfaceCulling();
    BeginShaderMode(sh);                            // проход 1: освещаемая геометрия
    const float aspect = (float)GetScreenWidth() /
                         (float)fmaxf(GetScreenHeight(), 1);
    const float tanHalfFovX =
        tanf(cam.fovy * DEG2RAD * 0.5f) * aspect * 1.15f;
    if (Level2IsActive()) {
        Level2Draw(pl, t, viewMeters);
        DrawLevel2Items(pl,t,false);
    } else {
        DrawLevel(pl, tanHalfFovX); DrawExit(pl, false); DrawItems(pl, t, false);
    }
    if (entitiesOn) EntitiesDraw(pl, false, sh, otherEntitiesEnabled);
    EndShaderMode();
    unlit = 1.0f; SetShaderValue(sh, lUnlit, &unlit, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(sh);                            // проход 2: самосветящиеся (лампы, глаза, маячки)
    if (!Level2IsActive()) {
        DrawLamps(pl, flick); DrawExit(pl, true); DrawItems(pl, t, true);
    } else {
        DrawLevel2Items(pl,t,true);
    }
    if (entitiesOn) EntitiesDraw(pl, true, sh, otherEntitiesEnabled);
    EndShaderMode();
    unlit = 0.0f; SetShaderValue(sh, lUnlit, &unlit, SHADER_UNIFORM_FLOAT);
    if (Level2IsActive()) Level2DrawModels();
    rlEnableBackfaceCulling();
    EndMode3D();
    rlEnableColorBlend();
}

// ---------- HUD ----------
static void Bar(float x, float y, float w, float h, float v, Color c, const char* label, float fs) {
    DrawRectangle((int)x, (int)y, (int)w, (int)h, Fade(BLACK, 0.55f));
    DrawRectangle((int)x, (int)y, (int)(w * Clamp(v, 0, 1)), (int)h, c);
    DrawRectangleLines((int)x, (int)y, (int)w, (int)h, Fade(WHITE, 0.5f));
    Txt(label, x + w + fs * 0.4f, y + (h - fs) / 2, fs, Fade(WHITE, 0.9f));
}

static void DrawHud(const Player& pl, const TouchState& t, int W, int H, bool hasExit, Vector2 exitPos) {
    float fs = H * 0.04f, bw = W * 0.18f, bh = H * 0.032f, x = W * 0.03f, y = H * 0.04f;
    Bar(x, y, bw, bh, pl.health / 100, Col(210, 40, 40, 1), T("HEALTH", "ЗДОРОВЬЕ"), fs);
    Bar(x, y + bh * 1.6f, bw, bh, pl.sanity / 100, Col(80, 170, 255, 1), T("SANITY", "РАССУДОК"), fs);
    Bar(x, y + bh * 3.2f, bw, bh, pl.battery / 100, Col(240, 210, 60, 1), T("BATTERY", "БАТАРЕЯ"), fs);
    Bar(x, y + bh * 4.8f, bw, bh, pl.stamina / 100, Col(120, 220, 120, 1), T("STAMINA", "СИЛЫ"), fs);

    char buf[64];                                   // сигнал M.E.G. - направление и расстояние до люка
    if (Level2IsActive()) {
        Txt(T("LEVEL 2  /  INDUSTRIAL COMPLEX", "УРОВЕНЬ 2  /  ТЕХНИЧЕСКИЙ КОМПЛЕКС"),
            W * 0.5f, H * 0.025f, fs * 0.72f, Fade(Color{155,205,235,255},0.92f), 1);
        Txt(T("OBJECTIVE: FIND THE RECESSED FIRE EXIT", "ЦЕЛЬ: НАЙТИ СКРЫТУЮ АВАРИЙНУЮ ДВЕРЬ"),
            W * 0.5f, H * 0.067f, fs * 0.58f, Fade(WHITE,0.78f), 1);
        if (Level2IsDark(pl.pos.x,pl.pos.z))
            Txt(T("DARK SECTOR  /  MOVEMENT DETECTED", "ТЁМНЫЙ СЕКТОР  /  ОБНАРУЖЕНО ДВИЖЕНИЕ"),
                W * 0.5f, H * 0.105f, fs * 0.56f, Fade(Color{255,105,92,255},0.95f), 1);
    } else if (hasExit) {
        float dx = exitPos.x - pl.pos.x, dz = exitPos.y - pl.pos.z, rel = pl.yaw - atan2f(dx, dz);
        snprintf(buf, sizeof buf, "%s %d m", T("M.E.G. SIGNAL", "СИГНАЛ M.E.G."), (int)(hypotf(dx, dz) / 5) * 5);
        Txt(buf, W / 2.0f, H * 0.03f, fs, Fade(Col(120, 255, 160, 1), 0.9f), 1);
        DrawPoly(Vector2{W / 2.0f, H * 0.03f + fs * 2.4f}, 3, fs * 0.9f, -90.0f + rel * RAD2DEG, Fade(Col(120, 255, 160, 1), 0.8f));
    } else if (!Level2IsActive()) {
        Txt(T("M.E.G. SIGNAL: NONE", "СИГНАЛ M.E.G.: НЕТ"), W / 2.0f, H * 0.03f, fs, Fade(GRAY, 0.8f), 1);
    }

    if (t.joy) {                                    // виртуальный джойстик
        float R = H * 0.15f;
        DrawCircleV(t.joyO, R, Fade(WHITE, 0.10f)); DrawCircleLines((int)t.joyO.x, (int)t.joyO.y, R, Fade(WHITE, 0.4f));
        DrawCircleV(t.joyP, R * 0.4f, Fade(WHITE, 0.35f));
    } else DrawCircleLines((int)(W * 0.14f), (int)(H * 0.72f), H * 0.15f, Fade(WHITE, 0.18f));

    const char* names[6] = {T("RUN", "БЕГ"), T("LIGHT", "СВЕТ"), T("WATER", "ВОДА"), T("MED", "АПТЕЧКА"), T("THROW", "БРОСОК"), (pl.crouching ? T("STAND", "ВСТАТЬ") : T("CROUCH", "ПРИСЕСТЬ"))};
    for (int i = 0; i < 6; i++) {
        float r; Vector2 c = ButtonCenter(i, W, H, &r);
        bool on = (i == 0 && t.sprint) || (i == 1 && pl.flash) || (i == 5 && pl.crouching);
        DrawCircleV(c, r, Fade(on ? Col(255, 230, 120, 1) : WHITE, on ? 0.55f : 0.16f));
        DrawCircleLines((int)c.x, (int)c.y, r, Fade(WHITE, 0.55f));
        Txt(names[i], c.x, c.y - fs * 0.55f, fs * 0.8f, WHITE, 1);
        if (i >= 2 && i <= 4) { int count = i == 2 ? pl.water : i == 3 ? pl.meds : pl.emptyBottles; snprintf(buf, sizeof buf, "x%d", count); Txt(buf, c.x, c.y + fs * 0.25f, fs * 0.8f, Fade(WHITE, 0.9f), 1); }
    }
    if (gMsgT > 0) {
        Font msgFont = gRu ? gFont : GetFontDefault();
        float msgSize = fs;
        float maxWidth = W * 0.94f;
        float textWidth = MeasureTextEx(msgFont, gMsg, msgSize, msgSize / 12.0f).x;

        if (textWidth > maxWidth && textWidth > 0.0f)
            msgSize *= maxWidth / textWidth;

        Txt(gMsg, W / 2.0f, H * 0.86f, msgSize,
            Fade(Col(255, 245, 190, 1), fminf(1.0f, gMsgT)), 1);
    }
}

static void Overlay(int W, int H, float insane, float hurt) {
    float a = Clamp(0.25f + insane * 0.75f, 0, 1);
    DrawTexturePro(gA.vignette, Rectangle{0, 0, 256, 256}, Rectangle{0, 0, (float)W, (float)H}, Vector2{0, 0}, 0, Fade(WHITE, a));
    if (hurt > 0) DrawRectangle(0, 0, W, H, Fade(RED, hurt * 0.35f));
}

static bool HitRect(Rectangle r, Vector2 p) {
    return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
}

static Rectangle CenterRect(int W, int H, float centerY, float widthFactor, float heightFactor) {
    float w = fminf(W * widthFactor, 760.0f), h = fmaxf(46.0f, H * heightFactor);
    return Rectangle{(W - w) * 0.5f, H * centerY - h * 0.5f, w, h};
}

static void DrawUiButton(Rectangle r, const char* label, float fs, bool accent = false) {
    Color fill = accent ? Color{65, 82, 78, 246} : Color{18, 25, 27, 242};
    Color edge = accent ? Color{185, 214, 194, 235} : Color{123, 145, 151, 205};
    Rectangle shadow{r.x+3.0f,r.y+4.0f,r.width,r.height};
    DrawRectangleRounded(shadow,0.14f,12,Fade(BLACK,0.68f));
    DrawRectangleRounded(r,0.14f,12,fill);
    DrawRectangleRoundedLinesEx(r,0.14f,12,2.0f,edge);
    DrawLineEx(Vector2{r.x+10.0f,r.y+7.0f},Vector2{r.x+r.width-10.0f,r.y+7.0f},
               1.0f,Fade(WHITE,accent?0.28f:0.12f));
    DrawRectangleRounded(Rectangle{r.x+5.0f,r.y+8.0f,4.0f,r.height-16.0f},0.45f,5,
                         accent?Color{190,214,178,235}:Color{95,145,155,210});
    Txt(label,r.x+r.width*0.5f,r.y+(r.height-fs)*0.5f,fs,WHITE,1);
}

static Rectangle SliderTrack(int W, int H, float centerY) {
    float w = fminf(W * 0.58f, 620.0f);
    return Rectangle{(W - w) * 0.5f, H * centerY - 5.0f, w, 10.0f};
}

static float SliderFraction(Rectangle r, Vector2 p) {
    return Clamp((p.x - r.x) / r.width, 0.0f, 1.0f);
}

static void DrawSlider(Rectangle r, float value, float fs, const char* label, const char* valueText) {
    Txt(label, r.x, r.y - fs * 2.05f, fs, Fade(WHITE, 0.95f));
    Txt(valueText, r.x + r.width, r.y - fs * 2.05f, fs, Color{255, 224, 140, 255}, 2);
    DrawRectangleRounded(Rectangle{r.x, r.y, r.width, r.height}, 0.5f, 8, Fade(WHITE, 0.20f));
    DrawRectangleRounded(Rectangle{r.x, r.y, r.width * Clamp(value, 0, 1), r.height}, 0.5f, 8, Color{198, 164, 83, 255});
    float kx = r.x + r.width * Clamp(value, 0, 1);
    DrawCircleV(Vector2{kx, r.y + r.height * 0.5f}, r.height * 1.35f, Color{255, 235, 180, 255});
    DrawCircleLines((int)kx, (int)(r.y + r.height * 0.5f), r.height * 1.35f, WHITE);
}

static void DrawSettingsGear(float x, float y, float r) {
    DrawCircleLines((int)x, (int)y, r * 0.58f, WHITE);
    DrawCircleLines((int)x, (int)y, r * 0.22f, WHITE);
    for (int i = 0; i < 8; ++i) {
        float a = i * PI / 4.0f;
        Vector2 p1{x + cosf(a) * r * 0.66f, y + sinf(a) * r * 0.66f};
        Vector2 p2{x + cosf(a) * r, y + sinf(a) * r};
        DrawLineEx(p1, p2, fmaxf(2.0f, r * 0.16f), WHITE);
    }
}

static void DrawMainMenu(int W, int H) {
    float fs = Clamp(H * 0.052f, 17.0f, 30.0f);
    Txt(T("BACKROOMS", "ЗАКУЛИСЬЕ"), W * 0.5f, H * 0.16f, fs * 2.0f, Color{242, 218, 142, 255}, 1);
    Txt(T("AN UNOFFICIAL BACKROOMS EXPERIENCE", "НЕОФИЦИАЛЬНОЕ ПУТЕШЕСТВИЕ ПО ЗАКУЛИСЬЮ"), W * 0.5f, H * 0.27f, fs * 0.62f, Fade(WHITE, 0.72f), 1);
    Txt(T("TWO LOCATIONS  /  ONE WAY OUT", "ДВЕ ЛОКАЦИИ  /  ОДИН ВЫХОД"), W * 0.5f, H * 0.34f, fs * 0.58f, Fade(Color{190,205,195,255},0.82f), 1);
    Rectangle level = CenterRect(W, H, 0.53f, 0.64f, 0.13f);
    Rectangle opts = CenterRect(W, H, 0.70f, 0.64f, 0.13f);
    DrawUiButton(level, T("SELECT LEVEL", "ВЫБРАТЬ УРОВЕНЬ"), fs * 0.95f, true);
    DrawUiButton(opts, T("SETTINGS", "НАСТРОЙКИ"), fs * 0.95f, false);
    DrawSettingsGear(opts.x + opts.width * 0.30f, opts.y + opts.height * 0.5f, fs * 0.55f);
}

static Rectangle Level0Rect(int W, int H) { return CenterRect(W, H, 0.405f, 0.72f, 0.12f); }
static Rectangle Level2Rect(int W, int H) { return CenterRect(W, H, 0.575f, 0.72f, 0.12f); }
static Rectangle LevelBackRect(int W, int H) { return CenterRect(W, H, 0.79f, 0.48f, 0.10f); }

static void DrawLevelSelect(int W, int H) {
    float fs = Clamp(H * 0.047f, 16.0f, 27.0f);
    Rectangle panel{W*0.12f,H*0.09f,W*0.76f,H*0.82f};
    DrawRectangleRounded(panel,0.035f,12,Fade(Color{7,12,15,255},0.94f));
    DrawRectangleRoundedLinesEx(panel,0.035f,12,2.0f,Fade(Color{177,194,195,255},0.68f));
    Txt(T("SELECT LOCATION", "ВЫБОР ЛОКАЦИИ"), W * 0.5f, H * 0.16f, fs * 1.55f, Color{226,218,186,255}, 1);
    Rectangle l0=Level0Rect(W,H), l2=Level2Rect(W,H), back=LevelBackRect(W,H);
    DrawUiButton(l0,T("LEVEL 0  /  THE LOBBY","УРОВЕНЬ 0  /  ЛОББИ"),fs*0.92f,true);
    Txt(T("ENDLESS OFFICE MAZE  -  INTRODUCTORY AREA","БЕСКОНЕЧНЫЙ ОФИСНЫЙ ЛАБИРИНТ  -  НАЧАЛЬНАЯ ЗОНА"),W*.5f,l0.y+l0.height+fs*.14f,fs*.49f,Fade(WHITE,.72f),1);
    DrawUiButton(l2,T("LEVEL 2  /  TECHNICAL COMPLEX","УРОВЕНЬ 2  /  ТЕХНИЧЕСКИЙ КОМПЛЕКС"),fs*.86f,true);
    Txt(T("SERVICE TUNNELS  -  POWER ROOMS  -  UNKNOWN SIGNAL","СЛУЖЕБНЫЕ ТОННЕЛИ  -  ЭЛЕКТРОЩИТОВЫЕ  -  НЕИЗВЕСТНЫЙ СИГНАЛ"),W*.5f,l2.y+l2.height+fs*.14f,fs*.49f,Fade(Color{150,205,230,255},.9f),1);
    DrawUiButton(back,T("BACK TO MENU","В ГЛАВНОЕ МЕНЮ"),fs*.76f,false);
}

static Rectangle EntitiesToggleRect(int W, int H) {
    return Rectangle{W * 0.19f, H * 0.43f, W * 0.62f, H * 0.105f};
}

static void DrawSettings(int W, int H) {
    float fs = Clamp(H * 0.040f, 15.0f, 23.0f);
    Rectangle panel{W * 0.08f, H * 0.045f, W * 0.84f, H * 0.91f};
    DrawRectangleRounded(panel, 0.045f, 12, Fade(Color{9, 13, 13, 255}, 0.94f));
    DrawRectangleRoundedLinesEx(panel, 0.035f, 12, 2.0f, Fade(Color{220, 198, 130, 255}, 0.72f));
    Txt(T("SETTINGS", "НАСТРОЙКИ"), W * 0.5f, H * 0.085f, fs * 1.45f, Color{242, 218, 142, 255}, 1);

    char value[48];
    const char* chunkWord = settings.viewChunks == 1 ? "ЧАНК" : "ЧАНКА";
    snprintf(value, sizeof value, "%d %s", settings.viewChunks, T("CHUNKS", chunkWord));
    Rectangle view = SliderTrack(W, H, 0.285f);
    DrawSlider(view, (settings.viewChunks - VIEW_CHUNKS_MIN) / (float)(VIEW_CHUNKS_MAX - VIEW_CHUNKS_MIN), fs * 0.78f,
               T("RENDER DISTANCE", "ДАЛЬНОСТЬ ПРОРИСОВКИ"), value);

    Rectangle toggle = EntitiesToggleRect(W, H);
    DrawRectangleRounded(toggle, 0.15f, 10, Fade(settings.otherEntities ? Color{43, 75, 52, 255} : Color{79, 48, 43, 255}, 0.95f));
    DrawRectangleRoundedLinesEx(toggle, 0.15f, 10, 2.0f, Fade(WHITE, 0.65f));
    Txt(T("OTHER ENTITIES", "ОСТАЛЬНЫЕ СУЩНОСТИ"), toggle.x + toggle.width * 0.04f, toggle.y + toggle.height * 0.18f, fs * 0.78f, WHITE);
    Txt(settings.otherEntities ? T("ON", "ВКЛЮЧЕНЫ") : T("OFF", "ОТКЛЮЧЕНЫ"), toggle.x + toggle.width * 0.96f, toggle.y + toggle.height * 0.18f, fs * 0.78f,
        settings.otherEntities ? Color{175, 255, 180, 255} : Color{255, 180, 160, 255}, 2);
    Txt(T("FIZICHKA ALWAYS REMAINS ACTIVE", "ФИЗИЧКА ВСЕГДА ОСТАЁТСЯ АКТИВНОЙ"), W * 0.5f, toggle.y + toggle.height * 0.62f, fs * 0.62f, Fade(WHITE, 0.78f), 1);

    int percent = (int)lroundf(settings.sensitivity / 0.0035f * 100.0f);
    snprintf(value, sizeof value, "%d%%", percent);
    Rectangle sens = SliderTrack(W, H, 0.685f);
    DrawSlider(sens, (settings.sensitivity - SENS_MIN) / (SENS_MAX - SENS_MIN), fs * 0.78f,
               T("CAMERA SENSITIVITY", "ЧУВСТВИТЕЛЬНОСТЬ КАМЕРЫ"), value);

    Rectangle back = CenterRect(W, H, 0.865f, 0.70f, 0.105f);
    DrawUiButton(back, T("RETURN TO MAIN MENU", "ВЕРНУТЬСЯ В ГЛАВНОЕ МЕНЮ"), fs * 0.74f, true);
}

static void LoadSettings() {
    if (LoadSettingValue(0) != SETTINGS_MAGIC) return;
    settings.viewChunks = Clamp(LoadSettingValue(1), VIEW_CHUNKS_MIN, VIEW_CHUNKS_MAX);
    settings.otherEntities = LoadSettingValue(2) != 0;
    float sens = LoadSettingValue(3) / 1000000.0f;
    settings.sensitivity = Clamp(sens, SENS_MIN, SENS_MAX);
}

static void SaveSettings() {
    SaveSettingValue(0, SETTINGS_MAGIC);
    SaveSettingValue(1, settings.viewChunks);
    SaveSettingValue(2, settings.otherEntities ? 1 : 0);
    SaveSettingValue(3, (int)lroundf(settings.sensitivity * 1000000.0f));
    settingsDirty = false;
}

static void NewGame(Player& pl) {
    EntitiesSetHoundRage(false);
    Level2SetActive(false);
    Level2Reset();
    MapReset((uint32_t)GetRandomValue(1, 1 << 30));
    PlayerInit(pl); EntitiesReset(); gMsgT = 0;
}

static void NewLevel2(Player& pl) {
    EntitiesSetHoundRage(false);
    Level2Reset();
    Level2SetActive(true);
    PlayerInit(pl);
    pl.pos = {9.0f, 0.0f, 42.0f};
    pl.yaw = PI * 0.5f;
    pl.water = 1;
    EntitiesReset();
    gMsgT = 0;
    Say(T("LEVEL 2. Keep to the lit service route.","УРОВЕНЬ 2. Держитесь освещённого служебного коридора."),5.0f);
}

static const char* RADIO_EN[] = {
    "M.E.G. radio: Level 0. Find a floor hatch - our signal tracks it.",
    "M.E.G.: Hounds hear you run. Walk, and hold eye contact.",
    "B.N.T.G.: Almond Water is currency and medicine. Ration it.",
    "M.E.G.: Smilers hate light. Never stare into the dark.",
    "M.E.G.: That wanderer might not be human. Keep your distance.",
    "B.N.T.G.: Batteries trade 2 for 1 at Trader's Keep. Not here, sadly."};
static const char* RADIO_RU[] = {
    "Радио M.E.G.: Уровень 0. Ищите люк в полу - наш сигнал его пеленгует.",
    "M.E.G.: Гончие слышат бег. Идите шагом и держите зрительный контакт.",
    "B.N.T.G.: Миндальная вода - и валюта, и лекарство. Экономьте.",
    "M.E.G.: Улыбающиеся боятся света. Не смотрите во тьму.",
    "M.E.G.: Тот скиталец может быть не человеком. Держите дистанцию.",
    "B.N.T.G.: Батарейки меняем 2 к 1 в Trader's Keep. Но не здесь."};

void GameRun() {
    gRu = LoadUiFont();
    LoadWallCaption();
    gA = ProcInit();
    Level2Load();
    SetTextureWrap(gA.ceiling, TEXTURE_WRAP_REPEAT);
    gPortrait = LoadTexture("kv31_wall_portrait.jpg");
    if (gPortrait.id == 0) {
        TraceLog(LOG_WARNING, "Portrait texture could not be loaded");
    }
    sh = LoadShaderFromMemory(VS, FS);
    lView = GetShaderLocation(sh, "viewPos"); lDir = GetShaderLocation(sh, "camDir"); lFlash = GetShaderLocation(sh, "flash");
    lFlick = GetShaderLocation(sh, "flicker"); lFogD = GetShaderLocation(sh, "fogDensity"); lFogC = GetShaderLocation(sh, "fogColor");
    lUnlit = GetShaderLocation(sh, "unlit");
    SetTargetFPS(60);

    Player pl; NewGame(pl);
    LoadSettings();
    float time = 0, stateT = 0, radioT = 3, phantomT = 12, scanT = 0;
    int radioIdx = 0; bool hasExit = false; Vector2 exitPos = {0, 0}; const char* endMsg = "";

    while (!WindowShouldClose()) {
        float dt = fminf(GetFrameTime(), 0.05f); time += dt;
        int W = GetScreenWidth(), H = GetScreenHeight();
        TouchState t = ReadTouch(W, H);
        float insane = Clamp(1.0f - pl.sanity / 40.0f, 0, 1);
        if (gMsgT > 0) gMsgT -= dt;

        if (state == ST_PLAY) {
            PlayerUpdate(pl, t, dt, settings.sensitivity);
            MapUpdate(pl.pos.x, pl.pos.z, settings.viewChunks + 1);
            float Lp = LightAt(pl.pos.x, pl.pos.z);

            if (Item* it = Level2IsActive() ? Level2ItemNear(pl.pos.x,pl.pos.z,0.9f)
                                             : ItemNear(pl.pos.x,pl.pos.z,0.9f)) {
                if (it->type == IT_WATER)   { pl.water++; Say(T("Almond Water +1 (restores sanity)", "Миндальная вода +1 (восстанавливает рассудок)")); }
                if (it->type == IT_BATTERY) { pl.battery = fminf(100, pl.battery + 50); Say(T("Battery: flashlight +50%", "Батарейка: фонарик +50%")); }
                if (it->type == IT_MEDKIT)  { pl.meds++; Say(T("Medkit +1 (M.E.G. cache)", "Аптечка +1 (тайник M.E.G.)")); }
                if(Level2IsActive()) Level2TakeItem(it); else TakeItem(it);
                Sfx(SFX_PICK);
            }
            if (t.tDrink && pl.water > 0) {                                 // Миндальная вода: +35 рассудка
                pl.water--; pl.emptyBottles++; pl.sanity = fminf(100, pl.sanity + 35); Sfx(SFX_DRINK, 0.8f);
                Say(T("Almond Water: you feel clearer.", "Миндальная вода: мысли проясняются."));
            }
            if (t.tThrow) {
                const bool doorHandled=Level2IsActive() && Level2Interact(pl);
                if(!doorHandled && pl.emptyBottles>0) EntitiesThrowBottle(pl);
            }
            if (t.tMed && pl.meds > 0 && pl.health < 100) { pl.meds--; pl.health = fminf(100, pl.health + 40); Sfx(SFX_PICK); }

            // рассудок: падает от блуждания и тьмы (фонарик замедляет падение), при 0 начинается безумие
            float rate = -0.10f - fminf(0.25f, pl.lost / 1500.0f);
            if (Lp < 0.25f) rate -= (pl.flash && pl.battery > 0) ? 0.30f : 1.3f;
            pl.lost += dt; pl.sanity = Clamp(pl.sanity + rate * dt, 0, 100);
            if (pl.sanity <= 0) pl.health -= 3.0f * dt;
            pl.hurt = fmaxf(0, pl.hurt - dt * 1.5f);

            EntitiesUpdate(pl, dt, insane, settings.otherEntities);
            if (Level2Update(pl, dt)) {
                // Верхний порог шлюза ведёт обратно на Level 0; встреча начинается сразу за дверью.
                Level2SetActive(false);
                MapReset((uint32_t)GetRandomValue(1, 1 << 30));
                pl.pos = {8.5f, 0.0f, 5.0f};
                pl.yaw = PI * 0.5f;
                pl.pitch = 0.0f;
                pl.flash = false;
                EntitiesReset();
                EntitiesSetHoundRage(true);
                Say(T("LEVEL 0. SHE WAS WAITING ON THE OTHER SIDE.","УРОВЕНЬ 0. ОНА ЖДАЛА ПО ТУ СТОРОНУ ДВЕРИ."),5.0f);
                Haptic(350);
                hasExit = false;
                radioT = 100000.0f;
            }
            if (pl.sanity < 25 && !Level2IsActive()) {                  // фантомные звуки Level 0
                phantomT -= dt;
                if (phantomT <= 0) { phantomT = Frand(8, 18); Sfx(SFX_HOUND, 0.35f, Frand(0, 1)); }
            }

            scanT -= dt;
            if (scanT <= 0) {
                scanT = 0.5f;
                hasExit = !Level2IsActive() && NearestExit(pl.pos.x, pl.pos.z, &exitPos);
            }
            int cx = CellOf(pl.pos.x), cz = CellOf(pl.pos.z);               // люк Level 0
            if (!Level2IsActive() && IsExitCell(cx, cz) && hypotf((cx + 0.5f) * CELL - pl.pos.x, (cz + 0.5f) * CELL - pl.pos.z) < 1.0f) {
                if (pl.exitT == 0) Say(T("The hatch is open... hold still.", "Люк открывается... не двигайтесь."));
                pl.exitT += dt;
                if (pl.exitT > 1.2f) { state = ST_WON; stateT = 0; Haptic(200); }
            } else pl.exitT = 0;

            radioT -= dt;
            if (radioT <= 0) { radioT = Frand(55, 80); Say(T(RADIO_EN[radioIdx % 6], RADIO_RU[radioIdx % 6]), 5.0f); radioIdx++; }

            if (pl.health <= 0) {
                state = ST_DEAD; stateT = 0; Haptic(500);
                endMsg = pl.sanity <= 0 ? T("YOU LOST YOUR MIND", "ВЫ ПОТЕРЯЛИ РАССУДОК") : T("YOU DIED", "ВЫ ПОГИБЛИ");
            }
        } else if (state == ST_MENU || state == ST_LEVEL_SELECT || state == ST_SETTINGS) {
            pl.yaw += dt * 0.035f; // медленный фоновый поворот камеры в меню
            if (state == ST_MENU && t.tapValid) {
                Rectangle level = CenterRect(W, H, 0.53f, 0.64f, 0.13f);
                Rectangle opts = CenterRect(W, H, 0.70f, 0.64f, 0.13f);
                if (HitRect(level, t.tapPos)) state = ST_LEVEL_SELECT;
                else if (HitRect(opts, t.tapPos)) state = ST_SETTINGS;
            } else if (state == ST_LEVEL_SELECT && t.tapValid) {
                if (HitRect(Level0Rect(W,H), t.tapPos)) {
                    NewGame(pl); radioT = 3; radioIdx = 0; state = ST_PLAY;
                } else if (HitRect(Level2Rect(W,H), t.tapPos)) {
                    NewLevel2(pl); radioT = 100000.0f; radioIdx = 0; state = ST_PLAY;
                } else if (HitRect(LevelBackRect(W,H), t.tapPos)) state = ST_MENU;
            } else if (state == ST_SETTINGS) {
                Rectangle toggle = EntitiesToggleRect(W, H);
                Rectangle back = CenterRect(W, H, 0.865f, 0.70f, 0.105f);
                Rectangle view = SliderTrack(W, H, 0.285f);
                Rectangle sens = SliderTrack(W, H, 0.685f);
                if (t.tapValid && HitRect(toggle, t.tapPos)) {
                    settings.otherEntities = !settings.otherEntities;
                    settingsDirty = true;
                } else if (t.tapValid && HitRect(back, t.tapPos)) {
                    if (settingsDirty) SaveSettings();
                    state = ST_MENU;
                }
                if (t.pointerDown) {
                    Rectangle viewHit{view.x - 12, view.y - 28, view.width + 24, 66};
                    Rectangle sensHit{sens.x - 12, sens.y - 28, sens.width + 24, 66};
                    if (HitRect(viewHit, t.pointerPos)) {
                        int v = VIEW_CHUNKS_MIN + (int)lroundf(SliderFraction(view, t.pointerPos) * (VIEW_CHUNKS_MAX - VIEW_CHUNKS_MIN));
                        if (v != settings.viewChunks) { settings.viewChunks = v; settingsDirty = true; }
                    } else if (HitRect(sensHit, t.pointerPos)) {
                        float v = SENS_MIN + SliderFraction(sens, t.pointerPos) * (SENS_MAX - SENS_MIN);
                        if (fabsf(v - settings.sensitivity) > 0.000015f) { settings.sensitivity = v; settingsDirty = true; }
                    }
                } else if (settingsDirty) SaveSettings();
            }
        } else {                                                            // DEAD / WON
            stateT += dt;
            if (stateT > 1.5f && t.anyNew) { state = ST_MENU; }
        }

        float lampVol = state == ST_PLAY ? 0.30f + 0.25f * insane : 0.25f;
        ProcMusicUpdate(lampVol, 1.0f - 0.25f * insane);

        BeginDrawing();
        ClearBackground(BLACK);
        RenderWorld(pl, time, insane, state == ST_PLAY, settings.otherEntities);
        Overlay(W, H, state == ST_PLAY ? insane : 0.0f, state == ST_PLAY ? pl.hurt : 0.0f);
        float fs = H * 0.045f;
        if (state == ST_PLAY) DrawHud(pl, t, W, H, hasExit, exitPos);
        else if (state == ST_MENU || state == ST_LEVEL_SELECT || state == ST_SETTINGS) {
            DrawRectangle(0, 0, W, H, Fade(BLACK, 0.72f));
            if (state == ST_MENU) DrawMainMenu(W, H);
            else if (state == ST_LEVEL_SELECT) DrawLevelSelect(W, H);
            else DrawSettings(W, H);
        } else {
            DrawRectangle(0, 0, W, H, Fade(BLACK, 0.78f));
            if (state == ST_DEAD) {
                Txt(endMsg, W / 2.0f, H * 0.38f, fs * 2.0f, Col(220, 40, 40, 1), 1);
                if (stateT > 1.5f) Txt(T("Tap to return", "Коснитесь, чтобы вернуться"), W / 2.0f, H * 0.62f, fs, Fade(WHITE, 0.8f), 1);
            } else {
                Txt(T("YOU ESCAPED TO LEVEL 1", "ВЫ СБЕЖАЛИ НА УРОВЕНЬ 1"), W / 2.0f, H * 0.36f, fs * 2.0f, Col(120, 255, 160, 1), 1);
                Txt(T("Habitable Zone - M.E.G. outpost ahead.", "Обитаемая зона - впереди аванпост M.E.G."), W / 2.0f, H * 0.52f, fs, Fade(WHITE, 0.85f), 1);
                if (stateT > 1.5f) Txt(T("Tap to return", "Коснитесь, чтобы вернуться"), W / 2.0f, H * 0.66f, fs, Fade(WHITE, 0.8f), 1);
            }
        }
        EndDrawing();
    }
    if (settingsDirty) SaveSettings();
    EntitiesUnload();
    if (gPortrait.id > 0) UnloadTexture(gPortrait);
    if (gWallCaption.id > 0) UnloadTexture(gWallCaption);
    UnloadShader(sh);
    ProcFree(gA);
    Level2Unload();
    if (gRu) UnloadFont(gFont);
}
