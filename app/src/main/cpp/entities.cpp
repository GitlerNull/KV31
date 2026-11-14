// entities.cpp - ИИ сущностей. Лор: Гончая (Entity 8) слышит шум; Улыбающийся (Entity 3) боится света и
// нападает, если долго смотреть в тьму на него; Кожекрад (Entity 10) выдаёт себя за человека; Синди - добрая.
#include "entities.h"
#include "map_gen.h"
#include "rlgl.h"
#include <vector>
#include <cstring>
#include <algorithm>

static std::vector<Entity> gE;
static float gSpawnT = 25.0f;

static float Frand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.0f; }
static float Dist(Vector2 a, Vector2 b) { return hypotf(a.x - b.x, a.y - b.y); }
static Vector2 CellCenter(int cx, int cz) { return {(cx + 0.5f) * CELL, (cz + 0.5f) * CELL}; }

static Vector2 RandomOpen(Vector2 from, int rad) {
    for (int i = 0; i < 12; i++) {
        int cx = CellOf(from.x) + GetRandomValue(-rad, rad), cz = CellOf(from.y) + GetRandomValue(-rad, rad);
        if (!IsSolid(cx, cz)) return CellCenter(cx, cz);
    }
    return from;
}

// BFS по клеткам в окне +-R: центр следующей клетки на пути к цели (или к ближайшей достижимой клетке)
static bool NextStep(Vector2 from, Vector2 to, Vector2* out) {
    constexpr int R = 16, W = 2 * R + 1;
    static int16_t prev[W * W], q[W * W]; static uint8_t seen[W * W];
    int sx = CellOf(from.x), sz = CellOf(from.y), tx = CellOf(to.x), tz = CellOf(to.y);
    auto idx = [&](int x, int z) { return (z - sz + R) * W + (x - sx + R); };
    memset(seen, 0, sizeof seen);
    int start = idx(sx, sz), head = 0, tail = 0, best = start; float bestD = 1e9f;
    q[tail++] = (int16_t)start; seen[start] = 1; prev[start] = -1;
    static const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    while (head < tail) {
        int cur = q[head++], cx = sx - R + cur % W, cz = sz - R + cur / W;
        float d = (float)((cx - tx) * (cx - tx) + (cz - tz) * (cz - tz));
        if (d < bestD) { bestD = d; best = cur; if (d == 0) break; }
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], nz = cz + dz[k];
            if (abs(nx - sx) > R || abs(nz - sz) > R) continue;
            int ni = idx(nx, nz);
            if (seen[ni] || IsSolid(nx, nz)) continue;
            seen[ni] = 1; prev[ni] = (int16_t)cur; q[tail++] = (int16_t)ni;
        }
    }
    if (best == start) return false;
    int cur = best;
    while (prev[cur] != start) cur = prev[cur];
    *out = CellCenter(sx - R + cur % W, sz - R + cur / W);
    return true;
}

static bool Move(Entity& e, Vector2 goal, float speed, float dt) {
    Vector2 d = {goal.x - e.pos.x, goal.y - e.pos.y}; float m = hypotf(d.x, d.y);
    if (m < 0.05f) return true;
    d.x /= m; d.y /= m; e.yaw = atan2f(d.x, d.y);
    float s = fminf(speed * dt, m), nx = e.pos.x + d.x * s, nz = e.pos.y + d.y * s;
    if (!CircleHits(nx, e.pos.y, 0.35f)) e.pos.x = nx;
    if (!CircleHits(e.pos.x, nz, 0.35f)) e.pos.y = nz;
    return false;
}
static void Follow(Entity& e, Vector2 target, float speed, float dt) {
    e.pathT -= dt;
    if (e.pathT <= 0) { e.pathT = 0.3f; Vector2 w; e.wp = NextStep(e.pos, target, &w) ? w : target; }
    bool direct = Dist(e.pos, target) < 3.0f && LineClear(e.pos, target);
    Move(e, direct ? target : e.wp, speed, dt);
}
static void Face(Entity& e, Vector2 p) { e.yaw = atan2f(p.x - e.pos.x, p.y - e.pos.y); }

