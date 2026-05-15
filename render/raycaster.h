#pragma once
#include "types.h"

struct RaycasterState {
    float rcX;
    float rcY;
    float rcAngle;
    float rcFOV;
    float rcMoveSpeed;
    float rcTurnSpeed;
    float rcPitch;
    float rcMouseSensX;
    float rcMouseSensY;
    bool cursorLocked;
    Color skyColor;
    bool showSkyColorPicker;
};

void InitRaycaster(RaycasterState* rc);
GameScene UpdateDrawRaycaster(RaycasterState* rc, DynamicMap* map);
Vector2 GetRaycasterWorldPos(const RaycasterState* rc);
float GetRaycasterAngle(const RaycasterState* rc);
