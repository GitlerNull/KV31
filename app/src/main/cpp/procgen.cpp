// procgen.cpp - текстуры (обои, ковёр, потолок) и звуки генерируются кодом. Файлов в assets/ не требуется.
#include "procgen.h"
#include "util.h"
#include <vector>
#include <cstdlib>
#include <cstring>

// ---------- текстуры ----------
// Текстуры рассчитываются один раз при запуске. Шаблоны ориентированы на
// реальные интерьеры Level 0: выцветшие узорчатые обои, бежевый петлевой ковёр
// и акустические плиты подвесного потолка. Все шумы периодические, поэтому
// стыки тайлов не должны образовывать заметных полос.
static float Smooth01(float a, float b, float x) {
    float t = (x - a) / (b - a);
    t = fmaxf(0.0f, fminf(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

// Периодический value-noise: края текстуры совпадают при повторении.
static float VN(float u, float v, int g, uint32_t salt) {
    float x = u * g, y = v * g;
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    auto h = [&](int a, int b) { return Rand01(PosMod(a, g), PosMod(b, g), salt); };
    float a = h(ix, iy), b = h(ix + 1, iy);
    float c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    return (a * (1.0f - fx) + b * fx) * (1.0f - fy) +
           (c * (1.0f - fx) + d * fx) * fy;
}

static float FBM(float u, float v, uint32_t salt) {
    return VN(u, v, 4, salt) * 0.52f +
           VN(u, v, 8, salt + 1) * 0.30f +
           VN(u, v, 16, salt + 2) * 0.18f;
}

static float TileRand(int x, int y, int period, uint32_t salt) {
    return Rand01(PosMod(x, period), PosMod(y, period), salt);
}

static Color RGB(float r, float g, float b) {
    auto c = [](float v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); };
    return Color{c(r), c(g), c(b), 255};
}

static Texture2D Finish(Image& im) {
    Texture2D t = LoadTextureFromImage(im);
    UnloadImage(im);
    if (t.id != 0) {
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    }
    return t;
}

static Texture2D GenWall() {
    // 512 px: subdued 1990s wallpaper print, faded yellow-cream paper,
    // narrow roll seams, fine grain and very mild damp discoloration.
    const int N = 512;
    Image im = GenImageColor(N, N, WHITE);
    Color* px = (Color*)im.data;
    const float PI = 3.14159265f;

    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        float u = (float)x / N, v = (float)y / N;
        float cloud = FBM(u, v, 51);
        float damp = VN(u, v, 3, 57) * 0.55f + VN(u, v, 7, 58) * 0.45f;
        float grain = (TileRand(x, y, N, 59) - 0.5f) * 6.0f;

        // Неброский повторяющийся ромбовидно-волнистый рисунок, а не крупные полосы.
        float nx = ((float)(x % 64) + 0.5f) / 32.0f - 1.0f;
        float ny = ((float)(y % 64) + 0.5f) / 32.0f - 1.0f;
        float diamond = fabsf(fabsf(nx) + fabsf(ny) - 1.12f);
        float innerDiamond = fabsf(fabsf(nx) + fabsf(ny) - 0.52f);
        float waveA = fabsf(nx - 0.20f * sinf(ny * PI));
        float waveB = fabsf(nx + 0.20f * sinf(ny * PI));
        float radius = sqrtf(nx * nx + ny * ny);
        float rosetteRadius = 0.27f + 0.065f * cosf(4.0f * atan2f(ny, nx));
        float rosette = 1.0f - Smooth01(0.025f, 0.075f, fabsf(radius - rosetteRadius));
        float center = 1.0f - Smooth01(0.025f, 0.070f, radius);
        float print = fmaxf(1.0f - Smooth01(0.025f, 0.075f, diamond),
                     fmaxf(0.55f * (1.0f - Smooth01(0.025f, 0.065f, innerDiamond)),
                     fmaxf(0.45f * (1.0f - Smooth01(0.025f, 0.060f, fminf(waveA, waveB))),
                           fmaxf(0.72f * rosette, 0.55f * center))));

        // Тонкие вертикальные стыки рулонов обоев примерно через 0,5 м.
        int rollX = x % 128;
        float seam = (rollX < 2) ? 0.94f : ((rollX == 2 || rollX == 127) ? 0.985f : 1.0f);
        float stain = 0.94f + 0.12f * cloud;
        stain *= 1.0f - 0.10f * Smooth01(0.52f, 1.0f, v) * damp;
        float k = stain * seam;

        float r = 205.0f * k + grain;
        float g = 190.0f * k + grain * 0.92f;
        float b = 132.0f * k + grain * 0.72f;
        // Цвет печатного рисунка близок к основному фону — он читается вблизи,
        // но не превращает стены в контрастные декоративные обои.
        float ink = print * (0.20f + 0.06f * (1.0f - cloud));
        r -= 34.0f * ink; g -= 31.0f * ink; b -= 23.0f * ink;
        float highlight = print * 0.035f;
        r += 18.0f * highlight; g += 17.0f * highlight; b += 12.0f * highlight;

        // Плинтус и узкая верхняя кромка продолжаются по всей стене.
        if (y >= 480) { r *= 0.70f; g *= 0.68f; b *= 0.62f; }
        else if (y < 7) { r *= 0.84f; g *= 0.82f; b *= 0.73f; }
        px[y * N + x] = RGB(r, g, b);
    }
    return Finish(im);
}

static Texture2D GenCarpet() {
    // 512 px loop-pile carpet: oatmeal/tan base, tiny woven fibres and soft
    // irregular traffic/damp shading. The image repeats over a 2 m floor tile.
    const int N = 512;
    Image im = GenImageColor(N, N, WHITE);
    Color* px = (Color*)im.data;
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        float u = (float)x / N, v = (float)y / N;
        float broad = FBM(u, v, 71);
        float medium = VN(u, v, 12, 75);
        float fibre = TileRand(x, y, N, 78);
        float micro = TileRand(x / 2, y / 2, N / 2, 79);
        float weave = ((x + 2 * y) % 5 == 0) ? 1.0f : 0.0f;
        float pile = 0.86f + 0.20f * fibre + 0.10f * micro + 0.045f * weave;
        float stain = 0.80f + 0.34f * broad;
        stain *= 0.94f + 0.12f * medium;
        // Tiny individual dark fibres, not block-shaped dirt pixels.
        float fleck = TileRand(x, y, N, 80);
        float dirt = fleck < 0.012f ? 0.84f : 1.0f;
        float k = pile * stain * dirt;
        px[y * N + x] = RGB(151.0f * k, 130.0f * k, 91.0f * k);
    }
    return Finish(im);
}

