// game.cpp - главный цикл (while !WindowShouldClose), рендер мира, туман-шейдер, рассудок, HUD.
// Стены/пол/потолок рисуются квадами через rlgl (только видимые грани) с процедурными текстурами.
// В raylib нет rlEnableFog: туман и свет (лампы + фонарик) считаются в шейдере GLSL ES 1.00.
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "game.h"
#include "util.h"
#include "map_gen.h"
#include "player.h"
#include "entities.h"
#include "procgen.h"

Vector3 gOrg = {0, 0, 0};
bool gRu = false;

namespace {
Font gFont;
ProcAssets gA;
Shader sh;
int lView, lDir, lFlash, lFlick, lFogD, lFogC, lUnlit;
char gMsg[128]; float gMsgT = 0;
enum State { ST_MENU, ST_PLAY, ST_DEAD, ST_WON } state = ST_MENU;
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
    vec3 tex = texture2D(texture0, fragUV).rgb * fragColor.rgb;
    float d = length(fragPos - viewPos);
    vec3 dir = (fragPos - viewPos) / max(d, 0.001);
    float cone = smoothstep(0.88, 0.965, dot(dir, camDir));
    float spot = flash * cone * (1.2 / (1.0 + 0.06*d*d));
    vec3 light = vec3(fragColor.a * flicker * 0.95) * vec3(1.0, 0.97, 0.85) + vec3(spot) * vec3(1.0, 0.96, 0.85);
    vec3 c = (unlit > 0.5) ? tex : tex * (light + 0.02);
    float k = d * fogDensity;
    c = mix(c, fogColor, clamp(1.0 - exp(-k*k), 0.0, 1.0));
    gl_FragColor = vec4(c, 1.0);
}
)";

void Say(const char* m, float s) { snprintf(gMsg, sizeof gMsg, "%s", m); gMsgT = s; }
static float Frand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.0f; }
static Color Col(int r, int g, int b, float L) { L = L < 0 ? 0 : L > 1 ? 1 : L; return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)(L * 255)}; }

