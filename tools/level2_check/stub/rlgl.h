#pragma once
#include "raylib.h"
#define RL_QUADS 0x0007
void rlSetTexture(unsigned int);
bool rlCheckRenderBatchLimit(int);
void rlBegin(int); void rlEnd();
void rlColor4ub(unsigned char, unsigned char, unsigned char, unsigned char);
void rlTexCoord2f(float, float);
void rlVertex3f(float, float, float);
void rlEnableBackfaceCulling(); void rlDisableBackfaceCulling();
void rlDrawRenderBatchActive();
