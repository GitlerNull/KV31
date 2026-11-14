// procgen.cpp - текстуры (обои, ковёр, потолок) и звуки генерируются кодом. Файлов в assets/ не требуется.
#include "procgen.h"
#include "util.h"
#include <vector>
#include <cstdlib>
#include <cstring>

// ---------- текстуры ----------
// тайловый value-noise: u,v в [0,1), g - число ячеек решётки
static float VN(float u, float v, int g, uint32_t salt) {
    float x = u * g, y = v * g; int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto h = [&](int a, int b) { return Rand01(PosMod(a, g), PosMod(b, g), salt); };
    float a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
static Color RGB(float r, float g, float b) {
    auto c = [](float v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); };
    return Color{c(r), c(g), c(b), 255};
}
static Texture2D Finish(Image& im) {
    Texture2D t = LoadTextureFromImage(im); UnloadImage(im);
    GenTextureMipmaps(&t);                       // размеры степени двойки -> безопасно для GLES2
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

static Texture2D GenWall() {                     // жёлтые влажные обои с полосами и пятнами
    const int N = 128; Image im = GenImageColor(N, N, WHITE); Color* px = (Color*)im.data;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = (float)x / N, v = (float)y / N;
        float k = ((x % 32) < 12 ? 0.90f : 1.0f) * (0.96f + 0.06f * Rand01(x, y, 1));
        k *= 0.78f + 0.40f * VN(u, v, 4, 5);                           // сырые пятна
        k *= 1.0f - 0.22f * fmaxf(0.0f, v - 0.55f) * VN(u, v, 8, 6);   // подтёки к низу
        Color c = RGB(212 * k, 192 * k, 98 * k);
        if (y >= 118) c = RGB(125 * k, 105 * k, 58 * k);               // плинтус
        else if (y < 5) c = RGB(150 * k, 132 * k, 70 * k);             // карниз
        px[y * N + x] = c;
    }
    return Finish(im);
}
static Texture2D GenCarpet() {                   // влажный ковёр
    const int N = 128; Image im = GenImageColor(N, N, WHITE); Color* px = (Color*)im.data;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = (float)x / N, v = (float)y / N;
        float k = (0.80f + 0.35f * Rand01(x, y, 2)) * (0.75f + 0.40f * VN(u, v, 6, 7));
        px[y * N + x] = RGB(132 * k, 112 * k, 52 * k);
    }
    return Finish(im);
}
static Texture2D GenCeil() {                     // потолочная плитка 32 px
    const int N = 128; Image im = GenImageColor(N, N, WHITE); Color* px = (Color*)im.data;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float k = 0.93f + 0.07f * Rand01(x, y, 3);
        if (Rand01(x, y, 4) < 0.05f) k *= 0.8f;
        bool line = (x % 32) < 2 || (y % 32) < 2;
        px[y * N + x] = line ? RGB(140 * k, 138 * k, 120 * k) : RGB(208 * k, 205 * k, 182 * k);
    }
    return Finish(im);
}

// ---------- звук ----------
static Sound gSfx[SFX_COUNT]; static bool gAudio = false;
static Music gHum; static std::vector<unsigned char> gHumWav;

template <class F> static Sound Synth(float dur, F f) {
    const int sr = 22050, n = (int)(dur * sr);
    short* d = (short*)RL_MALLOC(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float v = f((float)i / sr, dur); v = fmaxf(-1.0f, fminf(1.0f, v));
        d[i] = (short)(v * 30000);
    }
    Wave w = {(unsigned)n, (unsigned)sr, 16, 1, d};
    Sound s = LoadSoundFromWave(w); UnloadWave(w);
    return s;
}
static float Nz(float t) { return Rand01((int)(t * 22050.0f), 77, 9) * 2.0f - 1.0f; }
static const float TAU = 6.2831853f;

