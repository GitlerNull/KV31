// util.h - общие константы, хеш-функции и межмодульные объявления
#pragma once
#include <cstdint>
#include <cmath>
#include "raylib.h"

constexpr float CELL   = 2.0f;   // размер клетки, м
constexpr float WALL_H = 3.0f;   // высота потолка, м
constexpr int   CHUNK  = 16;     // сторона чанка в клетках

inline uint32_t gSeed = 20260101u;   // зерно мира

inline int FloorDiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
inline int PosMod(int a, int b)   { int m = a % b; return m < 0 ? m + b : m; }

// Хеш координат тайла (splitmix64). std::hash<int> на libc++ - тождество, для карты не годится.
inline uint32_t Hash2(int x, int y, uint32_t salt = 0) {
    uint64_t h = ((uint64_t)(uint32_t)x << 32) ^ (uint32_t)y ^ ((uint64_t)(salt + gSeed) * 0x9E3779B97F4A7C15ull);
    h += 0x9E3779B97F4A7C15ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)(h ^ (h >> 31));
}
inline float Rand01(int x, int y, uint32_t salt = 0) { return (Hash2(x, y, salt) & 0xFFFFFF) / 16777216.0f; }

// --- реализованы в других модулях ---
extern Vector3 gOrg;     // плавающее начало координат рендера (игра)
extern bool gRu;         // true, если загружен шрифт с кириллицей
inline const char* T(const char* en, const char* ru) { return gRu ? ru : en; }

void Say(const char* msg, float secs = 3.5f);   // субтитр (game.cpp)
void Haptic(int ms);                            // вибрация через JNI (jni_bridge.cpp)
unsigned char* LoadSystemFontData(int* size);     // системный шрифт Android с поддержкой кириллицы

enum SfxId { SFX_STEP, SFX_HOUND, SFX_SMILER, SFX_HIT, SFX_PICK, SFX_DRINK, SFX_CINDY, SFX_COUNT };
void Sfx(SfxId id, float vol = 1.0f, float pan = 0.5f);   // procgen.cpp
