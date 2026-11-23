// Офлайн-проверки Level 2 (логика, коллизии, ловушка, выход, ориентация текста/стрелок/смайлика, проходимость).
// НЕ заменяет сборку Android и тест на телефоне. Сборка: build_and_run.sh
#include "level2.h"
#include "level2_layout.h"
#include "level2_geom.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <vector>
#include <queue>
#include <string>
#include <cmath>

extern void StubDumpTexture(unsigned id, const char* path);
extern int gTrapReleaseCalls; extern bool gTrapReleasePlace; extern float gTrapRelX, gTrapRelZ;
const l2g::Scene& Level2TestScene(); const l2g::Scene& Level2TestDoors();
void Level2TestRecord(bool on); unsigned Level2TestTextureId(int slot);
void Level2TestSetDoorAngles(float trap, float lure, float exitDoor);
int Level2TestTrapPhase(); bool Level2TestTrapLights(); float Level2TestDoorAngle(int d);

using namespace l2; using namespace l2g;
static int gFail = 0, gPass = 0;
#define CHECK(cond, ...) do { if (cond) { ++gPass; } else { ++gFail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const float PI_F = 3.14159265f;
static Player MakePlayer(float x, float z, float yaw) { Player p{}; p.pos = {x, Level2FloorAt(x, z), z}; p.yaw = yaw; return p; }
static bool Run(Player& p, float seconds) {          // шаг игры; true, если уровень сообщил о завершении
    bool done = false; for (float t = 0; t < seconds; t += 1.0f / 60.0f) if (Level2Update(p, 1.0f / 60.0f)) done = true; return done;
}

// Заливка по сетке 0.25 м теми же правилами движения, что и в игре (перепад пола <= 0.24 м).
struct Flood {
    static constexpr float STEP = 0.25f; int W, H; std::vector<char> seen; std::vector<float> fl;
    Flood() : W((int)(WORLD_W / STEP)), H((int)(WORLD_D / STEP)), seen((size_t)W * H, 0), fl((size_t)W * H, 0) {}
    void Run(float sx, float sz, float r) {
        std::fill(seen.begin(), seen.end(), 0);
        std::queue<std::pair<int, int>> q; int i0 = (int)(sx / STEP), j0 = (int)(sz / STEP);
        seen[(size_t)j0 * W + i0] = 1; fl[(size_t)j0 * W + i0] = Level2FloorAt(sx, sz); q.push({i0, j0});
        const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (!q.empty()) {
            auto [i, j] = q.front(); q.pop();
            for (auto& dd : d) {
                int ni = i + dd[0], nj = j + dd[1];
                if (ni < 0 || nj < 0 || ni >= W || nj >= H || seen[(size_t)nj * W + ni]) continue;
                float nf; if (!Level2CanMoveTo((ni + 0.5f) * STEP, (nj + 0.5f) * STEP, r, fl[(size_t)j * W + i], &nf)) continue;
                seen[(size_t)nj * W + ni] = 1; fl[(size_t)nj * W + ni] = nf; q.push({ni, nj});
            }
        }
    }
    bool Reached(float x, float z) const { int i = (int)(x / STEP), j = (int)(z / STEP); return seen[(size_t)j * W + i] != 0; }
};

static void DumpScene(const char* dir, const char* name, const Scene& sc) {
    std::string path = std::string(dir) + "/" + name; FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return;
    int n = 0; long pos = std::ftell(f); std::fwrite(&n, 4, 1, f);
    for (int iz = 0; iz < sc.TilesZ(); ++iz) for (int ix = 0; ix < sc.TilesX(); ++ix) for (int t = 0; t < TEX_COUNT; ++t) {
        const auto& q = sc.TileAt(ix, iz).b[t].quads;
        for (size_t i = 0; i + 3 < q.size(); i += 4) { int tt = t; std::fwrite(&tt, 4, 1, f); std::fwrite(&q[i], sizeof(Vtx), 4, f); ++n; }
    }
    std::fseek(f, pos, SEEK_SET); std::fwrite(&n, 4, 1, f); std::fclose(f);
}
static void QuadFrame(const QuadRecord& r, V3* n, V3* right, V3* center) {
    *n = Norm(Cross(r.p[1] - r.p[0], r.p[2] - r.p[1]));
    *center = (r.p[0] + r.p[1] + r.p[2] + r.p[3]) * 0.25f;
    *right = Norm(Cross(*n * -1.0f, V3{0, 1, 0}));          // правая рука камеры raylib: forward x up, наблюдатель смотрит в квад
}
static void FreshLevel() { Level2Reset(); Level2SetActive(true); Level2Reset(); gTrapReleaseCalls = 0; }

