// entities.h - сущности Закулисья и их ИИ (конечные автоматы Idle/Patrol/Chase/Attack)
#pragma once
#include "player.h"

enum EType  { E_HOUND, E_SMILER, E_STEALER, E_CINDY };
enum EState { S_IDLE, S_PATROL, S_CHASE, S_ATTACK };

struct Entity {
    EType type; EState st;
    Vector2 pos;              // x, z в мире
    float yaw, timer, lookT, pathT, atkCd, life, t, lose, stunT, throwCd;
    Vector2 target, wp;       // цель патруля / следующая клетка пути
    bool flag;                // Кожекрад: true = уже раскрыт; Синди: true = игрок уже предупреждён
    bool persistent{};        // особая сущность уровня: не удаляется по времени/дистанции
};

void EntitiesReset();
void EntitiesThrowBottle(Player& pl);
void EntitiesUpdate(Player& pl, float dt, float insane, bool otherEntitiesEnabled); // Физичка не отключается
void EntitiesDraw(const Player& pl, bool glowPass, Shader worldShader, bool otherEntitiesEnabled);
void EntitiesUnload();        // glowPass: светящиеся части (глаза, улыбки)
