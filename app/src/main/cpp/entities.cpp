// entities.cpp - ИИ сущностей. Лор: Гончая (Entity 8) слышит шум; Улыбающийся (Entity 3) боится света и
// нападает, если долго смотреть в тьму на него; Кожекрад (Entity 10) выдаёт себя за человека; Синди - добрая.
#include "entities.h"
#include "map_gen.h"
#include "level2.h"
#include "rlgl.h"
#include <vector>
#include <cctype>
#include <cstring>
#include <algorithm>

static std::vector<Entity> gE;
static float gSpawnT = 25.0f;
static bool gHoundRage = false;

// Столкновение игрока только с постоянной Физичкой.
bool EntitiesCircleHitsPlayer(float x, float z, float playerRadius) {
    const float HOUND_RADIUS = HOUND_COLLISION_RADIUS;
    const float minDistance = playerRadius + HOUND_RADIUS;
    const float minDistanceSq = minDistance * minDistance;

    for (const Entity& e : gE) {
        if (e.type != E_HOUND || !e.persistent) continue;

        float dx = x - e.pos.x;
        float dz = z - e.pos.y;

        if (dx * dx + dz * dz < minDistanceSq) {
            return true;
        }
    }

    return false;
}

struct FlyingItem {
    Vector3 p{}, v{};
    float life{};
    bool bottle{};
    int textbook{-1};
    bool settled{};            // учебник лежит на полу и ждёт Физичку
    bool houndTextbook{};          // учебник, брошенный именно Физичкой
    bool hitPlayer{};          // попадание учитывается только один раз
    int bounces{};
    float rotation{};          // градусы
    float spin{};              // градусов в секунду
    Vector3 rotationAxis{0.6f, 0.3f, 0.7f};
};
static std::vector<FlyingItem> gFlying;
static Texture2D gWomanFace{};
static bool gTexturesLoaded = false;

// GLB-модель Гончей
static Model gHoundModel{};
static ModelAnimation* gHoundAnimations = nullptr;
static int gHoundAnimCount = 0;
static int gHoundIdle = -1;
static int gHoundWalk = -1;
static int gHoundRun = -1;
static bool gHoundLoadAttempted = false;
static ModelAnimation* gHoundThrowAnimations = nullptr;
static int gHoundThrowAnimCount = 0;
static int gHoundThrow = -1;
static ModelAnimation* gHoundThrowAnimation = nullptr;
static float gHoundThrowDuration = 0.90f;
static Model gTextbookModels[4]{};
static bool gTextbookModelLoaded[4]{};
static bool gTextbookLoadAttempted = false;

static constexpr int HOUND_ACTION_NONE = 0;
static constexpr int HOUND_ACTION_MELEE = 1;
static constexpr int HOUND_ACTION_THROW = 2;
static constexpr float TEXTBOOK_HELD_SCALE = 0.928f;   // Уменьшен в 2,5 раза
static constexpr float TEXTBOOK_FLY_SCALE = 0.992f;    // Уменьшен в 2,5 раза
static constexpr float HOUND_MELEE_RANGE = 0.62f;
static constexpr float HOUND_THROW_MIN_RANGE = 3.0f;
static constexpr float HOUND_THROW_MAX_RANGE = 5.0f;
static constexpr float HOUND_TEXTBOOK_BOOST = 1.20f;
static constexpr float HOUND_TEXTBOOK_BOOST_TIME = 2.0f;

static void LoadEntityTextures() {
    if (gTexturesLoaded) return;
    gWomanFace = LoadTexture("woman_face.png");
    gTexturesLoaded = true;
}


static void LoadTextbookModels() {
    if (gTextbookLoadAttempted) return;
    gTextbookLoadAttempted = true;

    const char* paths[4] = {
        "models/textbook_10.glb",
        "models/textbook_9.glb",
        "models/textbook_8.glb",
        "models/textbook_7.glb"
    };
    for (int i = 0; i < 4; ++i) {
        TraceLog(LOG_INFO, "HOUND textbook: attempting %s", paths[i]);
        gTextbookModels[i] = LoadModel(paths[i]);
        gTextbookModelLoaded[i] = gTextbookModels[i].meshCount > 0;
        if (!gTextbookModelLoaded[i])
            TraceLog(LOG_WARNING, "HOUND textbook: failed to load %s", paths[i]);
        else
            TraceLog(LOG_INFO, "HOUND textbook: loaded %s", paths[i]);
    }

}

static int RandomLoadedTextbookIndex() {
    LoadTextbookModels();

    int available[4];
    int count = 0;

    for (int i = 0; i < 4; ++i) {
        if (gTextbookModelLoaded[i])
            available[count++] = i;
    }

    if (count == 0) return 0;
    return available[GetRandomValue(0, count - 1)];
}

static Model* GetTextbookModel(int index) {
    LoadTextbookModels();
    if (index < 0 || index >= 4) index = 0;
    if (gTextbookModelLoaded[index]) return &gTextbookModels[index];
    for (int i = 0; i < 4; ++i)
        if (gTextbookModelLoaded[i]) return &gTextbookModels[i];
    return nullptr;
}

static bool ContainsIgnoreCase(const char* text, const char* part) {
    if (!text || !part || !*part) return false;

    for (; *text; ++text) {
        const char* a = text;
        const char* b = part;

        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }

        if (!*b) return true;
    }

    return false;
}

static int FindHoundAnimation(const char* namePart) {
    if (!gHoundAnimations || !namePart) return -1;

    for (int i = 0; i < gHoundAnimCount; i++) {
        if (ContainsIgnoreCase(gHoundAnimations[i].name, namePart)) return i;
    }
    return -1;
}

static bool SameHoundSkeleton(const ModelAnimation& anim);

