#include "raycaster.h"
#include "textures.h"
#include "raylib.h"
#include "raymath.h"

#include "raygui.h"
#include <cmath>
#include <vector>
#include <algorithm>

void InitRaycaster(RaycasterState* rc) {
    rc->rcX = 0.0f;
    rc->rcY = 0.0f;
    rc->rcAngle = 0.0f;
    rc->rcFOV = 1.2f;
    rc->rcMoveSpeed = 3.5f;
    rc->rcTurnSpeed = 2.0f;
    rc->rcPitch = 0;
    rc->rcMouseSensX = 0.003f;
    rc->rcMouseSensY = 2.0f;
    rc->cursorLocked = true;
    rc->skyColor = { 25, 25, 45, 255 };
    rc->showSkyColorPicker = false;
}

Vector2 GetRaycasterWorldPos(const RaycasterState* rc) {
    return { rc->rcX, rc->rcY };
}

float GetRaycasterAngle(const RaycasterState* rc) {
    return rc->rcAngle;
}

GameScene UpdateDrawRaycaster(RaycasterState* rc, DynamicMap* map) {
    GameScene nextScene = SCENE_BLUE;

    float dt = GetFrameTime();
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    if (IsKeyPressed(KEY_BACKSPACE)) rc->cursorLocked = !rc->cursorLocked;
    if (IsKeyPressed(KEY_ESCAPE)) {
        nextScene = SCENE_WHITE;
        rc->cursorLocked = true;
        EnableCursor();
        return nextScene;
    }
    if (IsKeyPressed(KEY_F1)) rc->showSkyColorPicker = !rc->showSkyColorPicker;

    if (rc->cursorLocked) {
        if (!IsCursorHidden()) DisableCursor();

        Vector2 md = GetMouseDelta();
        rc->rcAngle += md.x * rc->rcMouseSensX;
        rc->rcPitch = Clamp(rc->rcPitch + md.y * rc->rcMouseSensY, -(float)sh * 0.35f, (float)sh * 0.35f);

        float dx = 0, dy = 0;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) { dx += cosf(rc->rcAngle); dy += sinf(rc->rcAngle); }
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) { dx -= cosf(rc->rcAngle); dy -= sinf(rc->rcAngle); }
        if (IsKeyDown(KEY_A)) { dx += cosf(rc->rcAngle - 1.5708f); dy += sinf(rc->rcAngle - 1.5708f); }
        if (IsKeyDown(KEY_D)) { dx += cosf(rc->rcAngle + 1.5708f); dy += sinf(rc->rcAngle + 1.5708f); }
        float spd = rc->rcMoveSpeed * dt;

        auto isSolid = [&](float px, float py) -> bool {
            int mx = (int)floorf(px) + map->offsetX;
            int my = (int)floorf(py) + map->offsetY;
            if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return false;
            return map->data[mx][my].height > 0.0f && map->data[mx][my].solid;
        };
        float nx = rc->rcX + dx * spd, ny = rc->rcY + dy * spd;
        if (!isSolid(nx, rc->rcY)) rc->rcX = nx;
        if (!isSolid(rc->rcX, ny)) rc->rcY = ny;
    }
    else {
        if (IsCursorHidden()) EnableCursor();
    }

    BeginDrawing();

    int half = sh / 2;

    DrawRectangle(0, 0, sw, half + (int)rc->rcPitch, rc->skyColor);

    DrawRectangle(0, half + (int)rc->rcPitch, sw, sh - (half + (int)rc->rcPitch),
        { (uint8_t)((rc->skyColor.r >> 1)), (uint8_t)((rc->skyColor.g >> 1)), (uint8_t)((rc->skyColor.b >> 1)), 255 });

    std::vector<float> depthBuf(sw, 1e30f);

    for (int col = 0; col < sw; col++) {
        float rayA = rc->rcAngle + atanf((col - sw * 0.5f) / (sw / (2.0f * tanf(rc->rcFOV * 0.5f))));
        float sinA = sinf(rayA), cosA = cosf(rayA);

        float rx = rc->rcX, ry = rc->rcY;
        float deltaDistX = (fabsf(cosA) < 1e-6f) ? 1e30f : fabsf(1.0f / cosA);
        float deltaDistY = (fabsf(sinA) < 1e-6f) ? 1e30f : fabsf(1.0f / sinA);

        int mapX = (int)floorf(rx), mapY = (int)floorf(ry);
        int stepX, stepY;
        float sideDistX, sideDistY;

        if (cosA < 0) { stepX = -1; sideDistX = (rx - mapX) * deltaDistX; }
        else { stepX = 1; sideDistX = (mapX + 1.0f - rx) * deltaDistX; }
        if (sinA < 0) { stepY = -1; sideDistY = (ry - mapY) * deltaDistY; }
        else { stepY = 1; sideDistY = (mapY + 1.0f - ry) * deltaDistY; }

        bool hit = false; int side = 0;
        float dist = 0;
        Color wallColor = GRAY;
        float wallH = 1.0f;
        int maxSteps = map->width + map->height + 10;

        for (int step = 0; step < maxSteps && !hit; step++) {
            if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }

            int ax = mapX + map->offsetX, ay = mapY + map->offsetY;
            if (ax < 0 || ax >= map->width || ay < 0 || ay >= map->height) { dist = 30.0f; break; }
            if (map->data[ax][ay].height > 0.0f && map->data[ax][ay].type != TYPE_TEXTURE_ONLY) {
                hit = true;
                wallColor = map->data[ax][ay].color;
                wallH = map->data[ax][ay].height;
                dist = (side == 0)
                    ? (mapX - rx + (1 - stepX) * 0.5f) / cosA
                    : (mapY - ry + (1 - stepY) * 0.5f) / sinA;
                if (dist < 0) dist = 0.001f;
            }
        }

        if (!hit) continue;

        float corrDist = dist * cosf(rayA - rc->rcAngle);
        if (corrDist < 0.001f) corrDist = 0.001f;

        float baseH = (float)sh / corrDist;
        float drawH = baseH * wallH;

        float wallCenter = half + rc->rcPitch;
        int drawTop = (int)(wallCenter - drawH * 0.5f);
        int drawBottom = (int)(wallCenter + drawH * 0.5f);

        float shade = 1.0f / (1.0f + corrDist * 0.18f);
        if (side == 1) shade *= 0.65f;
        shade = Clamp(shade, 0.05f, 1.0f);

        Color c = {
            (uint8_t)(wallColor.r * shade),
            (uint8_t)(wallColor.g * shade),
            (uint8_t)(wallColor.b * shade),
            255
        };

        depthBuf[col] = corrDist;
        DrawLine(col, drawTop, col, drawBottom, c);
    }

    struct BillboardEntry { float dist; int ax, ay; };
    std::vector<BillboardEntry> bills;
    for (int ax = 0; ax < map->width; ax++) {
        for (int ay = 0; ay < map->height; ay++) {
            Cell& c = map->data[ax][ay];
            if (c.textureIndex < 0) continue;
            float lx = (ax - map->offsetX) + 0.5f;
            float ly = (ay - map->offsetY) + 0.5f;
            float dx2 = lx - rc->rcX, dy2 = ly - rc->rcY;
            float dist2 = dx2 * dx2 + dy2 * dy2;
            bills.push_back({ dist2, ax, ay });
        }
    }

    std::sort(bills.begin(), bills.end(), [](const BillboardEntry& a, const BillboardEntry& b) { return a.dist > b.dist; });

    for (auto& b : bills) {
        Cell& c = map->data[b.ax][b.ay];
        if (c.textureIndex < 0 || !gTextures[c.textureIndex].valid) continue;
        Texture2D& tex = gTextures[c.textureIndex].tex;

        float lx = (b.ax - map->offsetX) + 0.5f;
        float ly = (b.ay - map->offsetY) + 0.5f;
        float dx2 = lx - rc->rcX, dy2 = ly - rc->rcY;

        float spriteAngle = atan2f(dy2, dx2);
        float angleDiff = spriteAngle - rc->rcAngle;
        while (angleDiff > (float)PI) angleDiff -= 2 * (float)PI;
        while (angleDiff < -(float)PI) angleDiff += 2 * (float)PI;

        float dist = sqrtf(b.dist);
        if (dist < 0.1f) continue;

        if (fabsf(angleDiff) > rc->rcFOV * 0.75f) continue;

        float screenX = (sw / 2) + tanf(angleDiff) * (sw / (2.0f * tanf(rc->rcFOV * 0.5f)));
        float corrDist = dist * cosf(angleDiff);
        if (corrDist < 0.001f) continue;

        float effectiveH = 2.5f;
        //float effectiveH = (c.height > 0.0f) ? c.height : 1.0f;
        float spriteH = (float)sh / corrDist * effectiveH;
        float spriteW = spriteH * ((float)tex.width / tex.height);

        float drawX = screenX - spriteW * 0.5f;
        float drawY = (sh * 0.5f) + rc->rcPitch - spriteH * 0.5f;

        int colStart = (int)drawX;
        int colEnd = (int)(drawX + spriteW);
        for (int sc = colStart; sc < colEnd; sc++) {
            if (sc < 0 || sc >= sw) continue;
            if (corrDist >= depthBuf[sc]) continue;
            float u = (float)(sc - colStart) / spriteW;
            DrawTexturePro(tex,
                { u * tex.width, 0, 1.0f, (float)tex.height },
                { (float)sc, drawY, 1.0f, spriteH },
                { 0, 0 }, 0.0f, WHITE);
        }
    }

    DrawRectangle(0, sh - 50, sw, 50, { 35,35,35,200 });
    float cx2 = (float)sw / 2;
    if (GuiButton({ cx2 - 110, (float)sh - 40, 100, 30 }, "EDITOR")) nextScene = SCENE_WHITE;
    if (GuiButton({ cx2 + 10,  (float)sh - 40, 100, 30 }, "MAP"))    nextScene = SCENE_BLUE;
    DrawText("F1=Sky | Backspace=cursor | Esc=back | WASD=move | mouse=look",
        10, sh - 42, 9, { 140,140,160,255 });

    if (rc->showSkyColorPicker) {
        DrawRectangle(10, 10, 175, 200, { 40,40,40,230 });
        DrawRectangleLinesEx({ 10, 10, 175, 200 }, 1, LIGHTGRAY);
        DrawText("Sky Color", 18, 16, 10, LIGHTGRAY);
        GuiColorPicker({ 18, 30, 155, 155 }, NULL, &rc->skyColor);
        if (GuiButton({ 18, 190, 80, 16 }, "Close")) rc->showSkyColorPicker = false;
    }
    DrawFPS(sw - 80, sh - 35);
    EndDrawing();

    return nextScene;
}
