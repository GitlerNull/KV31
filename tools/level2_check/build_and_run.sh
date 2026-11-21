#!/bin/sh
# Офлайн-проверка Level 2 на ПК (нужны g++ и python3 с Pillow для предпросмотра). Игру/Android-сборку НЕ заменяет.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC="$HERE/../../app/src/main/cpp"; OUT="${1:-/tmp/level2_check}"
mkdir -p "$OUT"
g++ -std=c++17 -O1 -DLEVEL2_TESTING -I"$HERE/stub" -I"$SRC" "$HERE/level2_check.cpp" "$HERE/stubs.cpp" "$SRC/level2.cpp" "$SRC/level2_geom.cpp" -o "$OUT/level2_check"
python3 "$HERE/prepare_textures.py" "$OUT/raw" || true
LEVEL2_RAW_DIR="$OUT/raw" "$OUT/level2_check" --dump "$OUT"