static bool LookingAt(const Player& pl, Vector2 p, float cosT, float h) {
    Vector3 f = Forward(pl), e = EyePos(pl), to = {p.x - e.x, h - e.y, p.y - e.z};
    float m = sqrtf(to.x * to.x + to.y * to.y + to.z * to.z);
    return m < 0.001f || (to.x * f.x + to.y * f.y + to.z * f.z) / m > cosT;
}
static float Vol(float d) { return std::clamp(1.0f - d / 30.0f, 0.1f, 1.0f); }
static float Pan(const Player& pl, Vector2 p) {
    float dx = p.x - pl.pos.x, dz = p.y - pl.pos.z, m = hypotf(dx, dz) + 0.001f;
    return 0.5f + 0.5f * ((-cosf(pl.yaw)) * dx + sinf(pl.yaw) * dz) / m;
}
static void Hurt(Player& pl, float dmg, float san) { pl.health -= dmg; pl.sanity -= san; pl.hurt = 1; Haptic(180); Sfx(SFX_HIT, 1.0f); }

static void TrySpawn(const Player& pl) {
    int hostile = 0; bool cindy = false;
    for (auto& e : gE) { if (e.type == E_CINDY) cindy = true; else hostile++; }
    int r = GetRandomValue(0, 99);
    EType t = r < 38 ? E_HOUND : r < 68 ? E_SMILER : r < 90 ? E_STEALER : E_CINDY;
    if (t == E_CINDY && (cindy || pl.sanity > 65)) t = E_HOUND;     // Синди приходит к тем, кому плохо
    if (t != E_CINDY && hostile >= 4) return;
    Vector2 pp = {pl.pos.x, pl.pos.z};
    for (int k = 0; k < 30; k++) {
        float a = Frand(0, 6.2832f), d = Frand(16, 28);
        int cx = CellOf(pp.x + sinf(a) * d), cz = CellOf(pp.y + cosf(a) * d);
        if (IsSolid(cx, cz)) continue;
        Vector2 p = CellCenter(cx, cz);
        if (t == E_SMILER && LightAt(p.x, p.y) > 0.3f) continue;       // только в темноте
        if (t != E_CINDY && d < 22 && LineClear(pp, p)) continue;      // не появляться на глазах
        Entity e{}; e.type = t; e.pos = p; e.wp = p; e.target = p; e.st = S_IDLE;
        e.timer = Frand(0.5f, 2.0f); e.yaw = Frand(0, 6.28f);
        e.life = t == E_HOUND ? 120.f : t == E_SMILER ? 90.f : t == E_STEALER ? 150.f : 45.f;
        if (t == E_STEALER || t == E_CINDY) { e.st = S_PATROL; e.target = RandomOpen(p, 10); }
        gE.push_back(e); return;
    }
}

