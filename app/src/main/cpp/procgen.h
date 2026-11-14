// procgen.h - процедурные текстуры и звуки (внешние ассеты не нужны)
#pragma once
#include "raylib.h"

struct ProcAssets { Texture2D wall, carpet, ceiling, vignette; };

ProcAssets ProcInit();            // после InitWindow(): текстуры + синтез звуков (если звук доступен)
void ProcFree(ProcAssets& a);
void ProcMusicUpdate(float vol, float pitch);   // гудение ламп (зациклено), вызывать каждый кадр
