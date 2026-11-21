// Офлайн-проверки Level 2 (логика, коллизии, ориентация текста/стрелок, проходимость). НЕ заменяет сборку и тест на телефоне.
// Сборка: см. build_and_run.sh
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
const l2g::Scene& Level2TestScene(); const l2g::Scene& Level2TestDoors();
void Level2TestRecord(bool on); unsigned Level2TestTextureId(int slot); void Level2TestSetDoorAngles(float fire, float fin);

using namespace l2; using namespace l2g;
static int gFail = 0, gPass = 0;
#define CHECK(cond, ...) do { if (cond) { ++gPass; } else { ++gFail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Player MakePlayer(float x, float z, float yaw) { Player p{}; p.pos = {x, 0, z}; p.yaw = yaw; return p; }

// Заливка по сетке 0.25 м теми же правилами движения, что и в игре (радиус игрока 0.3 м, перепад пола <= 0.24 м).
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

static bool QuadFrame(const QuadRecord& r, V3* n, V3* right, V3* center) {
    *n = Norm(Cross(r.p[1] - r.p[0], r.p[2] - r.p[1]));
    *center = (r.p[0] + r.p[1] + r.p[2] + r.p[3]) * 0.25f;
    const V3 forward = *n * -1.0f;                      // наблюдатель стоит перед квадом и смотрит в него
    *right = Norm(Cross(forward, V3{0, 1, 0}));          // правая рука камеры raylib: forward x up
    return true;
}