// шрифт с кириллицей берём из assets/font.ttf, если файл положили; иначе интерфейс на английском
static bool LoadUiFont() {
    int n = 0; unsigned char* d = LoadFileData("font.ttf", &n);
    if (!d || n <= 0) return false;
    int cps[200], c = 0;
    for (int i = 32; i < 127; i++) cps[c++] = i;
    for (int i = 0x410; i <= 0x44F; i++) cps[c++] = i;
    cps[c++] = 0x401; cps[c++] = 0x451;
    gFont = LoadFontFromMemory(".ttf", d, n, 40, cps, c);
    UnloadFileData(d);
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

static const int VIEW_R = 14;   // радиус прорисовки в клетках

static void DrawLevel(const Player& pl) {
    int pcx = CellOf(pl.pos.x), pcz = CellOf(pl.pos.z), R = VIEW_R;
    Color white = {255, 255, 255, 255};
    int bx0 = FloorDiv(pcx - R, 3), bx1 = FloorDiv(pcx + R, 3), bz0 = FloorDiv(pcz - R, 3), bz1 = FloorDiv(pcz + R, 3);
    const float B = 3 * CELL;
    for (int pass = 0; pass < 2; pass++) {          // 0 - ковёр, 1 - потолок
        rlSetTexture(pass == 0 ? gA.carpet.id : gA.ceiling.id);
        float y = pass == 0 ? 0.0f : WALL_H, k = pass == 0 ? 0.9f : 0.8f;
        for (int bz = bz0; bz <= bz1; bz++) for (int bx = bx0; bx <= bx1; bx++) {
            float x0 = bx * B, x1 = x0 + B, z0 = bz * B, z1 = z0 + B;
            Quad(Wv(x0, y, z0), Wv(x1, y, z0), Wv(x1, y, z1), Wv(x0, y, z1),
                 LightAt(x0, z0) * k, LightAt(x1, z0) * k, LightAt(x1, z1) * k, LightAt(x0, z1) * k, white, 3, 3);
        }
    }
    rlSetTexture(gA.wall.id);
    auto face = [&](float xa, float za, float xb, float zb, float shade) {
        float la = LightAt(xa, za) * shade, lb = LightAt(xb, zb) * shade;
        Quad(Wv(xa, 0, za), Wv(xb, 0, zb), Wv(xb, WALL_H, zb), Wv(xa, WALL_H, za), la, lb, lb * 0.9f, la * 0.9f, white, 1, 1);
    };
    for (int cz = pcz - R; cz <= pcz + R; cz++) for (int cx = pcx - R; cx <= pcx + R; cx++) {
        if (!IsSolid(cx, cz)) continue;
        float x0 = cx * CELL, x1 = x0 + CELL, z0 = cz * CELL, z1 = z0 + CELL;
        if (!IsSolid(cx, cz - 1)) face(x0, z0, x1, z0, 1.0f);
        if (!IsSolid(cx, cz + 1)) face(x0, z1, x1, z1, 1.0f);
        if (!IsSolid(cx - 1, cz)) face(x0, z0, x0, z1, 0.85f);
        if (!IsSolid(cx + 1, cz)) face(x1, z0, x1, z1, 0.85f);
    }
    rlSetTexture(0);
}

static void DrawItems(const Player& pl, float t, bool glow) {
    int pcx = FloorDiv(CellOf(pl.pos.x), CHUNK), pcz = FloorDiv(CellOf(pl.pos.z), CHUNK);
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++)
        for (Item& it : GetChunk(pcx + i, pcz + j).items) {
            if (it.taken) continue;
            float x = (it.cx + 0.5f) * CELL, z = (it.cz + 0.5f) * CELL;
            if (hypotf(x - pl.pos.x, z - pl.pos.z) > 26) continue;
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

static void DrawExit(const Player& pl, bool glow) {
    int pcx = FloorDiv(CellOf(pl.pos.x), CHUNK), pcz = FloorDiv(CellOf(pl.pos.z), CHUNK);
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        Chunk& c = GetChunk(pcx + i, pcz + j);
        if (!c.hasExit) continue;
        float x = (c.exitX + 0.5f) * CELL, z = (c.exitZ + 0.5f) * CELL;
        if (hypotf(x - pl.pos.x, z - pl.pos.z) > 28) continue;
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
    int R = VIEW_R, rx0 = FloorDiv(CellOf(pl.pos.x) - R, 4), rx1 = FloorDiv(CellOf(pl.pos.x) + R, 4);
    int rz0 = FloorDiv(CellOf(pl.pos.z) - R, 4), rz1 = FloorDiv(CellOf(pl.pos.z) + R, 4);
    rlSetTexture(0);
    for (int rz = rz0; rz <= rz1; rz++) for (int rx = rx0; rx <= rx1; rx++) {
        float lv = LampLevel(rx, rz);
        if (lv <= 0.01f) continue;
        float cx = (rx * 4 + 2.5f) * CELL, cz = (rz * 4 + 2.5f) * CELL, h = 0.55f, k = fminf(1.0f, lv * flick);
        Color c = {(unsigned char)(255 * k), (unsigned char)(250 * k), (unsigned char)(200 * k), 255};
        float y = WALL_H - 0.02f;
        Quad(Wv(cx - h, y, cz - h), Wv(cx + h, y, cz - h), Wv(cx + h, y, cz + h), Wv(cx - h, y, cz + h), 1, 1, 1, 1, c, 1, 1);
    }
}

static float Flicker(float t, float insane) {
    float f = 0.94f + 0.06f * sinf(t * 90.0f);
    if (Rand01((int)(t * 11.0f), 5, 99) < 0.04f + 0.10f * insane) f *= 0.35f;   // перебои ламп
    return f;
}

static void RenderWorld(const Player& pl, float t, float insane, bool entitiesOn) {
    gOrg = {floorf(pl.pos.x / CELL) * CELL, 0, floorf(pl.pos.z / CELL) * CELL};
    Vector3 eye = EyePos(pl), f = Forward(pl);
    float shake = insane * 0.03f + pl.hurt * 0.05f;
    Camera3D cam{};
    cam.position = Wv(eye.x + shake * GetRandomValue(-100, 100) / 100.0f, eye.y + shake * GetRandomValue(-100, 100) / 100.0f, eye.z);
    cam.target = {cam.position.x + f.x, cam.position.y + f.y, cam.position.z + f.z};
    cam.up = {0, 1, 0}; cam.fovy = 75.0f + insane * 8.0f * sinf(t * 1.3f); cam.projection = CAMERA_PERSPECTIVE;

    float Lp = LightAt(pl.pos.x, pl.pos.z), flick = Flicker(t, insane);
    float flash = (pl.flash && pl.battery > 0) ? ((pl.battery < 15 && Rand01((int)(t * 14), 3, 98) < 0.35f) ? 0.2f : 1.0f) : 0.0f;
    float fogD = 0.062f, unlit = 0.0f;
    Vector3 fogC = {0.30f * (0.1f + 0.9f * Lp), 0.27f * (0.1f + 0.9f * Lp), 0.12f * (0.1f + 0.9f * Lp)};
    SetShaderValue(sh, lView, &cam.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, lDir, &f, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, lFogC, &fogC, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, lFlash, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, lFlick, &flick, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, lFogD, &fogD, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, lUnlit, &unlit, SHADER_UNIFORM_FLOAT);

    rlSetClipPlanes(0.08, 70.0);                    // узкий диапазон - нет z-fighting на 16-бит глубине
    BeginMode3D(cam);
    rlDisableColorBlend(); rlDisableBackfaceCulling();
    BeginShaderMode(sh);                            // проход 1: освещаемая геометрия
    DrawLevel(pl); DrawExit(pl, false); DrawItems(pl, t, false);
    if (entitiesOn) EntitiesDraw(pl, false);
    EndShaderMode();
    unlit = 1.0f; SetShaderValue(sh, lUnlit, &unlit, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(sh);                            // проход 2: самосветящиеся (лампы, глаза, маячки)
    DrawLamps(pl, flick); DrawExit(pl, true); DrawItems(pl, t, true);
    if (entitiesOn) EntitiesDraw(pl, true);
    EndShaderMode();
    unlit = 0.0f; SetShaderValue(sh, lUnlit, &unlit, SHADER_UNIFORM_FLOAT);
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
    if (hasExit) {
        float dx = exitPos.x - pl.pos.x, dz = exitPos.y - pl.pos.z, rel = pl.yaw - atan2f(dx, dz);
        snprintf(buf, sizeof buf, "%s %d m", T("M.E.G. SIGNAL", "СИГНАЛ M.E.G."), (int)(hypotf(dx, dz) / 5) * 5);
        Txt(buf, W / 2.0f, H * 0.03f, fs, Fade(Col(120, 255, 160, 1), 0.9f), 1);
        DrawPoly(Vector2{W / 2.0f, H * 0.03f + fs * 2.4f}, 3, fs * 0.9f, -90.0f + rel * RAD2DEG, Fade(Col(120, 255, 160, 1), 0.8f));
    } else Txt(T("M.E.G. SIGNAL: NONE", "СИГНАЛ M.E.G.: НЕТ"), W / 2.0f, H * 0.03f, fs, Fade(GRAY, 0.8f), 1);

    if (t.joy) {                                    // виртуальный джойстик
        float R = H * 0.15f;
        DrawCircleV(t.joyO, R, Fade(WHITE, 0.10f)); DrawCircleLines((int)t.joyO.x, (int)t.joyO.y, R, Fade(WHITE, 0.4f));
        DrawCircleV(t.joyP, R * 0.4f, Fade(WHITE, 0.35f));
    } else DrawCircleLines((int)(W * 0.14f), (int)(H * 0.72f), H * 0.15f, Fade(WHITE, 0.18f));

    const char* names[4] = {T("RUN", "БЕГ"), T("LIGHT", "СВЕТ"), T("WATER", "ВОДА"), T("MED", "МЕД")};
    for (int i = 0; i < 4; i++) {
        float r; Vector2 c = ButtonCenter(i, W, H, &r);
        bool on = (i == 0 && t.sprint) || (i == 1 && pl.flash);
        DrawCircleV(c, r, Fade(on ? Col(255, 230, 120, 1) : WHITE, on ? 0.55f : 0.16f));
        DrawCircleLines((int)c.x, (int)c.y, r, Fade(WHITE, 0.55f));
        Txt(names[i], c.x, c.y - fs * 0.55f, fs * 0.8f, WHITE, 1);
        if (i >= 2) { snprintf(buf, sizeof buf, "x%d", i == 2 ? pl.water : pl.meds); Txt(buf, c.x, c.y + fs * 0.25f, fs * 0.8f, Fade(WHITE, 0.9f), 1); }
    }
    if (gMsgT > 0) Txt(gMsg, W / 2.0f, H * 0.86f, fs, Fade(Col(255, 245, 190, 1), fminf(1.0f, gMsgT)), 1);
}

static void Overlay(int W, int H, float insane, float hurt) {
    float a = Clamp(0.25f + insane * 0.75f, 0, 1);
    DrawTexturePro(gA.vignette, Rectangle{0, 0, 256, 256}, Rectangle{0, 0, (float)W, (float)H}, Vector2{0, 0}, 0, Fade(WHITE, a));
    if (hurt > 0) DrawRectangle(0, 0, W, H, Fade(RED, hurt * 0.35f));
}

static void NewGame(Player& pl) {
    MapReset((uint32_t)GetRandomValue(1, 1 << 30));
    PlayerInit(pl); EntitiesReset(); gMsgT = 0;
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
    gA = ProcInit();
    sh = LoadShaderFromMemory(VS, FS);
    lView = GetShaderLocation(sh, "viewPos"); lDir = GetShaderLocation(sh, "camDir"); lFlash = GetShaderLocation(sh, "flash");
    lFlick = GetShaderLocation(sh, "flicker"); lFogD = GetShaderLocation(sh, "fogDensity"); lFogC = GetShaderLocation(sh, "fogColor");
    lUnlit = GetShaderLocation(sh, "unlit");
    SetTargetFPS(60);

    Player pl; NewGame(pl);
    float time = 0, stateT = 0, radioT = 3, phantomT = 12, scanT = 0;
    int radioIdx = 0; bool hasExit = false; Vector2 exitPos = {0, 0}; const char* endMsg = "";

    while (!WindowShouldClose()) {
        float dt = fminf(GetFrameTime(), 0.05f); time += dt;
        int W = GetScreenWidth(), H = GetScreenHeight();
        TouchState t = ReadTouch(W, H);
        float insane = Clamp(1.0f - pl.sanity / 40.0f, 0, 1);
        if (gMsgT > 0) gMsgT -= dt;

        if (state == ST_PLAY) {
            PlayerUpdate(pl, t, dt);
            MapUpdate(pl.pos.x, pl.pos.z);
            float Lp = LightAt(pl.pos.x, pl.pos.z);

            if (Item* it = ItemNear(pl.pos.x, pl.pos.z, 0.9f)) {          // подбор предметов
                if (it->type == IT_WATER)   { pl.water++; Say(T("Almond Water +1 (restores sanity)", "Миндальная вода +1 (восстанавливает рассудок)")); }
                if (it->type == IT_BATTERY) { pl.battery = fminf(100, pl.battery + 50); Say(T("Battery: flashlight +50%", "Батарейка: фонарик +50%")); }
                if (it->type == IT_MEDKIT)  { pl.meds++; Say(T("Medkit +1 (M.E.G. cache)", "Аптечка +1 (тайник M.E.G.)")); }
                TakeItem(it); Sfx(SFX_PICK);
            }
            if (t.tDrink && pl.water > 0) {                                 // Миндальная вода: +35 рассудка
                pl.water--; pl.sanity = fminf(100, pl.sanity + 35); Sfx(SFX_DRINK, 0.8f);
                Say(T("Almond Water: you feel clearer.", "Миндальная вода: мысли проясняются."));
            }
            if (t.tMed && pl.meds > 0 && pl.health < 100) { pl.meds--; pl.health = fminf(100, pl.health + 40); Sfx(SFX_PICK); }

            // рассудок: падает от блуждания и тьмы (фонарик замедляет падение), при 0 начинается безумие
            float rate = -0.10f - fminf(0.25f, pl.lost / 1500.0f);
            if (Lp < 0.25f) rate -= (pl.flash && pl.battery > 0) ? 0.30f : 1.3f;
            pl.lost += dt; pl.sanity = Clamp(pl.sanity + rate * dt, 0, 100);
            if (pl.sanity <= 0) pl.health -= 3.0f * dt;
            pl.hurt = fmaxf(0, pl.hurt - dt * 1.5f);

            EntitiesUpdate(pl, dt, insane);
            if (pl.sanity < 25) {                                           // галлюцинации: далёкие звуки
                phantomT -= dt;
                if (phantomT <= 0) { phantomT = Frand(8, 18); Sfx(SFX_HOUND, 0.35f, Frand(0, 1)); }
            }

            scanT -= dt;
            if (scanT <= 0) { scanT = 0.5f; hasExit = NearestExit(pl.pos.x, pl.pos.z, &exitPos); }
            int cx = CellOf(pl.pos.x), cz = CellOf(pl.pos.z);               // люк: постоять на нём ~1 с
            if (IsExitCell(cx, cz) && hypotf((cx + 0.5f) * CELL - pl.pos.x, (cz + 0.5f) * CELL - pl.pos.z) < 1.0f) {
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
        } else if (state == ST_MENU) {
            pl.yaw += dt * 0.15f;
            if (t.anyNew) { NewGame(pl); radioT = 3; radioIdx = 0; state = ST_PLAY; }
        } else {                                                            // DEAD / WON
            stateT += dt;
            if (stateT > 1.5f && t.anyNew) { NewGame(pl); state = ST_MENU; }
        }

        float lampVol = state == ST_PLAY ? 0.30f + 0.25f * insane : 0.25f;
        ProcMusicUpdate(lampVol, 1.0f - 0.25f * insane);

        BeginDrawing();
        ClearBackground(BLACK);
        RenderWorld(pl, time, insane, state != ST_MENU);
        Overlay(W, H, insane, pl.hurt);
        float fs = H * 0.045f;
        if (state == ST_PLAY) DrawHud(pl, t, W, H, hasExit, exitPos);
        else {
            DrawRectangle(0, 0, W, H, Fade(BLACK, state == ST_MENU ? 0.62f : 0.78f));
            if (state == ST_MENU) {
                Txt(T("BACKROOMS", "ЗАКУЛИСЬЕ"), W / 2.0f, H * 0.10f, fs * 2.2f, Col(235, 215, 90, 1), 1);
                Txt(T("Level 0 - The Lobby", "Уровень 0 - Лобби"), W / 2.0f, H * 0.28f, fs, Fade(WHITE, 0.8f), 1);
                const char* L[5] = {
                    T("Find a floor hatch (green light) to reach Level 1.", "Найдите люк в полу (зелёный свет), чтобы уйти на Уровень 1."),
                    T("Left: move. Right: look. Buttons: RUN, LIGHT, WATER, MED.", "Слева - движение, справа - обзор. Кнопки: БЕГ, СВЕТ, ВОДА, МЕД."),
                    T("Hounds hear you run. Walk and hold eye contact.", "Гончие слышат бег. Идите шагом и держите зрительный контакт."),
                    T("Smilers hate light. Do not stare into the dark.", "Улыбающиеся боятся света. Не смотрите во тьму."),
                    T("Almond Water restores sanity. Ration it.", "Миндальная вода восстанавливает рассудок. Экономьте.")};
                for (int i = 0; i < 5; i++) Txt(L[i], W / 2.0f, H * (0.38f + 0.08f * i), fs * 0.8f, Fade(WHITE, 0.85f), 1);
                Txt(T("TAP TO START", "КОСНИТЕСЬ, ЧТОБЫ НАЧАТЬ"), W / 2.0f, H * 0.84f, fs * 1.1f, Col(255, 240, 120, 1), 1);
                if (!gRu) Txt("(Cyrillic UI: put a TTF as assets/font.ttf)", W / 2.0f, H * 0.93f, fs * 0.6f, Fade(GRAY, 0.8f), 1);
            } else if (state == ST_DEAD) {
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
    UnloadShader(sh);
    ProcFree(gA);
    if (gRu) UnloadFont(gFont);
}
