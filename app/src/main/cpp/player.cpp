// player.cpp - камера от первого лица, движение с коллизиями, мультитач.
// В raylib нет GetTouchPointDelta: дельту пальца считаем сами по GetTouchPointId/GetTouchPosition.
#include "player.h"
#include "map_gen.h"
#include "entities.h"
#include <algorithm>

void PlayerInit(Player& p) {
    p = Player{};
    p.pos = {2.5f * CELL, 0, 2.5f * CELL};     // центр стартовой комнаты (клетка 2,2)
    p.health = 100; p.sanity = 100; p.battery = 100; p.stamina = 100;
    p.water = 1; p.meds = 0; p.noise = 0.05f;
}

Vector3 EyePos(const Player& p) {
    float height = p.crouching ? 0.8f : 1.6f;
    float bob = p.crouching ? 0.015f : 0.035f;
    return {p.pos.x, height + sinf(p.bob) * bob, p.pos.z};
}
Vector3 Forward(const Player& p) {
    float cp = cosf(p.pitch);
    return {cp * sinf(p.yaw), sinf(p.pitch), cp * cosf(p.yaw)};
}

Vector2 ButtonCenter(int i, int W, int H, float* radius) {
    float r = i == 5 ? H * 0.055f : H * 0.075f;
    if (radius) *radius = r;
    if (i == 5) return {W * 0.60f, H * 0.82f};
    switch (i) {
        case 0:  return {W - 2.2f * r, H - 2.2f * r};   // бег
        case 1:  return {W - 5.0f * r, H - 1.7f * r};   // фонарик
        case 2:  return {W - 2.2f * r, H - 5.2f * r};   // вода
        case 3:  return {W - 5.0f * r, H - 4.4f * r};   // аптечка
        default: return {W - 5.0f * r, H - 7.0f * r};   // бросок бутылки
    }
}

static int HitButton(Vector2 p, int W, int H) {
    for (int i = 0; i < 6; i++) {
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
            if (b >= 0) { s->role = 3; s->btn = b; t.tFlash |= (b == 1); t.tDrink |= (b == 2); t.tMed |= (b == 3); t.tThrow |= (b == 4); t.crouchToggle |= (b == 5); }
            else if (p.x < W * 0.5f) { s->role = 1; joyOrigin = p; }
            else s->role = 2;
            t.anyNew = true;
            if (!t.tapValid) { t.tapValid = true; t.tapPos = p; }
        } else if (s->role == 2) { t.look.x += p.x - s->last.x; t.look.y += p.y - s->last.y; }
        s->last = p; s->alive = true;
        if (!t.pointerDown) { t.pointerDown = true; t.pointerPos = p; }
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

void PlayerUpdate(Player& p, const TouchState& t, float dt, float sensitivity) {
    if (t.crouchToggle) p.crouching = !p.crouching;
    const float sens = std::clamp(sensitivity, 0.0010f, 0.0080f);
    p.yaw   -= t.look.x * sens;
    p.pitch  = std::clamp(p.pitch - t.look.y * sens, -1.35f, 1.35f);

    Vector3 f = {sinf(p.yaw), 0, cosf(p.yaw)}, r = {-f.z, 0, f.x};   // вперёд / вправо
    float jx = t.jx, jy = t.jy, m = hypotf(jx, jy);
    if (m < 0.12f) { jx = jy = 0; m = 0; } else if (m > 1) { jx /= m; jy /= m; m = 1; }

    bool run = !p.crouching && t.sprint && m > 0.2f && !p.tired && p.stamina > 0;
    float spd = p.crouching ? 1.4f : (run ? 5.2f : 2.8f);
    float playerRadius = p.crouching ? 0.15f : 0.3f;
    float vx = (f.x * -jy + r.x * jx) * spd * dt, vz = (f.z * -jy + r.z * jx) * spd * dt;
    // Стены и Физичка блокируют движение игрока независимо по осям.
    float nextX = p.pos.x + vx;
    if (!CircleHits(nextX, p.pos.z, playerRadius) &&
        !EntitiesCircleHitsPlayer(nextX, p.pos.z, playerRadius)) {
        p.pos.x = nextX;
    }

    float nextZ = p.pos.z + vz;
    if (!CircleHits(p.pos.x, nextZ, playerRadius) &&
        !EntitiesCircleHitsPlayer(p.pos.x, nextZ, playerRadius)) {
        p.pos.z = nextZ;
    }

    if (run) { p.stamina -= 22 * dt; if (p.stamina <= 0) { p.stamina = 0; p.tired = true; } }
    else { p.stamina = std::min(100.0f, p.stamina + 12 * dt); if (p.tired && p.stamina > 25) p.tired = false; }

    float tn = m < 0.05f ? 0.04f : (p.crouching ? 0.035f + 0.15f * m : (run ? 1.0f : 0.1f + 0.45f * m));   // шум для Гончих
    p.noise += (tn - p.noise) * std::min(1.0f, dt * 6);

    float moved = m * spd * dt;
    p.bob += moved * 2.2f; p.stepAcc += moved;
    if (p.stepAcc > (p.crouching ? 1.0f : (run ? 1.8f : 1.3f))) {
        p.stepAcc = 0;
        Sfx(SFX_STEP, p.crouching ? 0.18f : (run ? 0.8f : 0.45f));
    }

    if (t.tFlash && (p.battery > 0)) p.flash = !p.flash;
    if (p.flash) { p.battery -= 1.1f * dt; if (p.battery <= 0) { p.battery = 0; p.flash = false; } }
}
