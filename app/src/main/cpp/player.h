// player.h - игрок, мультитач-ввод (джойстик слева, свайп справа, кнопки)
#pragma once
#include "util.h"

struct Player {
    Vector3 pos;                 // y не используется для физики (глаза на 1.6 м)
    float yaw, pitch;
    float health, sanity, battery, stamina;
    int   water, meds, emptyBottles;
    bool  flash, tired, crouching;
    float noise;                 // 0..1 - громкость игрока (слышат Гончие)
    float bob, stepAcc, hurt, exitT, lost;
};

struct TouchState {
    bool  joy;  Vector2 joyO, joyP;   // джойстик: центр и положение пальца (для отрисовки)
    float jx, jy;                     // -1..1
    Vector2 look;                     // сдвиг пальца камеры за кадр, px
    bool sprint, tFlash, tDrink, tMed, tThrow, crouchToggle, anyNew;
    bool tapValid, pointerDown;
    Vector2 tapPos, pointerPos;
};

void    PlayerInit(Player& p);
TouchState ReadTouch(int W, int H);
void    PlayerUpdate(Player& p, const TouchState& t, float dt, float sensitivity);
Vector3 EyePos(const Player& p);
Vector3 Forward(const Player& p);
// кнопки: 0 бег, 1 фонарик, 2 вода, 3 аптечка, 4 бросок, 5 приседание
Vector2 ButtonCenter(int i, int W, int H, float* radius);
