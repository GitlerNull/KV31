#!/usr/bin/env python3
"""Safely apply the KV31 Level 0 dark-room / persistent Hound patch.

Run from the KV31 repository root:
    python tools/apply_phase1_patch.py
The script validates every expected source fragment before changing any file.
It creates .phase1.bak backups and rolls back written files if a write fails.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import sys

ROOT = Path.cwd()
CPP = ROOT / "app/src/main/cpp"


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly 1 matching source fragment, found {count}")
    return source.replace(old, new, 1)


def patch_entities_h(text: str) -> str:
    if "bool persistent{};" in text:
        raise RuntimeError("entities.h already appears patched (persistent field exists)")
    old = """    Vector2 target, wp;       // цель патруля / следующая клетка пути
    bool flag;                // Кожекрад: true = уже раскрыт; Синди: true = игрок уже предупреждён
};"""
    new = """    Vector2 target, wp;       // цель патруля / следующая клетка пути
    bool flag;                // Кожекрад: true = уже раскрыт; Синди: true = игрок уже предупреждён
    bool persistent{};        // особая сущность уровня: не удаляется по времени/дистанции
};"""
    return replace_once(text, old, new, "entities.h Entity fields")


def patch_entities_cpp(text: str) -> str:
    if "h.persistent = true;" in text:
        raise RuntimeError("entities.cpp already appears patched (special Hound exists)")

    old_spawn = """    int r = GetRandomValue(0, 99);
    EType t = r < 38 ? E_HOUND : r < 68 ? E_SMILER : r < 90 ? E_STEALER : E_CINDY;
    if (t == E_CINDY && (cindy || pl.sanity > 65)) t = E_HOUND;     // Синди приходит к тем, кому плохо"""
    new_spawn = """    int r = GetRandomValue(0, 99);
    // На уровне 0 Гончая только одна: особая сущность в тёмной комнате.
    // Остальные вероятности перенормированы без случайного спавна Гончей.
    EType t = r < 48 ? E_SMILER : r < 84 ? E_STEALER : E_CINDY;
    if (t == E_CINDY && (cindy || pl.sanity > 65)) t = E_STEALER; // Синди приходит к тем, кому плохо"""
    text = replace_once(text, old_spawn, new_spawn, "entities.cpp TrySpawn type selection")

    old_reset = """void EntitiesReset() { gE.clear(); gFlying.clear(); gSpawnT = 25.0f; }"""
    new_reset = """void EntitiesReset() {
    gE.clear();
    gFlying.clear();
    gSpawnT = 25.0f;

    // Единственная Гончая уровня 0: стоит в центре комнаты с аптечками.
    // Клетка (6,2), центр клетки в мировых координатах (13,5).
    Entity h{};
    h.type = E_HOUND;
    h.st = S_IDLE;
    h.pos = {13.0f, 5.0f};
    h.wp = h.pos;
    h.target = h.pos;
    h.yaw = 0.0f;
    h.timer = 0.0f;
    h.life = 1000000.0f;
    h.persistent = true;
    gE.push_back(h);
}"""
    text = replace_once(text, old_reset, new_reset, "entities.cpp EntitiesReset")

    old_update = """        e.life -= dt * (d > 42 ? 4.0f : 1.0f);
        if (d <= 42) Think(e, pl, dt);                       // дальше не думаем - экономим BFS и генерацию чанков
        if (e.life <= 0 || d > 55) gE.erase(gE.begin() + i); else i++;"""
    new_update = """        if (!e.persistent) e.life -= dt * (d > 42 ? 4.0f : 1.0f);
        if (d <= 42) Think(e, pl, dt);                       // дальше не думаем - экономим BFS и генерацию чанков
        if (!e.persistent && (e.life <= 0 || d > 55)) gE.erase(gE.begin() + i); else i++;"""
    text = replace_once(text, old_update, new_update, "entities.cpp EntitiesUpdate lifetime/removal")
    return text