int main(int argc, char** argv) {
    const char* dumpDir = (argc > 2 && !std::strcmp(argv[1], "--dump")) ? argv[2] : nullptr;
    std::printf("== Level 2 offline check ==\n");
    Level2TestRecord(true);
    auto t0 = std::chrono::steady_clock::now();
    Level2Reset(); Level2SetActive(true);
    double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const Scene& sc = Level2TestScene();
    std::printf("build: %.0f ms (desktop), static quads: %zu\n", buildMs, sc.QuadCount());

    // --- карта
    std::printf("\n[map] '#'=wall '.'=open 'o'=dark zone (x: 0..63, z: 0..43)\n");
    for (int z = 0; z < MAP_H; ++z) { for (int x = 0; x < MAP_W; ++x) std::putchar(Level2IsSolid(x, z) ? '#' : (Level2IsDark((x + 0.5f) * CELL, (z + 0.5f) * CELL) ? 'o' : '.')); std::putchar('\n'); }

    // --- предметы и укрытия стоят на свободных местах
    std::printf("\n[items/hide spots]\n");
    for (const Item& it : Level2GetItems()) {
        const float x = (it.cx + 0.5f) * CELL, z = (it.cz + 0.5f) * CELL;
        CHECK(!Level2CircleHits(x, z, 0.3f), "item (%d,%d) inside geometry", it.cx, it.cz);
    }
    for (int i = 0; i < Level2PatrolCount(); ++i) {
        Vector2 p = Level2PatrolPoint(i);
        CHECK(!Level2CircleHits(p.x, p.y, 0.35f), "hide spot %d (%.1f,%.1f) inside geometry", i, p.x, p.y);
        CHECK(Level2IsDark(p.x, p.y), "hide spot %d is not dark", i);
    }

    // --- проходимость при закрытой двери
    std::printf("\n[reachability, fire door closed]\n");
    Flood fl; fl.Run(START_X, START_Z, 0.3f);
    for (const Item& it : Level2GetItems()) {
        const float x = (it.cx + 0.5f) * CELL, z = (it.cz + 0.5f) * CELL;
        const bool inPassage = x > 88 && x < 90 && z < 38;
        CHECK(fl.Reached(x, z) != inPassage, "item (%d,%d) reach=%d expected=%d", it.cx, it.cz, fl.Reached(x, z), !inPassage);
    }
    for (int i = 0; i < Level2PatrolCount(); ++i) { Vector2 p = Level2PatrolPoint(i); CHECK(fl.Reached(p.x, p.y), "hide spot %d unreachable", i); }
    CHECK(fl.Reached(89.0f, 39.0f), "niche in front of the door unreachable");
    CHECK(!fl.Reached(89.0f, 36.0f), "passage reachable while the fire door is closed (leak!)");
    int open = 0, reached = 0;
    for (int z = 0; z < MAP_H; ++z) for (int x = 0; x < MAP_W; ++x) if (!Level2IsSolid(x, z)) {
        const float cx = (x + 0.5f) * CELL, cz = (z + 0.5f) * CELL;
        if (cx > 88 && cx < 90 && cz < 38) continue;
        ++open; if (fl.Reached(cx, cz)) ++reached;
    }
    std::printf("  open cells reachable (excluding passage): %d / %d\n", reached, open);

    // --- взаимодействие с дверью: бросок вдали не трогает дверь
    std::printf("\n[door interaction]\n");
    Player far = MakePlayer(89.0f, 45.0f, 3.14159265f);
    CHECK(!Level2Interact(far), "far player opened the fire door");
    Player away = MakePlayer(89.0f, 39.2f, 0.0f);   // рядом, но спиной к двери
    CHECK(!Level2Interact(away), "player facing away opened the fire door");
    Player aside = MakePlayer(89.0f, 39.2f, 1.5707963f);   // смотрит вдоль стены
    CHECK(!Level2Interact(aside), "player looking sideways opened the fire door");
    Player near = MakePlayer(89.0f, 39.2f, 3.14159265f);
    CHECK(Level2Interact(near), "near+facing player could not open the fire door");
    CHECK(!Level2Interact(near), "fire door opened twice");
    Player pf = MakePlayer(89.0f, 30.0f, 3.14159265f);
    for (int i = 0; i < 90; ++i) Level2Update(pf, 1.0f / 60.0f);
    Flood fl2; fl2.Run(START_X, START_Z, 0.3f);
    CHECK(fl2.Reached(89.0f, 36.0f), "passage unreachable after opening");
    CHECK(fl2.Reached(89.0f, 17.5f), "far end of the passage unreachable after opening (steps/collision mismatch?)");
    for (const Item& it : Level2GetItems()) { const float x = (it.cx + 0.5f) * CELL, z = (it.cz + 0.5f) * CELL; CHECK(fl2.Reached(x, z), "item (%d,%d) unreachable after opening", it.cx, it.cz); }

    // --- ступени прохода: идём вперёд и назад по оси и по краям, пол совпадает с коллизией
    std::printf("\n[passage steps]\n");
    for (float lane : {88.9f, 89.0f, 89.1f}) {
        float floorY = Level2FloorAt(lane, 37.5f); bool ok = true; float maxStep = 0;
        for (float z = 37.5f; z > 17.0f; z -= 0.02f) { float nf; if (!Level2CanMoveTo(lane, z, 0.3f, floorY, &nf)) { ok = false; std::printf("  blocked going north at z=%.2f lane=%.1f\n", z, lane); break; } maxStep = std::max(maxStep, fabsf(nf - floorY)); floorY = nf; }
        CHECK(ok, "forward walk blocked on lane %.1f", lane);
        for (float z = 17.0f; z < 37.5f && ok; z += 0.02f) { float nf; if (!Level2CanMoveTo(lane, z, 0.3f, floorY, &nf)) { ok = false; std::printf("  blocked going south at z=%.2f lane=%.1f\n", z, lane); } else floorY = nf; }
        CHECK(ok, "backward walk blocked on lane %.1f", lane);
        std::printf("  lane %.1f: max single rise %.2f m (limit %.2f), top floor %.2f m\n", lane, maxStep, MAX_STEP_UP, Level2FloorAt(lane, 17.5f));
    }
    {   // боком вдоль ступени: поперёк прохода на каждой ступени
        bool ok = true;
        for (float z : {30.8f, 30.5f, 24.3f, 24.0f, 18.7f, 18.4f}) { float f0 = Level2FloorAt(89.0f, z); for (float x = 88.6f; x < 89.4f; x += 0.02f) { float nf; if (!Level2CanMoveTo(x, z, 0.3f, f0, &nf)) { ok = false; break; } f0 = nf; } }
        CHECK(ok, "strafing across steps is blocked");
    }

    // --- конечная дверь: без нажатия перехода нет; с нажатием - ровно один раз
    std::printf("\n[final door]\n");
    Player endp = MakePlayer(89.0f, 17.3f, 3.14159265f); endp.pos.y = Level2FloorAt(89.0f, 17.3f);
    bool auto_trigger = false;
    for (int i = 0; i < 60 * 10; ++i) if (Level2Update(endp, 1.0f / 60.0f)) auto_trigger = true;
    CHECK(!auto_trigger, "AUTOMATIC transition happened without pressing THROW");
    Player farEnd = MakePlayer(89.0f, 27.0f, 3.14159265f);
    CHECK(!Level2Interact(farEnd), "final door used from far away");
    CHECK(Level2Interact(endp), "final door did not react to THROW nearby");
    int fires = 0; for (int i = 0; i < 60 * 3; ++i) if (Level2Update(endp, 1.0f / 60.0f)) ++fires;
    CHECK(fires == 1, "transition fired %d times (expected exactly 1)", fires);

    // --- ориентация текста и стрелок по записям геометрии
    std::printf("\n[text / arrow orientation]\n");
    int arrowsSeen[16] = {0};
    for (const QuadRecord& r : sc.Records()) {
        if (r.tex != TEX_ATLAS) continue;
        V3 n, right, c; QuadFrame(r, &n, &right, &c);
        CHECK(fabsf(n.y) < 1e-3f, "atlas quad not vertical (tag %d)", r.tag);
        CHECK(Dot(r.p[3] - r.p[0], right) > 0.0f && r.u[3] > r.u[0], "tag %d: u does not grow to the viewer's right (mirrored)", r.tag);
        CHECK(r.v[1] > r.v[0] && r.p[1].y < r.p[0].y, "tag %d: v does not grow downward (upside down)", r.tag);
        const V3 inside = c + n * 0.5f;
        CHECK(!Level2IsSolid((int)floorf(inside.x / CELL), (int)floorf(inside.z / CELL)), "tag %d: face points into the wall", r.tag);
        CHECK(c.y > 1.0f && c.y < 2.0f && r.p[0].y <= 2.51f && r.p[1].y >= 0.89f, "tag %d: height %.2f..%.2f not at eye level", r.tag, r.p[1].y, r.p[0].y);
        if (r.tag >= 0 && r.tag < (int)(sizeof(kArrows) / sizeof(kArrows[0]))) {
            float u0, v0, u1, v1; int slot = -1;
            for (int s = 0; s < ATLAS_SLOTS; ++s) { AtlasSlotRect(s, &u0, &v0, &u1, &v1); if (fabsf(u0 - r.u[0]) < 1e-5f && fabsf(v0 - r.v[0]) < 1e-5f) slot = s; }
            CHECK(slot >= 0 && slot <= ATLAS_ARROW_L2, "arrow tag %d uses non-arrow atlas slot %d", r.tag, slot);
            const bool texPointsRight = slot < ATLAS_ARROW_L0;
            const V3 dir = right * (texPointsRight ? 1.0f : -1.0f);
            const ArrowDef& a = kArrows[r.tag];
            CHECK(dir.x * a.dirX + dir.z * a.dirZ > 0.99f, "arrow %d points (%.2f,%.2f) instead of (%d,%d)", r.tag, dir.x, dir.z, a.dirX, a.dirZ);
            ++arrowsSeen[r.tag];
        }
    }
    for (size_t i = 0; i < sizeof(kArrows) / sizeof(kArrows[0]); ++i) CHECK(arrowsSeen[i] == 1, "arrow %zu geometry count = %d", i, arrowsSeen[i]);
    for (float ang : {0.0f, 0.6f, 1.2f, 1.66f}) {    // надписи на створках: при любом угле двери
        Level2TestSetDoorAngles(ang, ang);
        const Scene& ds = Level2TestDoors(); int found = 0;
        for (const QuadRecord& r : ds.Records()) {
            if (r.tag < 100 || r.tag > 102) continue; ++found;
            V3 n, right, c; QuadFrame(r, &n, &right, &c);
            CHECK(Dot(r.p[3] - r.p[0], right) > 0.0f && r.u[3] > r.u[0], "sign %d at angle %.2f is mirrored", r.tag, ang);
            CHECK(r.v[1] > r.v[0] && r.p[1].y < r.p[0].y, "sign %d at angle %.2f is upside down", r.tag, ang);
            CHECK(fabsf(n.y) < 1e-3f, "sign %d not vertical", r.tag);
            const float hingeZ = r.tag == 102 ? FINAL_DOOR_Z : FIRE_DOOR_Z;
            if (ang == 0.0f) { CHECK(n.z > 0.999f && c.z > hingeZ, "sign %d does not face the player side (n.z=%.2f)", r.tag, n.z); }
            // копланарность 4 вершин
            CHECK(fabsf(Dot(r.p[2] - r.p[0], n)) < 1e-4f, "sign %d quad not planar", r.tag);
        }
        CHECK(found == 3, "found %d door signs at angle %.2f", found, ang);
    }
    Level2TestSetDoorAngles(DOOR_OPEN_ANGLE, 0.0f);

    // --- проходимость сетки Физички (центры клеток, r=0.35, как в BFS entities.cpp)
    std::printf("\n[hound grid]\n");
    {
        static bool seen[MAP_H][MAP_W]; std::memset(seen, 0, sizeof seen);
        std::queue<std::pair<int, int>> q; q.push({4, 21}); seen[21][4] = true;
        while (!q.empty()) { auto [x, z] = q.front(); q.pop(); const int d[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& dd : d) { int nx = x + dd[0], nz = z + dd[1]; if (nx < 0 || nz < 0 || nx >= MAP_W || nz >= MAP_H || seen[nz][nx] || Level2IsSolid(nx, nz)) continue;
                if (Level2CircleHits((nx + 0.5f) * CELL, (nz + 0.5f) * CELL, 0.35f)) continue; seen[nz][nx] = true; q.push({nx, nz}); } }
        for (int i = 0; i < Level2PatrolCount(); ++i) { Vector2 p = Level2PatrolPoint(i); CHECK(seen[(int)(p.y / CELL)][(int)(p.x / CELL)], "hide spot %d not connected on the cell grid", i); }
        int cells = 0, ok = 0; for (int z = 0; z < MAP_H; ++z) for (int x = 0; x < MAP_W; ++x) if (!Level2IsSolid(x, z)) { ++cells; if (seen[z][x]) ++ok; }
        std::printf("  open cells %d, reachable on hound grid %d\n", cells, ok);
        std::printf("  unreachable open cells (excluding passage):");
        for (int z = 0; z < MAP_H; ++z) for (int x = 0; x < MAP_W; ++x) if (!Level2IsSolid(x, z) && !seen[z][x] && !(x == 44 && z <= 18)) std::printf(" (%d,%d)", x, z);
        std::printf("\n");
    }

    // --- свет: голубой источник и затухание
    std::printf("\n[light]\n");
    float lp[4], lc[3], lb[4]; CHECK(Level2GetLocalLight(lp, lc, lb), "local light inactive in Level 2");
    std::printf("  blue lamp at (%.1f,%.1f,%.1f) radius %.1f, gate x[%.1f,%.1f] z[%.1f,%.1f]\n", lp[0], lp[1], lp[2], lp[3], lb[0], lb[1], lb[2], lb[3]);
    CHECK(lb[3] < FIRE_DOOR_Z, "blue gate leaks outside the passage");
    CHECK(Level2LightAt(89.0f, 37.9f) < 0.20f, "emergency lamp from the niche leaks through the closed door into the passage");

    if (dumpDir) {
        std::string d(dumpDir);
        Level2TestSetDoorAngles(0.0f, 0.0f);   DumpScene(dumpDir, "scene.bin", sc); DumpScene(dumpDir, "doors_closed.bin", Level2TestDoors());
        Level2TestSetDoorAngles(DOOR_OPEN_ANGLE, 0.0f); DumpScene(dumpDir, "doors_open.bin", Level2TestDoors());
        for (int s = 0; s < TEX_COUNT; ++s) StubDumpTexture(Level2TestTextureId(s), (d + "/tex" + std::to_string(s) + ".rgba").c_str());
    }
    Level2SetActive(false);
    std::printf("\nRESULT: %d checks passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