static void Think(Entity& e, Player& pl, float dt) {
    Vector2 pp = {pl.pos.x, pl.pos.z};
    float d = Dist(e.pos, pp);
    e.t += dt;
    switch (e.type) {
    case E_HOUND: {   // слышит шум, преследует; зрительный контакт заставляет колебаться
        float hear = pl.noise * 20.0f;
        bool eye = d < 9 && d > 1.5f && LookingAt(pl, e.pos, 0.93f, 0.7f) && LineClear(pp, e.pos);
        auto alert = [&] { e.st = S_CHASE; e.lose = 0; Sfx(SFX_HOUND, Vol(d), Pan(pl, e.pos)); };
        switch (e.st) {
        case S_IDLE:   e.timer -= dt; if (e.timer <= 0) { e.target = RandomOpen(e.pos, 10); e.st = S_PATROL; } if (d < hear) alert(); break;
        case S_PATROL: Follow(e, e.target, 2.4f, dt); if (Dist(e.pos, e.target) < 1.2f) { e.st = S_IDLE; e.timer = Frand(1, 3); } if (d < hear) alert(); break;
        case S_CHASE:
            Follow(e, pp, eye ? 1.2f : 4.8f, dt);
            if (d < 1.4f) { e.st = S_ATTACK; e.atkCd = 0.2f; }
            if (d > hear * 1.2f + 6) { e.lose += dt; if (e.lose > 5) { e.st = S_PATROL; e.target = RandomOpen(e.pos, 10); } } else e.lose = 0;
            break;
        case S_ATTACK: e.atkCd -= dt; if (e.atkCd <= 0) { if (d < 1.7f) Hurt(pl, 10, 4); e.atkCd = 0.9f; } if (d >= 1.9f) e.st = S_CHASE; break;
        }
    } break;

    case E_SMILER: {  // прячется в темноте; боится света; нападает, если на него долго смотреть
        float L = LightAt(e.pos.x, e.pos.y);
        bool clear = LineClear(pp, e.pos);
        bool seen = clear && d < 18 && LookingAt(pl, e.pos, 0.90f, 1.8f);
        bool beam = pl.flash && pl.battery > 0 && clear && d < 10 && LookingAt(pl, e.pos, 0.93f, 1.8f);
        if (e.st == S_IDLE || e.st == S_PATROL) {
            Face(e, pp);
            if (L > 0.5f || beam) e.life = fminf(e.life, 0.8f);          // растворяется на свету
            if (seen) {
                e.lookT += dt; pl.sanity -= 3.0f * dt;
                if (e.lookT > 2.5f) { e.st = S_CHASE; Sfx(SFX_SMILER, 1.0f, Pan(pl, e.pos)); Say(T("The Smiler is coming!", "Улыбающийся идёт на вас!"), 2.5f); }
            } else {
                e.lookT = fmaxf(0, e.lookT - dt * 0.5f);
                if (d > 2.5f) Follow(e, pp, 0.8f, dt);                   // подкрадывается, пока не смотрят
            }
        } else if (e.st == S_CHASE) {
            Follow(e, pp, 6.0f, dt);
            if (beam) { e.st = S_IDLE; e.lookT = 0; e.life = 0.8f; }     // свет фонарика прогоняет
            if (d < 1.4f) { Hurt(pl, 25, 15); Haptic(300); e.life = 0.01f; }
        }
    } break;

    case E_STEALER: { // маскируется под человека, раскрывается вблизи или со спины
        bool clear = LineClear(pp, e.pos);
        if (!e.flag) {
            bool away = !LookingAt(pl, e.pos, -0.2f, 1.0f);
            bool lit  = pl.flash && pl.battery > 0 && d < 9 && clear && LookingAt(pl, e.pos, 0.95f, 1.5f);
            auto reveal = [&](float delay) {
                e.flag = true; e.st = S_IDLE; e.timer = delay; Face(e, pp);
                Sfx(SFX_SMILER, 0.9f, Pan(pl, e.pos)); Haptic(120);
            };
            if (e.st == S_PATROL) {
                Follow(e, e.target, 1.3f, dt);
                if (Dist(e.pos, e.target) < 1.2f) e.target = RandomOpen(e.pos, 10);
                if (clear && d < 20) { e.st = S_CHASE; e.timer = 0; }
            } else {      // подходит "помахать" и держит дистанцию ~4.5 м
                if (!e.lose) { e.lose = 1; Say(T("A wanderer waves at you... something is off.", "Какой-то скиталец машет вам... что-то не так."), 3.5f); }
                if (d > 4.5f) Follow(e, pp, 2.0f, dt); else Face(e, pp);
                e.timer += dt;
                if (lit) { e.lookT += dt; if (e.lookT > 1.0f) { Say(T("Its eyes don't reflect the light. SKIN-STEALER!", "Глаза не отражают свет. КОЖЕКРАД!"), 3.0f); reveal(2.0f); } }
                else if (d < 3.2f || (away && d < 8.0f) || e.timer > 25) reveal(1.0f);
            }
        } else if (e.st == S_IDLE) { Face(e, pp); e.timer -= dt; if (e.timer <= 0) e.st = S_CHASE; }
        else if (e.st == S_CHASE) { Follow(e, pp, 4.8f, dt); if (d < 1.5f) { e.st = S_ATTACK; e.atkCd = 0.1f; } }
        else if (e.st == S_ATTACK) { e.atkCd -= dt; if (e.atkCd <= 0) { if (d < 1.8f) Hurt(pl, 30, 10); e.atkCd = 1.2f; } if (d >= 2.0f) e.st = S_CHASE; }
    } break;

    case E_CINDY: {   // добрая сущность: следует за игроком и лечит рассудок
        Face(e, pp);
        if (d > 3.5f) Follow(e, pp, 2.4f, dt);
        if (d < 5.0f) {
            if (!e.flag) { e.flag = true; Sfx(SFX_CINDY, 0.8f, 0.5f); Say(T("Cindy is with you. You feel calmer.", "Синди рядом. Становится спокойнее."), 4.0f); }
            pl.sanity = fminf(100, pl.sanity + 4.0f * dt); pl.health = fminf(100, pl.health + 0.5f * dt);
        }
    } break;
    }
}