def patch_map_gen_cpp(text: str) -> str:
    if "gx >= 5 && gx <= 7 && gz >= 1 && gz <= 3" in text:
        raise RuntimeError("map_gen.cpp already appears patched (dark-room item exclusion exists)")

    old_gen = """static bool GenSolid(int x, int y) {
    int mx = PosMod(x, 4), my = PosMod(y, 4), rx = FloorDiv(x, 4), ry = FloorDiv(y, 4);"""
    new_gen = """static bool GenSolid(int x, int y) {
    // Фиксированная тёмная комната: room (1,0), внутренние клетки x=5..7, z=1..3.
    // Периметр закрыт, кроме одного входа со стороны стартовой комнаты (4,2).
    if (x >= 4 && x <= 8 && y >= 0 && y <= 4) {
        if (x == 4 && y == 2) return false;
        return x == 4 || x == 8 || y == 0 || y == 4;
    }
    int mx = PosMod(x, 4), my = PosMod(y, 4), rx = FloorDiv(x, 4), ry = FloorDiv(y, 4);"""
    text = replace_once(text, old_gen, new_gen, "map_gen.cpp GenSolid room override")

    old_lamp = """float LampLevel(int rx, int ry) {
    if (rx == 0 && ry == 0) return 1.0f;"""
    new_lamp = """float LampLevel(int rx, int ry) {
    if (rx == 1 && ry == 0) return 0.0f; // лампа в комнате с Гончей отсутствует
    if (rx == 0 && ry == 0) return 1.0f;"""
    text = replace_once(text, old_lamp, new_lamp, "map_gen.cpp LampLevel override")

    old_light = """float LightAt(float wx, float wz) {
    float u = (wx / CELL - 2.5f) / 4.0f, v = (wz / CELL - 2.5f) / 4.0f;"""
    new_light = """float LightAt(float wx, float wz) {
    int cx = CellOf(wx), cz = CellOf(wz);
    if (cx >= 5 && cx <= 7 && cz >= 1 && cz <= 3) return 0.07f; // постоянная темнота внутри комнаты
    float u = (wx / CELL - 2.5f) / 4.0f, v = (wz / CELL - 2.5f) / 4.0f;"""
    text = replace_once(text, old_light, new_light, "map_gen.cpp LightAt dark-room override")

    old_random_items = """            int gx = chx * CHUNK + x, gz = chz * CHUNK + z;
            if (abs(gx - 2) + abs(gz - 2) < 6) continue;                 // не у точки старта"""
    new_random_items = """            int gx = chx * CHUNK + x, gz = chz * CHUNK + z;
            if (gx >= 5 && gx <= 7 && gz >= 1 && gz <= 3) continue;     // в тёмной комнате только фиксированные аптечки
            if (abs(gx - 2) + abs(gz - 2) < 6) continue;                 // не у точки старта"""
    text = replace_once(text, old_random_items, new_random_items, "map_gen.cpp random item exclusion")

    old_return = """            c.items.push_back({gx, gz, t, false});
        }
    return c;
}"""
    new_return = """            c.items.push_back({gx, gz, t, false});
        }
    // Три гарантированные аптечки в комнате с Гончей. gTaken сохраняет подбор
    // даже после выгрузки и повторной генерации чанка.
    const int medkitCells[3][2] = {{5, 2}, {6, 1}, {7, 2}};
    for (const auto& cell : medkitCells) {
        int gx = cell[0], gz = cell[1];
        if (FloorDiv(gx, CHUNK) == chx && FloorDiv(gz, CHUNK) == chz && !gTaken.count(Key(gx, gz))) {
            c.items.push_back({gx, gz, IT_MEDKIT, false});
        }
    }
    return c;
}"""
    text = replace_once(text, old_return, new_return, "map_gen.cpp fixed medkit insertion")
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply KV31 Level 0 special dark-room patch safely")
    parser.add_argument("--check", action="store_true", help="validate compatibility without modifying files")
    args = parser.parse_args()

    if not (CPP / "entities.h").is_file() or not (CPP / "entities.cpp").is_file() or not (CPP / "map_gen.cpp").is_file():
        print("ERROR: run this script from the KV31 repository root (the app/src/main/cpp files were not found).", file=sys.stderr)
        return 2

    paths = [CPP / "entities.h", CPP / "entities.cpp", CPP / "map_gen.cpp"]
    originals = {p: p.read_text(encoding="utf-8") for p in paths}

    try:
        updated = {
            paths[0]: patch_entities_h(originals[paths[0]]),
            paths[1]: patch_entities_cpp(originals[paths[1]]),
            paths[2]: patch_map_gen_cpp(originals[paths[2]]),
        }
    except RuntimeError as exc:
        print(f"PATCH NOT APPLIED: {exc}", file=sys.stderr)
        print("No files were changed. Send the message above and the relevant source excerpts for adjustment.", file=sys.stderr)
        return 3

    print("Preflight OK: all expected source fragments matched exactly once.")
    for p in paths:
        print(f"  {p.relative_to(ROOT)}: {len(originals[p].splitlines())} -> {len(updated[p].splitlines())} lines")

    if args.check:
        print("Check-only mode: no files changed.")
        return 0

    backups: dict[Path, Path] = {}
    written: list[Path] = []
    try:
        for p in paths:
            backup = p.with_name(p.name + ".phase1.bak")
            if backup.exists():
                raise RuntimeError(f"backup already exists: {backup}; move it aside before applying")
            shutil.copy2(p, backup)
            backups[p] = backup
        for p in paths:
            temp = p.with_name(p.name + ".phase1.tmp")
            temp.write_text(updated[p], encoding="utf-8", newline="")
            os.replace(temp, p)
            written.append(p)
    except Exception as exc:
        for p in reversed(written):
            if p in backups:
                shutil.copy2(backups[p], p)
        print(f"ERROR while writing patch; attempted rollback: {exc}", file=sys.stderr)
        return 4

    print("PATCH APPLIED.")
    print("Backups created:")
    for p in paths:
        print(f"  {backups[p].relative_to(ROOT)}")
    print("Next: inspect `git diff --check` and `git diff`; do not build or push until the diff is reviewed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