static void SynthAll() {
    gSfx[SFX_STEP]   = Synth(0.14f, [](float t, float) { return (sinf(TAU * 90 * t) * 0.8f * expf(-t * 30) + Nz(t) * 0.35f * expf(-t * 50)); });
    gSfx[SFX_HOUND]  = Synth(0.9f,  [](float t, float d) {
        float f = 70 + 25 * sinf(TAU * 7 * t), ph = fmodf(f * t, 1.0f), saw = ph * 2 - 1;
        return (saw * 0.6f + Nz(t) * 0.3f) * sinf(3.14159f * t / d) * 0.9f; });
    gSfx[SFX_SMILER] = Synth(1.3f,  [](float t, float d) {
        float f = 300 + 1100 * (t / d); float trem = 0.6f + 0.4f * sinf(TAU * 14 * t);
        return (sinf(TAU * f * t) * 0.5f + sinf(TAU * 55 * t) * 0.5f) * trem * sinf(3.14159f * t / d); });
    gSfx[SFX_HIT]    = Synth(0.3f,  [](float t, float) { return (Nz(t) * 0.7f + sinf(TAU * 60 * t) * 0.6f) * expf(-t * 14); });
    gSfx[SFX_PICK]   = Synth(0.25f, [](float t, float) { return sinf(TAU * (t < 0.1f ? 660 : 880) * t) * 0.5f * expf(-t * 8); });
    gSfx[SFX_DRINK]  = Synth(0.6f,  [](float t, float d) {
        return sinf(TAU * (320 + 220 * sinf(TAU * 11 * t)) * t) * 0.4f * sinf(3.14159f * t / d); });
    gSfx[SFX_CINDY]  = Synth(1.2f,  [](float t, float) {
        return (sinf(TAU * 880 * t) + 0.6f * sinf(TAU * 1318 * t)) * 0.3f * expf(-t * 3.5f); });

    // гудение люминесцентных ламп: целые герцы и 2 секунды -> бесшовный цикл, упаковано в WAV в памяти
    const int sr = 22050, n = 2 * sr;
    gHumWav.assign(44 + n * 2, 0);
    unsigned char* b = gHumWav.data();
    auto w32 = [&](int o, uint32_t v) { memcpy(b + o, &v, 4); };
    auto w16 = [&](int o, uint16_t v) { memcpy(b + o, &v, 2); };
    memcpy(b, "RIFF", 4); w32(4, 36 + n * 2); memcpy(b + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
    w32(24, sr); w32(28, sr * 2); w16(32, 2); w16(34, 16); memcpy(b + 36, "data", 4); w32(40, n * 2);
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        float v = 0.32f * sinf(TAU * 60 * t) + 0.2f * sinf(TAU * 120 * t) + 0.1f * sinf(TAU * 180 * t) + 0.05f * sinf(TAU * 240 * t) + 0.02f * sinf(TAU * 3000 * t);
        int16_t s = (int16_t)(v * 20000); memcpy(b + 44 + i * 2, &s, 2);
    }
    gHum = LoadMusicStreamFromMemory(".wav", gHumWav.data(), (int)gHumWav.size());
    gHum.looping = true;
    PlayMusicStream(gHum);
}

ProcAssets ProcInit() {
    ProcAssets a{};
    a.wall = GenWall(); a.carpet = GenCarpet(); a.ceiling = GenCeil();
    Image v = GenImageGradientRadial(256, 256, 0.35f, Color{0, 0, 0, 0}, Color{0, 0, 0, 255});
    a.vignette = LoadTextureFromImage(v); UnloadImage(v);
    InitAudioDevice(); gAudio = IsAudioDeviceReady();
    if (gAudio) SynthAll();
    return a;
}

void ProcFree(ProcAssets& a) {
    UnloadTexture(a.wall); UnloadTexture(a.carpet); UnloadTexture(a.ceiling); UnloadTexture(a.vignette);
    if (gAudio) {
        for (int i = 0; i < SFX_COUNT; i++) UnloadSound(gSfx[i]);
        UnloadMusicStream(gHum); CloseAudioDevice(); gAudio = false;
    }
}

void ProcMusicUpdate(float vol, float pitch) {
    if (!gAudio) return;
    SetMusicVolume(gHum, vol); SetMusicPitch(gHum, pitch);
    UpdateMusicStream(gHum);
}

void Sfx(SfxId id, float vol, float pan) {
    if (!gAudio) return;
    SetSoundVolume(gSfx[id], vol); SetSoundPan(gSfx[id], pan);
    PlaySound(gSfx[id]);
}