static Texture2D GenCeil() {
    // Acoustic mineral-fibre tiles, 4x4 panels per texture. At the current
    // world UV scale, each panel is about 0.5 m square.
    const int N = 512;
    const int TILE = 128;
    Image im = GenImageColor(N, N, WHITE);
    Color* px = (Color*)im.data;
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        int lx = x % TILE, ly = y % TILE;
        int tileX = x / TILE, tileY = y / TILE;
        float panelTone = 0.965f + 0.045f * Rand01(tileX, tileY, 83);
        float speckle = (TileRand(x, y, N, 84) - 0.5f) * 12.0f;
        float pit = TileRand(x / 3, y / 3, N / 3, 85);
        float dirt = pit < 0.025f ? -13.0f : 0.0f;
        bool joint = lx < 3 || ly < 3;
        float edge = fminf((float)lx, fminf((float)ly,
                     fminf((float)(TILE - 1 - lx), (float)(TILE - 1 - ly))));
        float edgeShade = 1.0f - 0.035f * (1.0f - Smooth01(0.0f, 12.0f, edge));
        float k = panelTone * edgeShade;
        if (joint) {
            px[y * N + x] = RGB(132.0f + speckle * 0.25f, 129.0f + speckle * 0.25f, 116.0f + speckle * 0.20f);
        } else {
            px[y * N + x] = RGB(207.0f * k + speckle + dirt,
                                203.0f * k + speckle + dirt,
                                185.0f * k + speckle * 0.80f + dirt);
        }
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
