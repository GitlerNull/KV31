// player.cpp - камера от первого лица, движение с коллизиями, мультитач.
// В raylib нет GetTouchPointDelta: дельту пальца считаем сами по GetTouchPointId/GetTouchPosition.
#include "player.h"
#include "map_gen.h"
#include <algorithm>

static constexpr float SENS = 0.0035f;   // рад/px

void PlayerInit(Player& p) {
    p = Player{};
    p.pos = {2.5f * CELL, 0, 2.5f * CELL};     // центр стартовой комнаты (клетка 2,2)
    p.health = 100; p.sanity = 100; p.battery = 100; p.stamina = 100;
    p.water = 1; p.meds = 0; p.noise = 0.05f;
}

Vector3 EyePos(const Player& p) { return {p.pos.x, 1.6f + sinf(p.bob) * 0.035f, p.pos.z}; }
Vector3 Forward(const Player& p) {
    float cp = cosf(p.pitch);
    return {cp * sinf(p.yaw), sinf(p.pitch), cp * cosf(p.yaw)};
}

Vector2 ButtonCenter(int i, int W, int H, float* radius) {
    float r = H * 0.075f; if (radius) *radius = r;
    switch (i) {
        case 0:  return {W - 2.2f * r, H - 2.2f * r};   // бег
        case 1:  return {W - 5.0f * r, H - 1.7f * r};   // фонарик
        case 2:  return {W - 2.2f * r, H - 5.2f * r};   // вода
        default: return {W - 5.0f * r, H - 4.4f * r};   // аптечка
    }
}

static int HitButton(Vector2 p, int W, int H) {
    for (int i = 0; i < 4; i++) {
        float r; Vector2 c = ButtonCenter(i, W, H, &r);
        if (hypotf(p.x - c.x, p.y - c.y) < r * 1.25f) return i;
    }
    return -1;
}

struct Slot { int id, role, btn; Vector2 last; bool alive; };   // role: 1 джойстик, 2 камера, 3 кнопка
static Slot S[10]; static int nS = 0; static Vector2 joyOrigin;

TouchState ReadTouch(int W, int H) {
    TouchState t{};
    for (int i = 0; i < nS; i++) S[i].alive = false;
    int n = std::min(GetTouchPointCount(), 10);
    for (int i = 0; i < n; i++) {
        int id = GetTouchPointId(i); Vector2 p = GetTouchPosition(i);
        Slot* s = nullptr;
        for (int k = 0; k < nS; k++) if (S[k].id == id) { s = &S[k]; break; }
        if (!s) {                                           // новое касание - определяем роль
            if (nS >= 10) continue;
            s = &S[nS++]; *s = Slot{id, 0, -1, p, false};
            int b = HitButton(p, W, H);
            if (b >= 0) { s->role = 3; s->btn = b; t.tFlash |= (b == 1); t.tDrink |= (b == 2); t.tMed |= (b == 3); }
            else if (p.x < W * 0.5f) { s->role = 1; joyOrigin = p; }
            else s->role = 2;
            t.anyNew = true;
        } else if (s->role == 2) { t.look.x += p.x - s->last.x; t.look.y += p.y - s->last.y; }
        s->last = p; s->alive = true;
        if (s->role == 1) {
            float R = H * 0.15f, dx = p.x - joyOrigin.x, dy = p.y - joyOrigin.y, m = hypotf(dx, dy);
            if (m > R) { dx *= R / m; dy *= R / m; joyOrigin = {p.x - dx, p.y - dy}; }   // плавающий центр
            t.joy = true; t.joyO = joyOrigin; t.joyP = {joyOrigin.x + dx, joyOrigin.y + dy};
            t.jx = dx / R; t.jy = dy / R;
        }
        if (s->role == 3 && s->btn == 0) t.sprint = true;
    }
    int w = 0; for (int k = 0; k < nS; k++) if (S[k].alive) S[w++] = S[k];
    nS = w;
    return t;
}

void PlayerUpdate(Player& p, const TouchState& t, float dt) {
    p.yaw   -= t.look.x * SENS;
    p.pitch  = std::clamp(p.pitch - t.look.y * SENS, -1.35f, 1.35f);

    Vector3 f = {sinf(p.yaw), 0, cosf(p.yaw)}, r = {-f.z, 0, f.x};   // вперёд / вправо
    float jx = t.jx, jy = t.jy, m = hypotf(jx, jy);
    if (m < 0.12f) { jx = jy = 0; m = 0; } else if (m > 1) { jx /= m; jy /= m; m = 1; }

    bool run = t.sprint && m > 0.2f && !p.tired && p.stamina > 0;
    float spd = run ? 5.2f : 2.8f;
    float vx = (f.x * -jy + r.x * jx) * spd * dt, vz = (f.z * -jy + r.z * jx) * spd * dt;
    if (!CircleHits(p.pos.x + vx, p.pos.z, 0.3f)) p.pos.x += vx;      // раздельно по осям - скольжение вдоль стен
    if (!CircleHits(p.pos.x, p.pos.z + vz, 0.3f)) p.pos.z += vz;

    if (run) { p.stamina -= 22 * dt; if (p.stamina <= 0) { p.stamina = 0; p.tired = true; } }
    else { p.stamina = std::min(100.0f, p.stamina + 12 * dt); if (p.tired && p.stamina > 25) p.tired = false; }

    float tn = m < 0.05f ? 0.04f : (run ? 1.0f : 0.1f + 0.45f * m);   // шум для Гончих
    p.noise += (tn - p.noise) * std::min(1.0f, dt * 6);

    float moved = m * spd * dt;
    p.bob += moved * 2.2f; p.stepAcc += moved;
    if (p.stepAcc > (run ? 1.8f : 1.3f)) { p.stepAcc = 0; Sfx(SFX_STEP, run ? 0.8f : 0.45f); }

    if (t.tFlash && (p.battery > 0)) p.flash = !p.flash;
    if (p.flash) { p.battery -= 1.1f * dt; if (p.battery <= 0) { p.battery = 0; p.flash = false; } }
}
