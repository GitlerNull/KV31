// Заглушки raylib/игры для офлайн-проверки Level 2 (не входит в сборку Android).
#include "raylib.h"
#include "rlgl.h"
#include "util.h"
#include <map>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdio>
Vector3 gOrg = {0, 0, 0};
bool gRu = false;
void Say(const char* m, float) { std::printf("  [Say] %s\n", m); }
void Haptic(int) {}
struct Img { int w = 0, h = 0; std::vector<unsigned char> px; };
static std::map<unsigned, Img> gTextures;
static unsigned gNext = 1;
static bool LoadRaw(const char* path, Img& out) {
    const char* dir = std::getenv("LEVEL2_RAW_DIR");
    if (!dir) return false;
    std::string p(path); p = p.substr(p.find_last_of('/') + 1);
    p = std::string(dir) + "/" + p + ".rgba";
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    int wh[2]; if (std::fread(wh, 4, 2, f) != 2) { std::fclose(f); return false; }
    out.w = wh[0]; out.h = wh[1]; out.px.resize((size_t)wh[0] * wh[1] * 4);
    size_t n = std::fread(out.px.data(), 1, out.px.size(), f); std::fclose(f);
    return n == out.px.size();
}
Texture2D LoadTexture(const char* path) {
    Img im; LoadRaw(path, im);
    unsigned id = gNext++; gTextures[id] = im;
    return Texture2D{id, im.w, im.h, 1, 7};
}
void UnloadTexture(Texture2D) {}
Image LoadImage(const char* path) {
    Img im;
    if (!LoadRaw(path, im)) return Image{nullptr, 0, 0, 1, 0};
    void* d = std::malloc(im.px.size()); std::memcpy(d, im.px.data(), im.px.size());
    return Image{d, im.w, im.h, 1, 7};
}
void UnloadImage(Image i) { std::free(i.data); }
void ImageFormat(Image*, int) {}
void ImageResize(Image* im, int w, int h) {
    unsigned char* src = (unsigned char*)im->data;
    unsigned char* dst = (unsigned char*)std::malloc((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int sx = (int)((x + 0.5f) * im->width / w), sy = (int)((y + 0.5f) * im->height / h);
        std::memcpy(dst + ((size_t)y * w + x) * 4, src + ((size_t)sy * im->width + sx) * 4, 4);
    }
    std::free(im->data); im->data = dst; im->width = w; im->height = h;
}
Texture2D LoadTextureFromImage(Image i) {
    Img im; im.w = i.width; im.h = i.height; im.px.assign((unsigned char*)i.data, (unsigned char*)i.data + (size_t)i.width * i.height * 4);
    unsigned id = gNext++; gTextures[id] = im;
    return Texture2D{id, i.width, i.height, 1, 7};
}
void SetTextureWrap(Texture2D, int) {}
void SetTextureFilter(Texture2D, int) {}
Model LoadModel(const char*) { return Model{1, 1}; }
void UnloadModel(Model) {}
void DrawModelEx(Model, Vector3, Vector3, float, Vector3, Color) {}
void rlSetTexture(unsigned) {}
bool rlCheckRenderBatchLimit(int) { return false; }
void rlBegin(int) {} void rlEnd() {} void rlColor4ub(unsigned char, unsigned char, unsigned char, unsigned char) {}
void rlTexCoord2f(float, float) {} void rlVertex3f(float, float, float) {}
void rlEnableBackfaceCulling() {} void rlDisableBackfaceCulling() {} void rlDrawRenderBatchActive() {}
// Доступ для проверок: выгрузка текстуры в файл .rgba (w,h,пиксели)
void StubDumpTexture(unsigned id, const char* path) {
    auto it = gTextures.find(id); if (it == gTextures.end()) return;
    FILE* f = std::fopen(path, "wb"); int wh[2] = {it->second.w, it->second.h};
    std::fwrite(wh, 4, 2, f); std::fwrite(it->second.px.data(), 1, it->second.px.size(), f); std::fclose(f);
}