int main(int argc, char** argv) {
    const char* dumpDir = (argc > 2 && !std::strcmp(argv[1], "--dump")) ? argv[2] : nullptr;
    const DoorDef& TRAP = kDoors[DOOR_TRAP]; const DoorDef& LURE = kDoors[DOOR_LURE]; const DoorDef& EXIT = kDoors[DOOR_EXIT];
    std::printf("== Level 2 offline check ==\n");
    Level2TestRecord(true);
    auto t0 = std::chrono::steady_clock::now();
    FreshLevel();
    double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const Scene& sc = Level2TestScene();
    std::printf("build: %.0f ms (desktop), static quads: %zu, map %dx%d cells\n", buildMs, sc.QuadCount(), MAP_W, MAP_H);

    std::printf("\n[map] '#'=wall '.'=open 'o'=dark zone\n");
    for (int z = 0; z < MAP_H; ++z) { for (int x = 0; x < MAP_W; ++x) std::putchar(Level2IsSolid(x, z) ? '#' : (Level2IsDark((x + 0.5f) * CELL, (z + 0.5f) * CELL) ? 'o' : '.')); std::putchar('\n'); }

    std::printf("\n[items / hide spots]\n");
    for (const Item& it : Level2GetItems()) { const float x = (it.cx + 0.5f) * CELL, z = (it.cz + 0.5f) * CELL; CHECK(!Level2CircleHits(x, z, 0.3f), "item (%d,%d) inside geometry", it.cx, it.cz); }
    for (int i = 0; i < Level2PatrolCount(); ++i) { Vector2 p = Level2PatrolPoint(i); CHECK(!Level2CircleHits(p.x, p.y, 0.35f), "hide spot %d inside geometry", i); CHECK(Level2IsDark(p.x, p.y), "hide spot %d not dark", i); }

    std::printf("\n[reachability: start -> everything, trap door closed]\n");
    Flood fl; fl.Run(START_X, START_Z, 0.3f);
    CHECK(fl.Reached(EXIT.cx, EXIT.z + 1.4f), "REAL EXIT unreachable from the start (blocked by geometry)");
    CHECK(fl.Reached(EXIT.cx - 1.5f, EXIT.z + 1.4f) && fl.Reached(EXIT.cx + 1.5f, EXIT.z + 1.4f), "exit room is not fully reachable");
    CHECK(!Level2CircleHits(EXIT.cx, EXIT.z + 1.2f, 0.3f), "player cannot stand in front of the exit door");
    for (const Item& it : Level2GetItems()) {
        const float x = (it.cx + 0.5f) * CELL, z = (it.cz + 0.5f) * CELL; const bool inPassage = x > 88 && x < 90 && z < 38;
        CHECK(fl.Reached(x, z) != inPassage, "item (%d,%d) reach=%d expected=%d", it.cx, it.cz, fl.Reached(x, z), !inPassage);
    }
    for (int i = 0; i < Level2PatrolCount(); ++i) { Vector2 p = Level2PatrolPoint(i); CHECK(fl.Reached(p.x, p.y), "hide spot %d unreachable", i); }
    CHECK(fl.Reached(TRAP.cx, TRAP.z + 1.1f), "niche in front of the trap door unreachable");
    CHECK(!fl.Reached(TRAP.cx, TRAP.z - 2.0f), "stairwell reachable while the trap door is closed");
    {   // запас по ширине: до какого радиуса игрока проходимы ВСЕ узкие места на пути старт -> выход и старт -> ловушка
        float best = 0.3f;
        for (float r = 0.34f; r <= 0.60f; r += 0.02f) { Flood t; t.Run(START_X, START_Z, r); if (t.Reached(EXIT.cx, EXIT.z + 1.4f) && t.Reached(TRAP.cx, TRAP.z + 1.1f)) best = r; else break; }
        std::printf("  start->exit and start->trap stay passable up to player radius %.2f m (game uses 0.30 m)\n", best);
        CHECK(best >= 0.38f, "narrow places leave too little margin (%.2f m)", best);
    }
    int open = 0, reached = 0;
    for (int z = 0; z < MAP_H; ++z) for (int x = 0; x < MAP_W; ++x) if (!Level2IsSolid(x, z)) { const float cx = (x + 0.5f) * CELL, cz = (z + 0.5f) * CELL; if (cx > 88 && cx < 90 && cz < 38) continue; ++open; if (fl.Reached(cx, cz)) ++reached; }
    std::printf("  open cells reachable (excluding stairwell): %d / %d (rest are cells occupied by equipment/crates)\n", reached, open);

    std::printf("\n[real exit]\n");
    Player farEx = MakePlayer(EXIT.cx, EXIT.z + 5.0f, PI_F);
    CHECK(!Level2Interact(farEx), "exit used from far away");
    Player backEx = MakePlayer(EXIT.cx, EXIT.z + 1.2f, 0.0f);
    CHECK(!Level2Interact(backEx), "exit used while facing away");
    Player exitP = MakePlayer(EXIT.cx, EXIT.z + 1.2f, PI_F);
    CHECK(!Run(exitP, 8.0f), "level ended by itself near the exit (no button pressed)");
    CHECK(Level2Interact(exitP), "THROW next to the real exit did nothing");
    int fires = 0; for (float t = 0; t < 3.0f; t += 1.0f / 60.0f) if (Level2Update(exitP, 1.0f / 60.0f)) ++fires;
    CHECK(fires == 1, "level completion fired %d times (expected exactly 1)", fires);
    CHECK(!Level2Interact(exitP), "exit door reacts again after completion");

    std::printf("\n[trap door: interaction]\n");
    FreshLevel();
    Player far = MakePlayer(TRAP.cx, TRAP.z + 7.0f, PI_F);
    CHECK(!Level2Interact(far), "far player triggered the trap door");
    Player away = MakePlayer(TRAP.cx, TRAP.z + 1.2f, 0.0f);
    CHECK(!Level2Interact(away), "player facing away triggered the trap door");
    Player aside = MakePlayer(TRAP.cx, TRAP.z + 1.2f, PI_F * 0.5f);
    CHECK(!Level2Interact(aside), "player looking sideways triggered the trap door");
    Player near = MakePlayer(TRAP.cx, TRAP.z + 1.2f, PI_F);
    CHECK(Level2Interact(near), "near+facing player could not open the trap door");
    CHECK(Level2TestTrapPhase() == 1, "trap should be in OPEN phase after the first press");
    CHECK(!Run(near, 1.5f), "emergency door ended the level (must never)");
    CHECK(Level2TestTrapPhase() == 1 && !Level2TestTrapLights(), "trap fired before the player entered");
    Flood fo; fo.Run(START_X, START_Z, 0.3f);
    CHECK(fo.Reached(TRAP.cx, PASSAGE_Z_END + 1.5f), "stairwell top unreachable while the door is open");
    CHECK(fo.Reached(PASSAGE_CX, 27.0f), "item/lure position on the top landing unreachable");

    std::printf("\n[stairwell: identical tall steps]\n");
    {
        // ступени: одинаковые подъёмы 0.18 м, глубина 0.30 м; идём вперёд/назад/боком
        for (float lane : {88.8f, 89.0f, 89.2f}) {
            float floorY = Level2FloorAt(lane, 37.0f); bool ok = true; float maxStep = 0, minStep = 9;
            int risers = 0;
            for (float z = 37.0f; z > PASSAGE_Z_END + 1.0f; z -= 0.01f) { float nf; if (!Level2CanMoveTo(lane, z, 0.3f, floorY, &nf)) { ok = false; std::printf("  blocked going north z=%.2f lane=%.1f\n", z, lane); break; } const float d = nf - floorY; if (d > 0.001f) { ++risers; maxStep = std::max(maxStep, d); minStep = std::min(minStep, d); } floorY = nf; }
            CHECK(ok, "forward walk blocked on lane %.1f", lane);
            for (float z = PASSAGE_Z_END + 1.0f; z < 37.0f && ok; z += 0.01f) { float nf; if (!Level2CanMoveTo(lane, z, 0.3f, floorY, &nf)) { ok = false; std::printf("  blocked going south z=%.2f lane=%.1f\n", z, lane); } else floorY = nf; }
            CHECK(ok, "backward walk blocked on lane %.1f", lane);
            if (lane == 89.0f) {
                std::printf("  steps: %d risers, each %.3f..%.3f m (tread %.2f m), top floor %.2f m\n", risers, minStep, maxStep, STAIR_RUN, Level2FloorAt(89.0f, 25.0f));
                CHECK(risers == 2 * STAIR_STEPS, "expected %d risers, got %d", 2 * STAIR_STEPS, risers);
                CHECK(fabsf(maxStep - STAIR_RISE) < 0.002f && fabsf(minStep - STAIR_RISE) < 0.002f, "steps are not identical (%.3f..%.3f)", minStep, maxStep);
                CHECK(STAIR_RISE >= 0.15f && STAIR_RISE <= 0.18f && STAIR_RUN >= 0.29f, "step size outside the apartment-block norm");
            }
        }
        bool ok = true;   // боком поперёк каждой ступени
        for (float z : {34.9f, 34.5f, 33.7f, 33.1f, 30.1f, 29.7f, 28.3f}) { float f0 = Level2FloorAt(89.0f, z); for (float x = 88.8f; x < 89.2f; x += 0.01f) { float nf; if (!Level2CanMoveTo(x, z, 0.3f, f0, &nf)) { ok = false; break; } f0 = nf; } }
        CHECK(ok, "strafing across the steps is blocked");
        // запас над головой: потолок >= пол + 2.2 м вдоль всего прохода
        bool head = true; for (float z = 25.0f; z < 37.0f; z += 0.05f) { int cz = (int)floorf(z / CELL); float ceil = 0; for (const CeilZone& c : kCeil) if (cz >= c.r.z0 && cz <= c.r.z1 && 44 >= c.r.x0 && 44 <= c.r.x1) ceil = c.h; if (ceil - Level2FloorAt(89.0f, z) < 2.2f) head = false; }
        CHECK(head, "headroom in the stairwell is below 2.2 m");
    }

    std::printf("\n[trap door: slam, lights, hound]\n");
    {
        Player in = MakePlayer(PASSAGE_CX, 36.4f, PI_F);                // зашёл в лестничную клетку
        const float lightBefore = Level2LightAt(PASSAGE_CX, 33.0f);
        bool ended = Run(in, 0.3f);
        CHECK(Level2TestTrapPhase() == 2, "trap did not slam when the player entered the stairwell");
        CHECK(Level2TestTrapLights(), "lighting did not change");
        CHECK(Level2TestDoorAngle(DOOR_TRAP) < 0.01f, "door did not close (angle %.2f)", Level2TestDoorAngle(DOOR_TRAP));
        const float lightAfter = Level2LightAt(PASSAGE_CX, 33.0f);
        std::printf("  light in the stairwell: %.2f -> %.2f; passage dark now: %d\n", lightBefore, lightAfter, Level2IsDark(PASSAGE_CX, 33.0f));
        CHECK(lightAfter < lightBefore * 0.5f && Level2IsDark(PASSAGE_CX, 33.0f), "stairwell light did not drop");
        float lp[4], lc[3], lb[4]; Level2GetLocalLight(lp, lc, lb);
        CHECK(lc[0] > lc[2], "local lamp did not turn red");
        Flood jam; jam.Run(PASSAGE_CX, 33.0f, 0.3f);
        CHECK(!jam.Reached(TRAP.cx, TRAP.z + 1.1f), "player can walk through the slammed door");
        CHECK(gTrapReleaseCalls == 0, "hound released too early");
        ended |= Run(in, HOUND_RELEASE_DELAY);
        CHECK(gTrapReleaseCalls == 1 && gTrapReleasePlace, "trap did not release the hound (calls=%d)", gTrapReleaseCalls);
        CHECK(Level2IsSolid(44, 13) == false && !Level2CircleHits(gTrapRelX, gTrapRelZ, 0.35f), "hound release point is inside geometry");
        Player inJam = MakePlayer(TRAP.cx, TRAP.z - 1.0f, 0.0f);   // изнутри лицом к двери
        CHECK(Level2Interact(inJam) && Level2TestDoorAngle(DOOR_TRAP) < 0.01f, "jammed door should refuse (but consume the press)");
        ended |= Run(in, TRAP_JAM_TIME);
        CHECK(Level2TestTrapPhase() == 3, "trap did not finish");
        Player inside = MakePlayer(TRAP.cx, TRAP.z - 1.0f, 0.0f);           // изнутри лицом к двери (на юг)
        CHECK(Level2Interact(inside), "door cannot be reopened from inside after the jam");
        ended |= Run(inside, 1.2f);
        CHECK(Level2TestDoorAngle(DOOR_TRAP) > 1.5f, "door did not reopen");
        Flood esc; esc.Run(PASSAGE_CX, 33.0f, 0.3f);
        CHECK(esc.Reached(TRAP.cx, TRAP.z + 1.1f) && esc.Reached(START_X, START_Z), "player cannot escape the stairwell after the trap");
        CHECK(!Run(in, 6.0f) && !ended, "trap door ended the level (must never)");
        Player lure = MakePlayer(LURE.cx, LURE.z + 1.2f, PI_F);
        CHECK(Level2Interact(lure), "lure door ignored THROW");
        CHECK(!Run(lure, 5.0f), "lure door ended the level (must never)");
        CHECK(Level2TestTrapPhase() == 3, "trap re-armed");
    }
    {   // игрок остался снаружи: дверь захлопывается по таймауту, Физичка только активируется
        FreshLevel();
        Player out = MakePlayer(TRAP.cx, TRAP.z + 1.2f, PI_F);
        CHECK(Level2Interact(out), "cannot open the door (timeout test)");
        Run(out, SLAM_TIMEOUT + 0.3f);
        CHECK(Level2TestTrapPhase() == 2 && Level2TestTrapLights(), "door did not slam by timeout");
        Run(out, HOUND_RELEASE_DELAY + 0.2f);
        CHECK(gTrapReleaseCalls == 1 && !gTrapReleasePlace, "outside trap should only activate the hound (calls=%d place=%d)", gTrapReleaseCalls, gTrapReleasePlace);
    }
    FreshLevel();

    std::printf("\n[text / arrow / smiley orientation]\n");
    int arrowsSeen[32] = {0}; int smileys = 0, bloods = 0;
    for (const QuadRecord& r : sc.Records()) {
        if (r.tex != TEX_ATLAS) continue;
        V3 n, right, c; QuadFrame(r, &n, &right, &c);
        CHECK(fabsf(n.y) < 1e-3f, "atlas quad not vertical (tag %d)", r.tag);
        CHECK(Dot(r.p[3] - r.p[0], right) > 0.0f && r.u[3] > r.u[0], "tag %d: u does not grow to the viewer's right (mirrored)", r.tag);
        CHECK(r.v[1] > r.v[0] && r.p[1].y < r.p[0].y, "tag %d: v does not grow downward (upside down)", r.tag);
        const V3 inside = c + n * 0.5f;
        CHECK(!Level2IsSolid((int)floorf(inside.x / CELL), (int)floorf(inside.z / CELL)), "tag %d: face points into the wall", r.tag);
        CHECK(c.y > 1.0f && c.y < 2.0f && r.p[0].y <= 2.51f && r.p[1].y >= 0.89f, "tag %d: height %.2f..%.2f not at eye level", r.tag, r.p[1].y, r.p[0].y);
        if (r.tag == 1001) { ++smileys; CHECK(c.x > TRAP.cx && c.x < TRAP.cx + 4.0f && fabsf(c.z - 20 * CELL) < 0.05f, "smiley is not next to the trap door (%.1f, %.1f)", c.x, c.z); }
        if (r.tag == 1000) ++bloods;
        if (r.tag >= 0 && r.tag < (int)(sizeof(kArrows) / sizeof(kArrows[0]))) {
            float u0, v0, u1, v1; int slot = -1;
            for (int s = 0; s < ATLAS_SLOTS; ++s) { AtlasSlotRect(s, &u0, &v0, &u1, &v1); if (fabsf(u0 - r.u[0]) < 1e-5f && fabsf(v0 - r.v[0]) < 1e-5f) slot = s; }
            CHECK(slot >= 0 && slot <= ATLAS_ARROW_L2, "arrow tag %d uses non-arrow atlas slot %d", r.tag, slot);
            const V3 dir = right * (slot < ATLAS_ARROW_L0 ? 1.0f : -1.0f);
            const ArrowDef& a = kArrows[r.tag];
            CHECK(dir.x * a.dirX + dir.z * a.dirZ > 0.99f, "arrow %d points (%.2f,%.2f) instead of (%d,%d)", r.tag, dir.x, dir.z, a.dirX, a.dirZ);
            ++arrowsSeen[r.tag];
        }
    }
    const size_t nArrows = sizeof(kArrows) / sizeof(kArrows[0]);
    for (size_t i = 0; i < nArrows; ++i) CHECK(arrowsSeen[i] == 1, "arrow %zu geometry count = %d", i, arrowsSeen[i]);
    CHECK(smileys == 1 && bloods == 1, "smileys=%d bloods=%d", smileys, bloods);
    {   // стрелки образуют цепочку: расстояние от каждой до ловушки убывает вдоль маршрута (по порядку в списке)
        std::printf("  %zu arrows lead from the start to the trap door\n", nArrows);
        CHECK(nArrows >= 10, "too few arrows");
    }
    for (float ang : {0.0f, 0.6f, 1.2f, 1.66f}) {
        Level2TestSetDoorAngles(ang, ang, ang);
        const Scene& ds = Level2TestDoors(); int found = 0;
        for (const QuadRecord& r : ds.Records()) {
            if (r.tag < 100 || r.tag > 103) continue; ++found;
            V3 n, right, c; QuadFrame(r, &n, &right, &c);
            CHECK(Dot(r.p[3] - r.p[0], right) > 0.0f && r.u[3] > r.u[0], "sign %d at angle %.2f is mirrored", r.tag, ang);
            CHECK(r.v[1] > r.v[0] && r.p[1].y < r.p[0].y, "sign %d at angle %.2f is upside down", r.tag, ang);
            CHECK(fabsf(n.y) < 1e-3f && fabsf(Dot(r.p[2] - r.p[0], n)) < 1e-4f, "sign %d not vertical/planar", r.tag);
            const float hz = r.tag <= 101 ? TRAP.z : (r.tag == 102 ? LURE.z : EXIT.z);
            if (ang == 0.0f) CHECK(n.z > 0.999f && c.z > hz, "sign %d does not face the player side (n.z=%.2f)", r.tag, n.z);
        }
        CHECK(found == 4, "found %d door signs at angle %.2f", found, ang);
    }
    Level2TestSetDoorAngles(0, 0, 0);

    std::printf("\n[hound grid]\n");
    {
        static bool seen[MAP_H][MAP_W]; std::memset(seen, 0, sizeof seen);
        std::queue<std::pair<int, int>> q; q.push({4, 21}); seen[21][4] = true;
        while (!q.empty()) { auto [x, z] = q.front(); q.pop(); const int d[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& dd : d) { int nx = x + dd[0], nz = z + dd[1]; if (nx < 0 || nz < 0 || nx >= MAP_W || nz >= MAP_H || seen[nz][nx] || Level2IsSolid(nx, nz)) continue;
                if (Level2CircleHits((nx + 0.5f) * CELL, (nz + 0.5f) * CELL, 0.35f)) continue; seen[nz][nx] = true; q.push({nx, nz}); } }
        for (int i = 0; i < Level2PatrolCount(); ++i) { Vector2 p = Level2PatrolPoint(i); CHECK(seen[(int)(p.y / CELL)][(int)(p.x / CELL)], "hide spot %d not connected on the cell grid", i); }
        CHECK(seen[(int)(EXIT.z / CELL) + 1][(int)(EXIT.cx / CELL)], "exit room not connected on the hound grid");
        int cells = 0, ok = 0; for (int z = 0; z < MAP_H; ++z) for (int x = 0; x < MAP_W; ++x) if (!Level2IsSolid(x, z)) { ++cells; if (seen[z][x]) ++ok; }
        std::printf("  open cells %d, reachable on hound grid %d\n", cells, ok);
    }

    std::printf("\n[light]\n");
    float lp[4], lc[3], lb[4]; CHECK(Level2GetLocalLight(lp, lc, lb), "local light inactive in Level 2");
    std::printf("  blue lamp at (%.1f,%.1f,%.1f) radius %.1f, colour (%.2f,%.2f,%.2f), gate x[%.1f,%.1f] z[%.1f,%.1f]\n", lp[0], lp[1], lp[2], lp[3], lc[0], lc[1], lc[2], lb[0], lb[1], lb[2], lb[3]);
    CHECK(lb[3] < TRAP.z && lc[2] > lc[0], "blue gate leaks outside the passage or the lamp is not blue");
    CHECK(Level2LightAt(PASSAGE_CX, 37.9f) < 0.25f, "emergency lamp from the niche leaks through the closed door");

    if (dumpDir) {
        std::string d(dumpDir);
        Level2TestSetDoorAngles(0, 0, 0);                         DumpScene(dumpDir, "scene.bin", sc); DumpScene(dumpDir, "doors_closed.bin", Level2TestDoors());
        Level2TestSetDoorAngles(DOOR_OPEN_ANGLE, 0, DOOR_OPEN_ANGLE); DumpScene(dumpDir, "doors_open.bin", Level2TestDoors());
        for (int s = 0; s < TEX_COUNT; ++s) StubDumpTexture(Level2TestTextureId(s), (d + "/tex" + std::to_string(s) + ".rgba").c_str());
    }
    Level2SetActive(false);
    std::printf("\nRESULT: %d checks passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