void EntitiesReset() { gE.clear(); gSpawnT = 25.0f; }

void EntitiesUpdate(Player& pl, float dt, float insane) {
    gSpawnT -= dt * (1.0f + insane * 1.5f + (pl.sanity <= 0 ? 2.0f : 0.0f));
    if (gSpawnT <= 0) { gSpawnT = Frand(10, 20); TrySpawn(pl); }
    Vector2 pp = {pl.pos.x, pl.pos.z};
    for (size_t i = 0; i < gE.size();) {
        Entity& e = gE[i];
        float d = Dist(e.pos, pp);
        e.life -= dt * (d > 42 ? 4.0f : 1.0f);
        if (d <= 42) Think(e, pl, dt);                       // дальше не думаем - экономим BFS и генерацию чанков
        if (e.life <= 0 || d > 55) gE.erase(gE.begin() + i); else i++;
    }
}

// ---------- отрисовка ----------
static Color C(int r, int g, int b, float L) { L = L < 0 ? 0 : L > 1 ? 1 : L; return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)(L * 255)}; }
static void Beg(const Entity& e) { rlPushMatrix(); rlTranslatef(e.pos.x - gOrg.x, 0, e.pos.y - gOrg.z); rlRotatef(e.yaw * RAD2DEG, 0, 1, 0); }