static bool LoadHoundModel() {
    if (gHoundLoadAttempted) return gHoundModel.meshCount > 0;

    gHoundLoadAttempted = true;

    gHoundModel = LoadModel("models/FIZI4KA.glb");

for (int i = 0; i < gHoundModel.materialCount; i++) {
    Material *m = &gHoundModel.materials[i];

    TraceLog(LOG_INFO,
        "HOUND MAT %d: albedo=%u normal=%u roughness=%u metalness=%u occlusion=%u",
        i,
        m->maps[MATERIAL_MAP_ALBEDO].texture.id,
        m->maps[MATERIAL_MAP_NORMAL].texture.id,
        m->maps[MATERIAL_MAP_ROUGHNESS].texture.id,
        m->maps[MATERIAL_MAP_METALNESS].texture.id,
        m->maps[MATERIAL_MAP_OCCLUSION].texture.id
    );
}


    if (gHoundModel.meshCount <= 0) {
        TraceLog(LOG_ERROR, "HOUND: failed to load GLB model");
        return false;
    }

    // Диагностика скелета Физички.
    TraceLog(LOG_INFO, "HOUND BONES: count=%d", gHoundModel.boneCount);

    for (int i = 0; i < gHoundModel.boneCount; ++i) {
        TraceLog(LOG_INFO,
            "HOUND BONE: index=%d name=%s parent=%d",
            i,
            gHoundModel.bones[i].name,
            gHoundModel.bones[i].parent
        );
    }

    gHoundAnimations = LoadModelAnimations(
        "models/FIZI4KA.glb",
        &gHoundAnimCount
    );

    gHoundIdle = FindHoundAnimation("Idle_Breathing");
    gHoundWalk = FindHoundAnimation("Walk");
    gHoundRun = FindHoundAnimation("Run");

    // Сначала загружаем отдельный клип броска.
    // Встроенная анимация используется только как запасной вариант.
    TraceLog(LOG_INFO, "HOUND THROW: attempting models/FIZI4KA_throw.glb");

    gHoundThrowAnimations = LoadModelAnimations(
            "models/FIZI4KA_throw.glb", &gHoundThrowAnimCount
        );

        TraceLog(LOG_INFO,
            "HOUND THROW FILE: animations=%d baseBones=%d",
            gHoundThrowAnimCount, gHoundModel.boneCount);

        for (int i = 0; gHoundThrowAnimations && i < gHoundThrowAnimCount; ++i) {
            const ModelAnimation& anim = gHoundThrowAnimations[i];

            TraceLog(LOG_INFO,
                "HOUND THROW ANIM: index=%d name=%s frames=%d bones=%d customValid=%d",
                i, anim.name, anim.frameCount, anim.boneCount,
                SameHoundSkeleton(anim) ? 1 : 0);
        }

        // Ищем отдельный клип по универсальной части имени.
        const char* preferredNames[] = {"throwtextbook", "throw"};
        for (const char* preferred : preferredNames) {
            for (int i = 0; gHoundThrowAnimations && i < gHoundThrowAnimCount; ++i) {
                if (ContainsIgnoreCase(gHoundThrowAnimations[i].name, preferred) &&
                    gHoundThrowAnimations[i].frameCount > 1 &&
                    SameHoundSkeleton(gHoundThrowAnimations[i])) {
                    gHoundThrow = i;
                    gHoundThrowAnimation = &gHoundThrowAnimations[i];
                    break;
                }
            }
            if (gHoundThrowAnimation) break;
        }

    // Запасной вариант — анимация из основной модели.
    if (!gHoundThrowAnimation) {
        gHoundThrow = FindHoundAnimation("throw");

        if (gHoundThrow >= 0 &&
            gHoundAnimations[gHoundThrow].frameCount > 1) {
            gHoundThrowAnimation = &gHoundAnimations[gHoundThrow];
        }
    }

    if (gHoundThrowAnimation) {
        gHoundThrowDuration =
            (float)(gHoundThrowAnimation->frameCount - 1) * 0.017f;
        TraceLog(LOG_INFO, "HOUND: throw animation loaded, duration %.3f s",
                 gHoundThrowDuration);
    } else {
        TraceLog(LOG_WARNING,
                 "HOUND: no compatible throw animation; using fallback timing");
    }
    LoadTextbookModels();

    TraceLog(LOG_INFO,
        "HOUND: GLB loaded, animations: %d, idle=%d walk=%d run=%d",
        gHoundAnimCount, gHoundIdle, gHoundWalk, gHoundRun
    );

    for (int i = 0; i < gHoundAnimCount; i++) {
        TraceLog(LOG_INFO,
            "HOUND ANIM: %s, frames=%d",
            gHoundAnimations[i].name,
            gHoundAnimations[i].frameCount
        );
    }

    return true;
}

static bool SameHoundSkeleton(const ModelAnimation& anim) {
    if (anim.boneCount != gHoundModel.boneCount || anim.boneCount <= 0) return false;
    for (int i = 0; i < anim.boneCount; ++i) {
        if (strcmp(anim.bones[i].name, gHoundModel.bones[i].name) != 0 ||
            anim.bones[i].parent != gHoundModel.bones[i].parent) return false;
    }
    return true;
}

static float HoundActionDuration() {
    float duration = gHoundThrowAnimation && gHoundThrowAnimation->frameCount > 1
        ? (float)(gHoundThrowAnimation->frameCount - 1) * 0.017f
        : gHoundThrowDuration;
    return gHoundRage ? duration * 0.5f : duration;
}

static void UpdateHoundAnimation(const Entity& e) {
    // Одноразовая атака должна работать и тогда, когда бросковый клип
    // загружен отдельным GLB, даже если набор базовых клипов пуст.
    if (e.houndAction != HOUND_ACTION_NONE && gHoundThrowAnimation &&
        SameHoundSkeleton(*gHoundThrowAnimation) && gHoundThrowAnimation->frameCount > 0) {
        int frame = std::clamp((int)(e.actionT / 0.017f), 0, gHoundThrowAnimation->frameCount - 1);
        UpdateModelAnimation(gHoundModel, *gHoundThrowAnimation, frame);
        return;
    }

    if (!gHoundAnimations || gHoundAnimCount <= 0) return;

    int index = gHoundIdle;

    // Во время атаки не переключаемся на бег.
    if (e.houndAction != HOUND_ACTION_NONE) {
        if (index < 0 || index >= gHoundAnimCount) return;
        ModelAnimation anim = gHoundAnimations[index];
        if (anim.frameCount <= 0) return;
        int frame = (int)(e.t * 60.0f) % anim.frameCount;
        UpdateModelAnimation(gHoundModel, anim, frame);
        return;
    }

    if (e.st == S_CHASE || e.st == S_ATTACK) {
        if (gHoundRun >= 0) index = gHoundRun;
        else if (gHoundWalk >= 0) index = gHoundWalk;
    } else if (e.st == S_PATROL) {
        if (gHoundWalk >= 0) index = gHoundWalk;
    }
    if (index < 0 || index >= gHoundAnimCount) return;
    ModelAnimation anim = gHoundAnimations[index];
    if (anim.frameCount <= 0) return;
    int frame = (int)(e.t * 60.0f) % anim.frameCount;
    UpdateModelAnimation(gHoundModel, anim, frame);
}

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

            // Проверяем, помещается ли Физичка в центре следующей клетки.
            Vector2 candidate = CellCenter(nx, nz);
            if (CircleHits(candidate.x, candidate.y, 0.35f)) continue;

            seen[ni] = 1;
            prev[ni] = (int16_t)cur;
            q[tail++] = (int16_t)ni;
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