void EntitiesDraw(const Player& pl, bool glow) {
    Vector2 pp = {pl.pos.x, pl.pos.z};
    for (const Entity& e : gE) {
        if (Dist(e.pos, pp) > 34) continue;
        float L = LightAt(e.pos.x, e.pos.y);
        Beg(e);
        switch (e.type) {
        case E_HOUND:
            if (!glow) {
                float sw = sinf(e.t * 14) * (e.st == S_CHASE ? 0.14f : 0.05f);
                Color body = C(72, 56, 46, L), head = C(112, 94, 82, L), leg = C(58, 44, 36, L);
                DrawCube({0, 0.62f, 0}, 0.5f, 0.48f, 1.1f, body);
                DrawCube({0, 0.74f, 0.72f}, 0.36f, 0.34f, 0.42f, head);
                DrawCube({-0.2f, 0.22f, 0.38f + sw}, 0.11f, 0.44f, 0.11f, leg);
                DrawCube({0.2f, 0.22f, 0.38f - sw}, 0.11f, 0.44f, 0.11f, leg);
                DrawCube({-0.2f, 0.22f, -0.38f - sw}, 0.11f, 0.44f, 0.11f, leg);
                DrawCube({0.2f, 0.22f, -0.38f + sw}, 0.11f, 0.44f, 0.11f, leg);
            } else {
                DrawCube({-0.1f, 0.8f, 0.94f}, 0.06f, 0.05f, 0.04f, C(255, 40, 30, 1));
                DrawCube({0.1f, 0.8f, 0.94f}, 0.06f, 0.05f, 0.04f, C(255, 40, 30, 1));
            }
            break;
        case E_SMILER:
            if (!glow) {   // чёрный силуэт - виден только на свету фонарика
                Color k = C(10, 10, 10, L);
                DrawCube({0, 0.95f, 0}, 0.55f, 1.3f, 0.3f, k);
                DrawCube({0, 1.85f, 0}, 0.34f, 0.38f, 0.3f, k);
                DrawCube({-0.38f, 1.0f, 0}, 0.12f, 1.2f, 0.12f, k);
                DrawCube({0.38f, 1.0f, 0}, 0.12f, 1.2f, 0.12f, k);
                DrawCube({-0.14f, 0.3f, 0}, 0.18f, 0.6f, 0.2f, k);
                DrawCube({0.14f, 0.3f, 0}, 0.18f, 0.6f, 0.2f, k);
            } else {       // светящиеся глаза и улыбка
                Color w = C(255, 255, 235, 1);
                DrawCube({-0.1f, 1.92f, 0.16f}, 0.09f, 0.16f, 0.04f, w);
                DrawCube({0.1f, 1.92f, 0.16f}, 0.09f, 0.16f, 0.04f, w);
                for (int i = -4; i <= 4; i++) DrawCube({i * 0.04f, 1.72f + 0.006f * i * i, 0.16f}, 0.04f, 0.035f, 0.04f, w);
            }
            break;
        case E_STEALER:
            if (!glow) {
                if (!e.flag) {   // "человек": оранжевый жилет M.E.G.
                    Color skin = C(215, 170, 140, L);
                    DrawCube({0, 1.15f, 0}, 0.5f, 0.65f, 0.28f, C(235, 120, 20, L));
                    DrawCube({-0.13f, 0.4f, 0}, 0.2f, 0.8f, 0.22f, C(50, 70, 120, L));
                    DrawCube({0.13f, 0.4f, 0}, 0.2f, 0.8f, 0.22f, C(50, 70, 120, L));
                    DrawCube({0, 1.67f, 0}, 0.26f, 0.28f, 0.26f, skin);
                    DrawCube({-0.34f, 1.1f, 0}, 0.12f, 0.6f, 0.12f, skin);
                    DrawCube({0.34f, 1.1f, 0}, 0.12f, 0.6f, 0.12f, skin);
                } else {         // истинная форма: высокий, "без кожи"
                    Color f = C(190, 100, 90, L);
                    DrawCube({0, 1.45f, 0}, 0.42f, 1.1f, 0.24f, f);
                    DrawCube({-0.12f, 0.5f, 0}, 0.14f, 1.0f, 0.16f, f);
                    DrawCube({0.12f, 0.5f, 0}, 0.14f, 1.0f, 0.16f, f);
                    DrawCube({0, 2.25f, 0.02f}, 0.2f, 0.42f, 0.22f, C(205, 170, 150, L));
                    DrawCube({-0.38f, 1.2f, 0.05f}, 0.08f, 1.3f, 0.08f, f);
                    DrawCube({0.38f, 1.2f, 0.05f}, 0.08f, 1.3f, 0.08f, f);
                }
            } else if (e.flag) {
                DrawCube({-0.06f, 2.3f, 0.12f}, 0.04f, 0.04f, 0.03f, C(255, 30, 30, 1));
                DrawCube({0.06f, 2.3f, 0.12f}, 0.04f, 0.04f, 0.03f, C(255, 30, 30, 1));
            }
            break;
        case E_CINDY:
            if (glow) {
                float bob = sinf(e.t * 2.0f) * 0.06f;
                DrawCube({0, 0.7f + bob, 0}, 0.28f, 0.55f, 0.18f, C(150, 200, 255, 1));
                DrawCube({0, 1.1f + bob, 0}, 0.2f, 0.2f, 0.18f, C(190, 225, 255, 1));
            }
            break;
        }
        rlPopMatrix();
    }
}