// Отдельное движение Физички: плавный поворот и контроль застревания.
static void FollowHound(Entity& e, Vector2 target, float speed, float dt, bool avoidPlayer = true) {
    if (dt <= 0.0f) return;

    float moved = Dist(e.pos, e.lastPos);

    if (moved < 0.015f) {
        e.stuckT += dt;
    } else {
        e.stuckT = 0.0f;
    }

    e.lastPos = e.pos;

    float distance = Dist(e.pos, target);

    if (distance < 0.08f) {
        e.stuckT = 0.0f;
        e.houndVelocity = {0.0f, 0.0f};
        return;
    }

    e.pathT -= dt;

    bool direct = false;

    if (distance < 2.4f && LineClear(e.pos, target)) {
        direct = true;
        int samples = (int)ceilf(distance / 0.25f);

        for (int i = 1; i <= samples; ++i) {
            float t = (float)i / (float)samples;
            float x = e.pos.x + (target.x - e.pos.x) * t;
            float z = e.pos.y + (target.y - e.pos.y) * t;

            if (CircleHits(x, z, 0.35f)) {
                direct = false;
                break;
            }
        }
    }

    if (e.pathT <= 0.0f || e.stuckT > 0.35f) {
        e.pathT = 0.22f;

        if (!direct) {
            Vector2 next{};

            if (NextStep(e.pos, target, &next)) {
                e.wp = next;
            } else {
                e.wp = e.pos;
            }
        }

        if (e.stuckT > 1.0f) {
            e.pathT = 0.0f;
        }
    }

    Vector2 goal = direct ? target : e.wp;
    Vector2 delta = {goal.x - e.pos.x, goal.y - e.pos.y};
    float len = hypotf(delta.x, delta.y);

    if (len < 0.12f) {
        e.pathT = 0.0f;
        e.houndVelocity.x *= expf(-10.0f * dt);
        e.houndVelocity.y *= expf(-10.0f * dt);
        return;
    }

    float desiredYaw = atan2f(delta.x, delta.y);
    float angle = atan2f(
        sinf(desiredYaw - e.yaw),
        cosf(desiredYaw - e.yaw)
    );

    constexpr float TURN_RATE = 2.7925268f;
    float maxTurn = TURN_RATE * dt;
    e.yaw += std::clamp(angle, -maxTurn, maxTurn);

    float remainingAngle = atan2f(
        sinf(desiredYaw - e.yaw),
        cosf(desiredYaw - e.yaw)
    );

    float alignment = fmaxf(0.0f, cosf(remainingAngle));

    // Скорость плавно уменьшается возле точки назначения.
    float targetSpeed = fminf(speed, len * 2.0f) * alignment;

    Vector2 desiredVelocity = {
        sinf(e.yaw) * targetSpeed,
        cosf(e.yaw) * targetSpeed
    };

    // Постепенно изменяем текущую скорость к требуемой.
    float acceleration = 9.0f;
    float braking = 11.0f;

    float vx = desiredVelocity.x - e.houndVelocity.x;
    float vz = desiredVelocity.y - e.houndVelocity.y;
    float velocityDifference = hypotf(vx, vz);

    float maxChange = (targetSpeed < 0.05f ? braking : acceleration) * dt;

    if (velocityDifference > maxChange && velocityDifference > 0.0001f) {
        float scale = maxChange / velocityDifference;
        vx *= scale;
        vz *= scale;
    }

    e.houndVelocity.x += vx;
    e.houndVelocity.y += vz;

    constexpr float PLAYER_RADIUS = 0.30f;
    constexpr float MIN_PLAYER_DISTANCE =
        HOUND_COLLISION_RADIUS + PLAYER_RADIUS;

    float nx = e.pos.x + e.houndVelocity.x * dt;
    float nz = e.pos.y + e.houndVelocity.y * dt;

    auto CanMoveTo = [&](float x, float z) {
        if (CircleHits(x, z, 0.35f)) return false;

        if (avoidPlayer) {
            float currentDx = e.pos.x - target.x;
            float currentDz = e.pos.y - target.y;
            float currentDistanceSq =
                currentDx * currentDx + currentDz * currentDz;

            float dx = x - target.x;
            float dz = z - target.y;
            float distanceSq = dx * dx + dz * dz;
            float minDistanceSq =
                MIN_PLAYER_DISTANCE * MIN_PLAYER_DISTANCE;

            if (distanceSq < minDistanceSq &&
                distanceSq <= currentDistanceSq) {
                return false;
            }
        }

        return true;
    };

    bool movedX = CanMoveTo(nx, e.pos.y);
    if (movedX) {
        e.pos.x = nx;
    } else {
        e.houndVelocity.x = 0.0f;
    }

    bool movedZ = CanMoveTo(e.pos.x, nz);
    if (movedZ) {
        e.pos.y = nz;
    } else {
        e.houndVelocity.y = 0.0f;
    }

    if (!movedX && !movedZ) {
        e.pathT = 0.0f;
    }
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

static void StartHoundAction(Entity& e, int action) {
    if (action == HOUND_ACTION_THROW) {
        e.textbookIndex = RandomLoadedTextbookIndex();
    }
    e.houndAction = action;
    e.actionT = 0.0f;
    e.actionEvent = false;
    e.st = S_ATTACK;
}

static void LaunchHoundTextbook(Entity& e, const Player& pl) {
    const float fx = sinf(e.yaw);
    const float fz = cosf(e.yaw);
    Vector3 start = {e.pos.x + fx * 0.42f, 1.18f, e.pos.y + fz * 0.42f};
    Vector3 target = {pl.pos.x, 1.20f, pl.pos.z};
    float dx = target.x - start.x;
    float dz = target.z - start.z;
    float distance = hypotf(dx, dz);
    const float flightTime = std::clamp(0.78f + distance * 0.018f, 0.84f, 1.02f);
    const float gravity = 9.81f;
    Vector3 velocity{};
    velocity.x = dx / flightTime;
    velocity.z = dz / flightTime;
    velocity.y = (target.y - start.y + 0.5f * gravity * flightTime * flightTime) / flightTime;


    FlyingItem textbook{};
    textbook.houndTextbook = true;
    textbook.p = start;
    textbook.v = velocity;
    textbook.life = 5.0f;
    textbook.bottle = false;
    textbook.textbook = e.textbookIndex;
    textbook.rotation = Frand(-25.0f, 25.0f);
    textbook.spin = Frand(420.0f, 620.0f);
    textbook.rotationAxis = {0.65f, 0.25f, 0.72f};
    gFlying.push_back(textbook);

    e.textbookHeld = false;
    // Ускорение начнётся после завершения бросковой анимации,
    // чтобы все 2.0 секунды Физичка действительно бежала к учебнику.
    e.textbookBoostT = 0.0f;
    e.throwCd = Frand(3.8f, 5.4f) * (gHoundRage ? 0.5f : 1.0f);
    Sfx(SFX_HOUND, 0.9f);
}

static void UpdateHoundAction(Entity& e, Player& pl, float dt) {
    const int action = e.houndAction;
    const float duration = std::clamp(HoundActionDuration(), 0.45f, 1.8f);
    e.actionT += dt;
    const float eventTime = duration * (action == HOUND_ACTION_THROW ? 0.44f : 0.30f);

    if (!e.actionEvent && e.actionT >= eventTime) {
        e.actionEvent = true;
        if (action == HOUND_ACTION_THROW && e.textbookHeld) {
            LaunchHoundTextbook(e, pl);
        } else if (action == HOUND_ACTION_MELEE) {
            Vector2 pp = {pl.pos.x, pl.pos.z};
            float d = Dist(e.pos, pp);
            if (d <= HOUND_MELEE_RANGE && LineClear(e.pos, pp)) {
                Hurt(pl, 24.0f, 3.0f);
            }
        }
    }

    if (e.actionT >= duration) {
        e.houndAction = HOUND_ACTION_NONE;
        e.actionT = 0.0f;
        e.actionEvent = false;
        e.st = S_CHASE;
    }
}

static void SettleTextbook(FlyingItem& q) {
    q.settled = true;
    q.v = {0.0f, 0.0f, 0.0f};
    q.p.y = 0.011f;
    q.rotation = 90.0f;
    q.rotationAxis = {1.0f, 0.0f, 0.0f};
    q.spin = 0.0f;
}

static void TrySpawn(const Player& pl) {
    int hostile = 0; bool cindy = false;
    for (auto& e : gE) { if (e.type == E_CINDY) cindy = true; else hostile++; }
    int r = GetRandomValue(0, 99);
    // На уровне 0 Гончая только одна: особая сущность в тёмной комнате.
    // Остальные вероятности перенормированы без случайного спавна Гончей.
    // Диапазон бывшего Кожекрада отключён: он больше не появляется.
    if (r >= 48 && r < 84) return;
    EType t = r < 48 ? E_SMILER : E_CINDY;
    if (t == E_CINDY && (cindy || pl.sanity > 65)) return; // Синди приходит к тем, кому плохо
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


// Границы внутренней части комнаты Гончей.
// Комната занимает клетки x=5..7, z=1..3.
static bool HoundRoomContains(Vector2 p) {
    if (Level2IsActive()) return Level2IsDark(p.x, p.y);
    int cx = CellOf(p.x);
    int cz = CellOf(p.y);
    return cx >= 5 && cx <= 7 && cz >= 1 && cz <= 3;
}

static Vector2 HoundRoomPoint() {
    if (Level2IsActive()) return Level2PatrolPoint(GetRandomValue(0, 5));
    int cx = GetRandomValue(5, 7);
    int cz = GetRandomValue(1, 3);
    return CellCenter(cx, cz);
}

static bool Level2PlayerNearDarkZone(Vector2 player) {
    if (Level2IsDark(player.x, player.y)) return true;
    for (int i=0;i<6;i++) {
        Vector2 spot=Level2PatrolPoint(i);
        if (Level2IsDark(spot.x,spot.y) && Dist(player,spot)<6.5f) return true;
    }
    return false;
}

static Vector2 Level2NearestHidePoint(Vector2 from) {
    Vector2 best=Level2PatrolPoint(0);
    float bestD=1.0e9f;
    for(int i=0;i<6;i++) {
        Vector2 q=Level2PatrolPoint(i);
        if(!Level2IsDark(q.x,q.y) || Level2CircleHits(q.x,q.y,0.34f)) continue;
        float d=Dist(from,q);
        if(d<bestD) { bestD=d; best=q; }
    }
    return best;
}

static bool Level2FindAmbushPoint(Vector2 player, Vector2* out) {
    float best=1.0e9f; bool found=false;
    for(int i=0;i<6;i++) {
        Vector2 q=Level2PatrolPoint(i);
        float d=Dist(player,q);
        if(d<3.8f || d>15.0f || !Level2IsDark(q.x,q.y) || Level2CircleHits(q.x,q.y,0.34f)) continue;
        // Телепортация разрешена только в закрытую от прямого взгляда точку.
        if(LineClear(player,q)) continue;
        if(d<best) { best=d; *out=q; found=true; }
    }
    return found;
}

static void ThinkHoundLevel2(Entity& e, Player& pl, float dt, Vector2 pp, float d) {
    const bool dangerZone=Level2PlayerNearDarkZone(pp);
    if(e.houndAction!=HOUND_ACTION_NONE) {
        e.houndVelocity={0.0f,0.0f};
        UpdateHoundAction(e,pl,dt);
        return;
    }

    if(e.st==S_CHASE) {
        if(!dangerZone) {
            e.lose+=dt;
            if(e.lose>1.15f) {
                e.st=S_PATROL;
                e.target=Level2NearestHidePoint(e.pos);
                e.lose=0.0f;
                e.houndVelocity={0.0f,0.0f};
            }
        } else e.lose=0.0f;

        if(e.st==S_CHASE) {
            if(d<=HOUND_MELEE_RANGE) {
                Face(e,pp);
                if(e.atkCd<=0.0f) { e.atkCd=0.58f; StartHoundAction(e,HOUND_ACTION_MELEE); }
                else FollowHound(e,pp,5.8f,dt);
            } else if(e.textbookHeld && d>=HOUND_THROW_MIN_RANGE && d<=HOUND_THROW_MAX_RANGE &&
                     e.throwCd<=0.0f && LineClear(e.pos,pp)) {
                Face(e,pp);
                StartHoundAction(e,HOUND_ACTION_THROW);
            } else {
                FollowHound(e,pp,5.8f,dt);
            }
            return;
        }
    }

    if(e.st==S_PATROL) {
        FollowHound(e,e.target,3.4f,dt,false);
        if(Dist(e.pos,e.target)<0.72f) {
            e.st=S_IDLE; e.timer=Frand(4.0f,8.0f); e.houndVelocity={0.0f,0.0f};
        }
    } else if(e.st==S_IDLE) {
        e.houndVelocity={0.0f,0.0f};
        if(dangerZone) {
            Vector2 ambush{};
            if(d>16.0f && Level2FindAmbushPoint(pp,&ambush)) {
                e.pos=ambush; e.lastPos=ambush; e.wp=ambush; e.target=ambush;
                e.houndVelocity={0.0f,0.0f};
            }
            e.st=S_CHASE; e.lose=0.0f;
            Sfx(SFX_HOUND,Vol(d),Pan(pl,e.pos));
            return;
        }
        e.timer-=dt;
        if(e.timer<=0.0f) {
            Vector2 next=Level2PatrolPoint(GetRandomValue(0,5));
            if(Dist(pp,next)>10.0f && !LineClear(pp,next) && Dist(e.pos,next)>7.0f) {
                // Смена тёмной зоны без появления на глазах игрока.
                e.pos=next; e.lastPos=next; e.wp=next; e.target=next;
                e.timer=Frand(5.0f,10.0f);
            } else {
                e.target=Level2NearestHidePoint(e.pos);
                if(Dist(e.pos,e.target)>0.8f) e.st=S_PATROL;
                else e.timer=Frand(4.0f,8.0f);
            }
        }
    }
}

static void Think(Entity& e, Player& pl, float dt) {
    if (e.type == E_HOUND && e.textbookBoostT > 0.0f)
        e.textbookBoostT = fmaxf(0.0f, e.textbookBoostT - dt);
    if (e.type == E_HOUND && e.stunT > 0) {
        e.houndVelocity = {0.0f, 0.0f};
        e.stunT = fmaxf(0, e.stunT - dt);
        e.yaw += sinf(e.t * 2.7f) * dt * 0.75f;
        return;
    }
    Vector2 pp = {pl.pos.x, pl.pos.z};
    float d = Dist(e.pos, pp);
    e.t += dt;
    switch (e.type) {
    case E_HOUND: {   // Физичка бьёт учебником вблизи и бросает его со средней дистанции.
        e.throwCd = fmaxf(0.0f, e.throwCd - dt);
        e.atkCd = fmaxf(0.0f, e.atkCd - dt);

        // После перезарядки Физичка получает новый учебник.
        if (!e.textbookHeld && e.throwCd <= 0.0f) {
            e.textbookHeld = true;
            e.textbookIndex = RandomLoadedTextbookIndex();
        }

        if (e.houndAction != HOUND_ACTION_NONE && !Level2IsActive()) {
            e.houndVelocity = {0.0f, 0.0f};
            UpdateHoundAction(e, pl, dt);
            break;
        }

        if (Level2IsActive()) {
            ThinkHoundLevel2(e,pl,dt,pp,d);
            break;
        }

        if (e.st != S_CHASE) {
            e.houndVelocity = {0.0f, 0.0f};
        }

        const bool industrial = Level2IsActive();
        float hear = pl.crouching ? (industrial ? 0.8f : 0.0f)
                                  : std::min(industrial ? 32.0f : 20.0f,
                                             pl.noise * (industrial ? 54.0f : 36.0f));
        bool eye = d < (industrial ? 15.0f : 9.0f) && d > 1.5f &&
                   LookingAt(pl, e.pos, 0.93f, industrial ? 1.2f : 0.7f) && LineClear(pp, e.pos);
        bool darkPresence = industrial && d < 22.0f && Level2IsDark(pp.x, pp.y);
        auto alert = [&] { e.st = S_CHASE; e.lose = 0; Sfx(SFX_HOUND, Vol(d), Pan(pl, e.pos)); };
        if (darkPresence && e.st != S_CHASE) alert();
        switch (e.st) {
        case S_IDLE:
            e.timer -= dt;
            if (e.timer <= 0) { e.target = HoundRoomPoint(); e.st = S_PATROL; }
            if (d < hear || eye) alert();
            break;
        case S_PATROL:
            Follow(e, e.target, gHoundRage ? 3.6f : 2.4f, dt);
            if (Dist(e.pos, e.target) < 1.2f) { e.st = S_IDLE; e.timer = Frand(1, 3); }
            if (d < hear || eye) alert();
            break;
        case S_CHASE: {
            if (d <= HOUND_MELEE_RANGE) {
                Face(e, pp);
                if (e.atkCd <= 0.0f) {
                    e.atkCd = 0.65f * (gHoundRage ? 0.5f : 1.0f);
                    StartHoundAction(e, HOUND_ACTION_MELEE);
                } else {
                    FollowHound(e, pp, gHoundRage ? 7.2f : 4.8f, dt);
                }
            } else if (e.textbookHeld &&
                       d >= HOUND_THROW_MIN_RANGE &&
                       d <= HOUND_THROW_MAX_RANGE &&
                       e.throwCd <= 0.0f && LineClear(e.pos, pp)) {
                Face(e, pp);
                StartHoundAction(e, HOUND_ACTION_THROW);
            } else {
                FollowHound(e, pp, gHoundRage ? 7.2f : 4.8f, dt);
            }

            // Если игрок потерян, возвращаемся в комнату.
            if (!eye && (pl.crouching || d > hear * 1.2f + 6)) {
                e.lose += dt;
                if (e.lose > (industrial ? 9.0f : 5.0f)) {
                    e.st = S_PATROL;
                    e.target = HoundRoomPoint();
                    e.lose = 0;
                }
            } else {
                e.lose = 0;
            }
            break;
        }
        case S_ATTACK:
            // Ближняя атака разрешена только в пределах заданного радиуса.
            if (d > HOUND_MELEE_RANGE) {
                e.st = S_CHASE;
            } else if (e.atkCd <= 0.0f) {
                Face(e, pp);
                e.atkCd = 0.65f * (gHoundRage ? 0.5f : 1.0f);
                StartHoundAction(e, HOUND_ACTION_MELEE);
            }
            break;
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

void EntitiesSetHoundRage(bool enabled) {
    const bool activate = enabled && !gHoundRage;
    gHoundRage=enabled;
    if (activate) {
        for (Entity& e : gE) if (e.type==E_HOUND) {
            e.throwCd*=0.5f;
            e.atkCd*=0.5f;
        }
    }
}

void EntitiesReset() {
    gE.clear();
    gFlying.clear();
    gSpawnT = 25.0f;

    // Единственная Гончая уровня 0: стоит в центре комнаты с аптечками.
    // Клетка (6,2), центр клетки в мировых координатах (13,5).
    Entity h{};
    h.type = E_HOUND;
    h.st = S_IDLE;
    h.pos = Level2IsActive() ? Level2PatrolPoint(0) : Vector2{13.0f, 5.0f};
    h.lastPos = h.pos;
    h.stuckT = 0.0f;
    h.wp = h.pos;
    h.target = h.pos;
    h.yaw = 0.0f;
    h.timer = 0.0f;
    h.life = 1000000.0f;
    h.persistent = true;
    h.textbookHeld = true;
    h.textbookIndex = RandomLoadedTextbookIndex();
    h.throwCd = Frand(3.8f,5.4f) * (gHoundRage ? 0.5f : 1.0f);
    h.timer = Level2IsActive() ? Frand(3.0f,6.0f) : 0.0f;
    gE.push_back(h);
}

void EntitiesThrowBottle(Player& pl) {
    if (pl.emptyBottles <= 0) return;
    pl.emptyBottles--;
    Vector3 f = Forward(pl);
    Vector3 start = EyePos(pl);
    start.y -= 0.12f;
    gFlying.push_back({start, {f.x*10.0f, f.y*10.0f+2.1f, f.z*10.0f}, 3.0f, true, -1});
    Sfx(SFX_PICK, 0.65f);
}

void EntitiesUpdate(Player& pl, float dt, float insane, bool otherEntitiesEnabled) {
    // При отключении остальных сущностей удаляем их сразу; Физичка и её снаряды остаются.
    if (!otherEntitiesEnabled) {
        for (size_t i = 0; i < gE.size();) {
            if (gE[i].type != E_HOUND) gE.erase(gE.begin() + i);
            else ++i;
        }
        gSpawnT = 25.0f;
    }

    // Физика бутылок и учебников. Учебник после попадания/удара о стену
    // не исчезает: он падает на пол и остаётся там до подбора Физичкой.
    for (size_t i = 0; i < gFlying.size();) {
        FlyingItem& q = gFlying[i];
        if (q.bottle) {
            q.life -= dt;
            q.v.y -= 9.81f * dt;
            q.p.x += q.v.x * dt;
            q.p.y += q.v.y * dt;
            q.p.z += q.v.z * dt;
            bool remove = q.life <= 0.0f || q.p.y < 0.12f || CircleHits(q.p.x, q.p.z, 0.12f);
            if (q.p.y > 0.35f) {
                for (Entity& e : gE) {
                    if (e.type == E_HOUND && e.stunT <= 0 &&
                        hypotf(e.pos.x - q.p.x, e.pos.y - q.p.z) < 0.85f && q.p.y < 1.65f) {
                        e.stunT = 15.0f;
                        e.st = S_IDLE;
                        e.lose = 0;
                        remove = true;
                        Say(T("Bottle hit! Tatyana Alexandrovna is stunned.", "Попадание бутылкой! Татьяна Александровна оглушена на 15 секунд."), 3.0f);
                        Sfx(SFX_HIT, 0.9f);
                        Haptic(140);
                        break;
                    }
                }
            }
            if (remove) gFlying.erase(gFlying.begin() + i);
            else ++i;
            continue;
        }

        if (q.settled) { ++i; continue; }
        q.life -= dt;
        q.rotation += q.spin * dt;
        q.v.y -= 9.81f * dt;
        const Vector3 old = q.p;
        const Vector3 next = {old.x + q.v.x * dt, old.y + q.v.y * dt, old.z + q.v.z * dt};
        bool wallHit = false;
        // Несколько подшагов не дают быстрому учебнику пролетать сквозь игрока/стену.
        constexpr int SUBSTEPS = 5;
        for (int step = 1; step <= SUBSTEPS; ++step) {
            const float f = (float)step / (float)SUBSTEPS;
            const Vector3 candidate = {
                old.x + (next.x - old.x) * f,
                old.y + (next.y - old.y) * f,
                old.z + (next.z - old.z) * f
            };
            if (CircleHits(candidate.x, candidate.z, 0.055f)) {
                wallHit = true;
                break;
            }
            q.p = candidate;
            if (!q.hitPlayer && candidate.y >= 0.35f && candidate.y <= 1.90f &&
                hypotf(pl.pos.x - candidate.x, pl.pos.z - candidate.z) <= 0.48f) {
                q.hitPlayer = true;
                Hurt(pl, 20.0f, 2.5f);   // гарантированный урон один раз за попадание
                q.v.x *= 0.22f;
                q.v.z *= 0.22f;
                q.v.y = fminf(q.v.y, -2.8f);
            }
        }
        if (wallHit) {
            q.v.x = 0.0f;
            q.v.z = 0.0f;
            q.v.y = fminf(q.v.y, 0.0f);
        }

        if (q.p.y <= 0.035f) {
            q.p.y = 0.035f;
            if (q.bounces == 0 && fabsf(q.v.y) > 1.6f && !q.hitPlayer && !wallHit) {
                q.v.y = fabsf(q.v.y) * 0.18f;
                q.v.x *= 0.52f;
                q.v.z *= 0.52f;
                q.bounces++;
            } else {
                SettleTextbook(q);
            }
        } else if (q.life <= 0.0f) {
            SettleTextbook(q);
        }
        ++i;
    }
    if (otherEntitiesEnabled) {
        gSpawnT -= dt * (1.0f + insane * 1.5f + (pl.sanity <= 0 ? 2.0f : 0.0f));
        if (gSpawnT <= 0) { gSpawnT = Frand(10, 20); TrySpawn(pl); }
    }
    Vector2 pp = {pl.pos.x, pl.pos.z};
    for (size_t i = 0; i < gE.size();) {
        Entity& e = gE[i];
        float d = Dist(e.pos, pp);
        if (!e.persistent) e.life -= dt * (d > 42 ? 4.0f : 1.0f);
        if (d <= 42 || (e.type == E_HOUND && e.persistent))
            Think(e, pl, dt);
        if (!e.persistent && (e.life <= 0 || d > 55)) gE.erase(gE.begin() + i); else i++;
    }
}

// ---------- отрисовка ----------
static Color C(int r, int g, int b, float L) { L = L < 0 ? 0 : L > 1 ? 1 : L; return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)(L * 255)}; }
static void Beg(const Entity& e) {
    const float floorY = Level2IsActive() ? Level2FloorAt(e.pos.x, e.pos.y) : 0.0f;
    rlPushMatrix();
    rlTranslatef(e.pos.x - gOrg.x, floorY, e.pos.y - gOrg.z);
    rlRotatef(e.yaw * RAD2DEG, 0, 1, 0);
}

void EntitiesDraw(const Player& pl, bool glow, Shader worldShader, bool otherEntitiesEnabled) {
    LoadEntityTextures();
    Vector2 pp = {pl.pos.x, pl.pos.z};
    for (const Entity& e : gE) {
        if (!otherEntitiesEnabled && e.type != E_HOUND) continue;
        if (Dist(e.pos, pp) > 34) continue;
        float L = LightAt(e.pos.x, e.pos.y);

        // Новая модель Hound. Если GLB не загрузился,
        // ниже автоматически останется старая модель.
        if (e.type == E_HOUND && LoadHoundModel()) {
            if (!glow) {
                UpdateHoundAnimation(e);

                const float entityFloorY = Level2IsActive() ? Level2FloorAt(e.pos.x, e.pos.y) : 0.0f;
                Vector3 modelPos = {
                    e.pos.x - gOrg.x,
                    entityFloorY,
                    e.pos.y - gOrg.z
                };

                DrawModelEx(
                    gHoundModel,
                    modelPos,
                    {0.0f, 1.0f, 0.0f},
                    e.yaw * RAD2DEG,
                    {0.62f, 0.62f, 0.62f},
                    WHITE
                );

                // Пока учебник находится у Физички, рисуем его у правой руки.
                // Масштаб учебника уменьшен в 2,5 раза относительно прежнего.
                if (e.textbookHeld) {
                    Model* textbookModel = GetTextbookModel(e.textbookIndex);
                    if (textbookModel) {
                        const float rightX = cosf(e.yaw);
                        const float rightZ = -sinf(e.yaw);
                        const float frontX = sinf(e.yaw);
                        const float frontZ = cosf(e.yaw);
                        Vector3 handPos = {
                            e.pos.x - rightX * 0.31f + frontX * 0.34f - gOrg.x,
                            entityFloorY + 1.12f,
                            e.pos.y - rightZ * 0.31f + frontZ * 0.34f - gOrg.z
                        };
                        DrawModelEx(*textbookModel, handPos, {0.0f, 1.0f, 0.0f},
                                    e.yaw * RAD2DEG, {TEXTBOOK_HELD_SCALE, TEXTBOOK_HELD_SCALE, TEXTBOOK_HELD_SCALE}, WHITE);
                    }
                }

                // DrawMesh внутри DrawModelEx напрямую переключает
                // OpenGL shader. Синхронизируем состояние RLGL.
                rlSetShader(0, nullptr);
                rlSetShader(worldShader.id, worldShader.locs);
            } else {
                rlPushMatrix();
                rlTranslatef(
                    e.pos.x - gOrg.x,
                    0.0f,
                    e.pos.y - gOrg.z
                );
                rlRotatef(e.yaw * RAD2DEG, 0, 1, 0);

                rlPopMatrix();
            }

            continue;
        }

        Beg(e);
        switch (e.type) {
        case E_HOUND:
            if (!glow) {
                float sw = e.stunT > 0 ? 0.015f : sinf(e.t * 10.5f) * (e.st == S_CHASE ? 0.19f : 0.08f);
                Color skin = C(218, 184, 164, L), pink = C(190, 132, 137, L), hair = C(225, 218, 204, L);
                // Низкая четвероногая поза: корпус, плечи, таз и четыре анимируемые конечности.
                DrawCube({0, 0.57f, -0.02f}, 0.58f, 0.48f, 1.02f, pink);
                DrawCube({0, 0.78f, 0.48f}, 0.43f, 0.36f, 0.42f, skin);
                DrawCube({0, 1.08f, 0.62f}, 0.39f, 0.40f, 0.38f, skin);
                DrawCube({0, 1.27f, 0.59f}, 0.43f, 0.20f, 0.39f, hair);
                DrawCube({-0.19f, 1.08f, 0.79f}, 0.055f, 0.055f, 0.025f, C(45,55,65,L));
                DrawCube({0.19f, 1.08f, 0.79f}, 0.055f, 0.055f, 0.025f, C(45,55,65,L));
                DrawCube({0, 0.98f, 0.80f}, 0.10f, 0.025f, 0.025f, C(130,75,75,L));
                for (int side=-1; side<=1; side+=2) {
                    float x=side*0.23f;
                    DrawCube({x,0.36f,0.40f+sw},0.16f,0.48f,0.17f,skin);
                    DrawCube({x,0.13f,0.57f+sw},0.17f,0.12f,0.30f,skin);
                    DrawCube({x,0.35f,-0.40f-sw},0.17f,0.50f,0.18f,skin);
                    DrawCube({x,0.13f,-0.57f-sw},0.17f,0.12f,0.30f,skin);
                }
                if (gWomanFace.id) {
                    rlSetTexture(gWomanFace.id); rlBegin(RL_QUADS); rlColor4ub(255,255,255,(unsigned char)(L*255));
                    rlTexCoord2f(0,1); rlVertex3f(-0.19f,0.91f,0.805f); rlTexCoord2f(1,1); rlVertex3f(0.19f,0.91f,0.805f);
                    rlTexCoord2f(1,0); rlVertex3f(0.19f,1.23f,0.805f); rlTexCoord2f(0,0); rlVertex3f(-0.19f,1.23f,0.805f); rlEnd(); rlSetTexture(0);
                }
            } else {
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
    if (!glow) {
        for (const FlyingItem& q : gFlying) {
            if (q.bottle) {
                DrawCube({q.p.x - gOrg.x, q.p.y, q.p.z - gOrg.z},
                         0.13f, 0.30f, 0.13f, Color{165, 205, 220, 220});
                continue;
            }
            Model* textbookModel = GetTextbookModel(q.textbook);
            Vector3 pos = {q.p.x - gOrg.x, q.p.y, q.p.z - gOrg.z};
            if (textbookModel) {
                DrawModelEx(*textbookModel, pos, q.rotationAxis, q.rotation,
                            {TEXTBOOK_FLY_SCALE, TEXTBOOK_FLY_SCALE, TEXTBOOK_FLY_SCALE}, WHITE);
                rlSetShader(0, nullptr);
                rlSetShader(worldShader.id, worldShader.locs);
            } else {
                DrawCube(pos, 0.16f, 0.22f, 0.02f, Color{55, 65, 105, 255});
            }
        }
    }
}


void EntitiesUnload() {
    if (gHoundAnimations) {
        UnloadModelAnimations(gHoundAnimations, gHoundAnimCount);
        gHoundAnimations = nullptr;
        gHoundAnimCount = 0;
    }
    if (gHoundThrowAnimations) {
        UnloadModelAnimations(gHoundThrowAnimations, gHoundThrowAnimCount);
        gHoundThrowAnimations = nullptr;
        gHoundThrowAnimCount = 0;
    }
    gHoundThrowAnimation = nullptr;

    if (gHoundModel.meshCount > 0) {
        UnloadModel(gHoundModel);
        gHoundModel = {};
    }

    if (gWomanFace.id > 0) UnloadTexture(gWomanFace);
    gWomanFace = {};

    for (int i = 0; i < 4; i++) {
        if (gTextbookModelLoaded[i] && gTextbookModels[i].meshCount > 0)
            UnloadModel(gTextbookModels[i]);
        gTextbookModels[i] = {};
        gTextbookModelLoaded[i] = false;
    }

    gTextbookLoadAttempted = false;
    gHoundThrow = -1;
    gHoundThrowDuration = 0.90f;
    gTexturesLoaded = false;
    gHoundLoadAttempted = false;
    gHoundIdle = -1;
    gHoundWalk = -1;
    gHoundRun = -1;
}
