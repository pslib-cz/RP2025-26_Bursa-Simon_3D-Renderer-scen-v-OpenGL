#define _CRT_SECURE_NO_WARNINGS
#define RAYGUI_IMPLEMENTATION
#include "raylib.h"
#include "raymath.h"
#include "raygui.h"
#include "editor.h"
#include "editor_draw.h"
#include "editor_input.h"
#include "map.h"
#include "preview.h"
#include "textures.h"
#include "utils.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

std::string OpenFileDialog();
void SyncBufsFromColor(EditorState* ed);

static void DrawCell(EditorState* ed, DynamicMap* map, int lx, int ly, int ax, int ay)
{
    Rectangle r = {
        lx * CELL_BASE_SIZE,
        ly * CELL_BASE_SIZE,
        CELL_BASE_SIZE - 1,
        CELL_BASE_SIZE - 1
    };

    bool isSel = ed->selGrid.data[ax][ay];
    Cell& cell = map->data[ax][ay];

    if (ed->sel.isMoving && isSel)
    {
        Color ghost = cell.height > 0 ? cell.color : DARKGRAY;
        DrawRectangleRec(r, Fade(ghost, 0.25f));
        return;
    }

    if (cell.height > 0)
    {
        if (cell.type == TYPE_TEXTURE_ONLY)
        {
            DrawRectangleRec(r, Fade(cell.color, 0.25f));
            DrawRectangleLinesEx(r, fmaxf(1.0f, 2.0f / ed->camera2D.zoom), { 255, 220, 0, 255 });

            float x1 = r.x, y1 = r.y;
            float x2 = r.x + r.width, y2 = r.y + r.height;
            DrawLine((int)x1, (int)y1, (int)x2, (int)y2, { 255, 220, 0, 160 });
            DrawLine((int)x2, (int)y1, (int)x1, (int)y2, { 255, 220, 0, 160 });

            if (ed->camera2D.zoom > 1.2f)
                DrawText("TEX", (int)r.x + 2, (int)r.y + 12, 8, { 255, 220, 0, 255 });
        }
        else
        {
            DrawRectangleRec(r, cell.color);

            if (!cell.solid)
                DrawRectangleLinesEx(r, fmaxf(1.0f, 2.0f / ed->camera2D.zoom), { 255, 80, 80, 200 });
        }
    }
    else
    {
        if (ed->camera2D.zoom > 0.6f)
        {
            DrawRectangleLinesEx(r, fmaxf(1.0f, 0.5f / ed->camera2D.zoom), DARKGRAY);

            bool hasColor = (cell.color.r != 80 || cell.color.g != 80 || cell.color.b != 80);

            if (hasColor)
            {
                float x1 = r.x, y1 = r.y;
                float x2 = r.x + r.width, y2 = r.y + r.height;
                DrawLine((int)x1, (int)y1, (int)x2, (int)y2, Fade(cell.color, 0.5f));
                DrawLine((int)x2, (int)y1, (int)x1, (int)y2, Fade(cell.color, 0.5f));
            }
        }
    }

    if (ed->toolMode == TOOL_SELECT && isSel && !ed->sel.isMoving)
    {
        DrawRectangleLinesEx(r, fmaxf(1.0f, 2.5f / ed->camera2D.zoom), { 0, 220, 255, 255 });
        DrawRectangleRec(r, { 0, 180, 255, 40 });
    }

    if (ed->toolMode == TOOL_SELECT && isSel && ed->sel.isMoving)
        DrawRectangleLinesEx(r, 1.5f / ed->camera2D.zoom, Fade(SKYBLUE, 0.5f));

    if (cell.textureIndex >= 0 && ed->camera2D.zoom > 0.4f)
        DrawText("T", (int)r.x + 2, (int)r.y + (int)r.height - 12, 10, { 0, 220, 80, 255 });

    if (ed->camera2D.zoom > 1.2f)
        DrawText(TextFormat("%d,%d", lx, ly), (int)r.x + 2, (int)r.y + 2, 8, Fade(GRAY, 0.4f));
}

void DrawMapCellAt(EditorState* ed, DynamicMap* map, int logicX, int logicY)
{
    int ax = logicX + map->offsetX;
    int ay = logicY + map->offsetY;
    if (ax < 0 || ax >= map->width || ay < 0 || ay >= map->height) return;

    BeginTextureMode(ed->mapRenderTex);
    BeginMode2D(ed->camera2D);
    DrawCell(ed, map, logicX, logicY, ax, ay);
    EndMode2D();
    EndTextureMode();
}

void InitMapRenderTexture(EditorState* ed, int sw, int sh)
{
    if (ed->mapRenderTex.id != 0)
        UnloadRenderTexture(ed->mapRenderTex);

    ed->mapRenderTex = LoadRenderTexture(sw, sh);
    ed->mapDirty = true;
}

void MarkMapDirty(EditorState* ed)
{
    ed->mapDirty = true;
}

void DrawMapCells(EditorState* ed, DynamicMap* map, int sw, int sh)
{
    if (ed->mapRenderTex.texture.width != sw || ed->mapRenderTex.texture.height != sh)
        InitMapRenderTexture(ed, sw, sh);

    bool cameraChanged = (ed->camera2D.target.x != ed->lastCamTarget.x
        || ed->camera2D.target.y != ed->lastCamTarget.y
        || ed->camera2D.zoom != ed->lastCamZoom
        || ed->camera2D.offset.x != ed->lastOffsetX
        || ed->camera2D.offset.y != ed->lastOffsetY);

    if (!ed->mapDirty && !cameraChanged)
    {
        DrawTextureRec(
            ed->mapRenderTex.texture,
            { 0, 0, (float)sw, -(float)sh },
            { 0, 0 },
            WHITE
        );
        return;
    }

    ed->lastCamTarget = ed->camera2D.target;
    ed->lastCamZoom = ed->camera2D.zoom;
    ed->lastOffsetX = ed->camera2D.offset.x;
    ed->lastOffsetY = ed->camera2D.offset.y;
    ed->mapDirty = false;

    BeginTextureMode(ed->mapRenderTex);
    ClearBackground({ 25, 25, 25, 255 });
    BeginMode2D(ed->camera2D);

    Vector2 tl = GetScreenToWorld2D({ 0, 45 }, ed->camera2D);
    Vector2 br = GetScreenToWorld2D({ (float)sw, (float)sh - 50 }, ed->camera2D);

    int sLX = (int)floor(tl.x / CELL_BASE_SIZE) - 1;
    int sLY = (int)floor(tl.y / CELL_BASE_SIZE) - 1;
    int eLX = (int)floor(br.x / CELL_BASE_SIZE) + 1;
    int eLY = (int)floor(br.y / CELL_BASE_SIZE) + 1;

    for (int ly = sLY; ly <= eLY; ly++)
    {
        for (int lx = sLX; lx <= eLX; lx++)
        {
            int ax = lx + map->offsetX;
            int ay = ly + map->offsetY;

            if (ax < 0 || ax >= map->width || ay < 0 || ay >= map->height)
                continue;

            DrawCell(ed, map, lx, ly, ax, ay);
        }
    }

    EndMode2D();
    EndTextureMode();

    DrawTextureRec(
        ed->mapRenderTex.texture,
        { 0, 0, (float)sw, -(float)sh },
        { 0, 0 },
        WHITE
    );
}

void DrawMovingSelection(EditorState* ed, DynamicMap* map)
{
    for (int i = 0; i < ed->sel.count; i++)
    {
        int lx = ed->sel.srcX[i] - map->offsetX + ed->sel.moveDeltaX;
        int ly = ed->sel.srcY[i] - map->offsetY + ed->sel.moveDeltaY;

        int nx = ed->sel.srcX[i] + ed->sel.moveDeltaX;
        int ny = ed->sel.srcY[i] + ed->sel.moveDeltaY;
        bool inBounds = (nx >= 0 && nx < map->width && ny >= 0 && ny < map->height);

        Rectangle r = {
            lx * CELL_BASE_SIZE,
            ly * CELL_BASE_SIZE,
            CELL_BASE_SIZE - 1,
            CELL_BASE_SIZE - 1
        };

        DrawRectangleRec(r, Fade(ed->sel.cells[i].color, inBounds ? 0.65f : 0.3f));
        DrawRectangleLinesEx(r, 2.0f / ed->camera2D.zoom, { 0, 200, 255, 200 });
    }
}

void DrawDragSelect(EditorState* ed)
{
    int lx0 = (int)floor(fminf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE);
    int ly0 = (int)floor(fminf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE);
    int lx1 = (int)floor(fmaxf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE) + 1;
    int ly1 = (int)floor(fmaxf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE) + 1;

    Rectangle sr = {
        lx0 * CELL_BASE_SIZE,
        ly0 * CELL_BASE_SIZE,
        (lx1 - lx0) * CELL_BASE_SIZE,
        (ly1 - ly0) * CELL_BASE_SIZE
    };

    DrawRectangleRec(sr, { 0, 160, 255, 30 });
    DrawRectangleLinesEx(sr, 1.5f / ed->camera2D.zoom, { 0, 200, 255, 200 });
}

void DrawPastePreview(EditorState* ed, int logicX, int logicY)
{
    for (int i = 0; i < ed->clipboard.count; i++)
    {
        int lx = logicX + ed->clipboard.relX[i];
        int ly = logicY + ed->clipboard.relY[i];

        Rectangle r = {
            lx * CELL_BASE_SIZE,
            ly * CELL_BASE_SIZE,
            CELL_BASE_SIZE - 1,
            CELL_BASE_SIZE - 1
        };

        Color baseColor = ed->clipboard.cells[i].height > 0
            ? ed->clipboard.cells[i].color
            : DARKGRAY;

        DrawRectangleRec(r, Fade(baseColor, 0.5f));
        DrawRectangleLinesEx(r, 2.0f / ed->camera2D.zoom, { 255, 200, 0, 200 });
    }
}

void DrawShapePreview(EditorState* ed)
{
    for (int i = 0; i < gPreviewCount; i++)
    {
        Rectangle r = {
            gPreview[i].lx * CELL_BASE_SIZE,
            gPreview[i].ly * CELL_BASE_SIZE,
            CELL_BASE_SIZE - 1,
            CELL_BASE_SIZE - 1
        };

        DrawRectangleRec(r, Fade(ed->brushColor, 0.55f));
        DrawRectangleLinesEx(r, fmaxf(1.0f, 1.5f / ed->camera2D.zoom), { 255, 200, 0, 200 });
    }

    Rectangle sr = {
        ed->shapeStartLX * CELL_BASE_SIZE,
        ed->shapeStartLY * CELL_BASE_SIZE,
        CELL_BASE_SIZE - 1,
        CELL_BASE_SIZE - 1
    };

    DrawRectangleLinesEx(sr, 3.0f / ed->camera2D.zoom, WHITE);
}

void DrawCursorHighlight(EditorState* ed, bool mouseOnUI, int logicX, int logicY)
{
    if (mouseOnUI)
        return;

    Rectangle r = {
        logicX * CELL_BASE_SIZE,
        logicY * CELL_BASE_SIZE,
        CELL_BASE_SIZE - 1,
        CELL_BASE_SIZE - 1
    };

    if (ed->toolMode == TOOL_BRUSH || ed->toolMode == TOOL_NONE)
        DrawRectangleLinesEx(r, 1.5f / ed->camera2D.zoom, Fade(ed->brushColor, 0.7f));

    if (ed->toolMode == TOOL_ERASER)
        DrawRectangleLinesEx(r, 1.5f / ed->camera2D.zoom, Fade(RED, 0.7f));
}

void DrawPlayerMarker(EditorState* ed, Vector2 rcWorldPos, float rcAngle, int sw, int sh)
{
    float wx = rcWorldPos.x * CELL_BASE_SIZE;
    float wy = rcWorldPos.y * CELL_BASE_SIZE;

    float axisLen = 50.0f * CELL_BASE_SIZE;
    float lw = fmaxf(1.0f, 2.0f / ed->camera2D.zoom);

    if (ed->camera2D.zoom > 0.6f)
    {
        DrawLineEx({ -axisLen, 0 }, { axisLen, 0 }, lw, { 255, 50, 50, 180 });
        DrawLineEx({ 0, -axisLen }, { 0, axisLen }, lw, { 50, 220, 50, 180 });
    }

    float dlen = 0.5f * CELL_BASE_SIZE;
    DrawLineEx(
        { wx, wy },
        { wx + cosf(rcAngle) * dlen, wy + sinf(rcAngle) * dlen },
        2.5f / ed->camera2D.zoom,
        { 255, 200, 0, 255 }
    );

    DrawCircleV({ wx, wy }, 6.0f / ed->camera2D.zoom, { 255, 100, 0, 220 });
    DrawCircleLinesV({ wx, wy }, 6.0f / ed->camera2D.zoom, WHITE);
}

GameScene DrawTopBar(EditorState* ed, DynamicMap* map, int sw, int sh)
{
    GameScene result = SCENE_WHITE;

    DrawRectangle(0, 0, sw, 45, { 35, 35, 35, 255 });
    DrawLine(0, 45, sw, 45, GRAY);
    DrawText("Map Editor", 24, 14, 16, LIGHTGRAY);

    Rectangle undoBtnRect = { (float)sw - 410, 10, 50, 25 };
    Rectangle redoBtnRect = { (float)sw - 355, 10, 50, 25 };
    Rectangle newBtnRect = { (float)sw - 300, 10, 50, 25 };
    Rectangle saveBtnRect = { (float)sw - 245, 10, 55, 25 };
    Rectangle loadBtnRect = { (float)sw - 185, 10, 55, 25 };
    Rectangle returnBtnRect = { (float)sw - 125, 10, 115, 25 };

    bool canUndo = (ed->undoStack.current > 0);
    bool canRedo = (ed->undoStack.current < ed->undoStack.count - 1);

    GuiSetState(canUndo ? STATE_NORMAL : STATE_DISABLED);

    if (GuiButton(undoBtnRect, "#56#Undo"))
    {
        if (UndoStep(&ed->undoStack, map))
        {
            FreeSelectionGrid(&ed->selGrid);
            ed->selGrid = InitSelectionGrid(map->width, map->height);
            ClearSelection(&ed->selGrid, &ed->sel);
            SetStatus(&ed->status, "Undo", SKYBLUE);
            MarkMapDirty(ed);
        }
    }

    GuiSetState(canRedo ? STATE_NORMAL : STATE_DISABLED);

    if (GuiButton(redoBtnRect, "#57#Redo"))
    {
        if (RedoStep(&ed->undoStack, map))
        {
            FreeSelectionGrid(&ed->selGrid);
            ed->selGrid = InitSelectionGrid(map->width, map->height);
            ClearSelection(&ed->selGrid, &ed->sel);
            SetStatus(&ed->status, "Redo", SKYBLUE);
            MarkMapDirty(ed);
        }
    }

    GuiSetState(STATE_NORMAL);

    if (GuiButton(newBtnRect, "#8#New"))
        ed->showNewConfirm = true;

    if (GuiButton(saveBtnRect, "#2#Save"))
    {
        if (SaveMap(ed->saveFile, map))
            SetStatus(&ed->status, TextFormat("Saved: %s", ed->saveFile), GREEN);
        else
            SetStatus(&ed->status, TextFormat("Save FAILED: %s", ed->saveFile), RED);
    }

    if (GuiButton(loadBtnRect, "#1#Load"))
    {
        bool ok;
        DynamicMap loaded = LoadMap(ed->saveFile, &ok);

        if (ok)
        {
            FreeDynamicMap(map);
            *map = loaded;

            FreeSelectionGrid(&ed->selGrid);
            ed->selGrid = InitSelectionGrid(map->width, map->height);
            ClearSelection(&ed->selGrid, &ed->sel);
            ClearUndoStack(&ed->undoStack);
            PushUndo(&ed->undoStack, map);
            SetStatus(&ed->status, TextFormat("Loaded: %s", ed->saveFile), GREEN);
            MarkMapDirty(ed);
        }
        else
        {
            SetStatus(&ed->status, TextFormat("Load FAILED: %s", ed->saveFile), RED);
        }
    }

    if (GuiButton(returnBtnRect, "#101#Return to 0,0"))
        ed->camera2D.target = { 0, 0 };

    return result;
}


void DrawSidebar(EditorState* ed, DynamicMap* map, Vector2 mousePos, int sh, int logicX, int logicY)
{
    const int COLOR_PANEL_H = 16 + 144 + 4 + 16 + 20 + 48 + 12 + 10;
    int sidebarH = sh - 96 - COLOR_PANEL_H;

    DrawRectangle(0, 46, 160, sh - 96, { 30, 30, 30, 200 });
    DrawLine(160, 46, 160, sh - 50, { 80, 80, 80, 200 });

    Rectangle fileInputRect = { 8, 50, 144, 18 };
    Color boxBorder = ed->editingFilename ? Color{ 0, 200, 255, 255 } : Color{ 80, 80, 80, 255 };
    DrawRectangleLinesEx(fileInputRect, 1, boxBorder);
    DrawText(ed->saveFile, (int)fileInputRect.x + 3, (int)fileInputRect.y + 4, 9, LIGHTGRAY);

    if (ed->editingFilename && ((int)(GetTime() * 2) % 2 == 0))
    {
        int cx2 = (int)fileInputRect.x + 3 + MeasureText(ed->saveFile, 9);

        if (cx2 < (int)(fileInputRect.x + fileInputRect.width - 2))
            DrawLine(cx2, (int)fileInputRect.y + 2, cx2, (int)fileInputRect.y + 15, { 0, 200, 255, 255 });
    }

    int sby = 72;

    GuiSetState(ed->toolMode == TOOL_BRUSH ? STATE_PRESSED : STATE_NORMAL);

    if (GuiButton({ 8, (float)sby, 110, 26 }, "#22# Brush"))
    {
        ed->toolMode = (ed->toolMode == TOOL_BRUSH) ? TOOL_NONE : TOOL_BRUSH;

        if (ed->toolMode != TOOL_SELECT)
        {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
        }
    }

    GuiSetState(ed->brushSolid ? STATE_PRESSED : STATE_NORMAL);

    if (GuiButton({ 122, (float)sby, 30, 26 }, "COL"))
        ed->brushSolid = !ed->brushSolid;

    GuiSetState(STATE_NORMAL);

    GuiSetState(ed->toolMode == TOOL_ERASER ? STATE_PRESSED : STATE_NORMAL);

    if (GuiButton({ 8, (float)sby + 30, 144, 26 }, "#143# Eraser"))
    {
        ed->toolMode = (ed->toolMode == TOOL_ERASER) ? TOOL_NONE : TOOL_ERASER;

        if (ed->toolMode != TOOL_SELECT)
        {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
        }
    }

    GuiSetState(ed->toolMode == TOOL_SELECT ? STATE_PRESSED : STATE_NORMAL);

    if (GuiButton({ 8, (float)sby + 60, 144, 26 }, "#32# Select"))
    {
        ed->toolMode = (ed->toolMode == TOOL_SELECT) ? TOOL_NONE : TOOL_SELECT;

        if (ed->toolMode != TOOL_SELECT)
        {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->pasteMode = false;
        }

        ed->shapeFirstClick = false;
    }

    GuiSetState(ed->toolMode == TOOL_LINE ? STATE_PRESSED : STATE_NORMAL);

    if (GuiButton({ 8, (float)sby + 90, 144, 26 }, "Line"))
    {
        ed->toolMode = (ed->toolMode == TOOL_LINE) ? TOOL_NONE : TOOL_LINE;

        if (ed->toolMode != TOOL_SELECT)
        {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
            ClearPreview();
        }
    }

    GuiSetState(ed->toolMode == TOOL_RECT ? STATE_PRESSED : STATE_NORMAL);

    if (GuiButton({ 8, (float)sby + 120, 144, 26 }, "Rectangle"))
    {
        ed->toolMode = (ed->toolMode == TOOL_RECT) ? TOOL_NONE : TOOL_RECT;

        if (ed->toolMode != TOOL_SELECT)
        {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
            ClearPreview();
        }
    }

    GuiSetState(ed->toolMode == TOOL_CIRCLE ? STATE_PRESSED : STATE_NORMAL);

    if (GuiButton({ 8, (float)sby + 150, 144, 26 }, "Circle"))
    {
        ed->toolMode = (ed->toolMode == TOOL_CIRCLE) ? TOOL_NONE : TOOL_CIRCLE;

        if (ed->toolMode != TOOL_SELECT)
        {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
            ClearPreview();
        }
    }

    GuiSetState(STATE_NORMAL);

    int infoY = sby + 182;

    if (ed->toolMode == TOOL_SELECT && ed->sel.count > 0 && !ed->pasteMode)
    {
        const char* info = ed->sel.isMoving
            ? TextFormat("Moving %d\nDx:%d Dy:%d", ed->sel.count, ed->sel.moveDeltaX, ed->sel.moveDeltaY)
            : TextFormat("%d selected\nC=copy R=rot\nCtrl<>=mirr\nDel=erase", ed->sel.count);

        DrawText(info, 10, infoY, 9, { 0, 200, 255, 255 });
        infoY += 56;
    }

    if (ed->pasteMode)
    {
        DrawText("PASTE MODE\nClick to place\nR=rot Esc=cancel", 10, infoY, 9, { 255, 200, 0, 255 });
        infoY += 42;
    }

    bool isShapeTool = (ed->toolMode == TOOL_LINE || ed->toolMode == TOOL_RECT || ed->toolMode == TOOL_CIRCLE);

    if (isShapeTool && ed->shapeFirstClick)
    {
        DrawText(
            TextFormat("Start: %d,%d\nClick 2nd point", ed->shapeStartLX, ed->shapeStartLY),
            10, infoY, 9, { 255, 200, 0, 255 }
        );
    }
}

void DrawColorPanel(EditorState* ed, Vector2 mousePos, int sw, int sh, int logicX, int logicY)
{
    const int COLOR_PANEL_H = 16 + 144 + 4 + 16 + 20 + 48 + 12 + 10;
    int panelTop = sh - 50 - COLOR_PANEL_H;

    DrawRectangle(0, panelTop, 160, COLOR_PANEL_H, { 30, 30, 35, 245 });
    DrawLine(0, panelTop, 160, panelTop, { 80, 80, 80, 200 });
    DrawText("Brush Color", 8, panelTop + 4, 9, { 160, 160, 200, 255 });

    int py = panelTop + 16;
    Rectangle cpRect = { 8, (float)py, 120, 144 };
    Color newC = ed->brushColor;
    GuiColorPicker(cpRect, NULL, &newC);

    if (newC.r != ed->brushColor.r || newC.g != ed->brushColor.g || newC.b != ed->brushColor.b)
    {
        ed->brushColor = newC;
        ed->brushColor.a = 255;
        SyncBufsFromColor(ed);
    }

    py += 148;

    DrawText("Hex:", 8, py, 9, GRAY);
    Rectangle hexR = { 34, (float)py - 2, 80, 16 };
    bool hexHov = CheckCollisionPointRec(mousePos, hexR);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        ed->hexEditing = hexHov;

        if (hexHov)
        {
            ed->rEdit = false;
            ed->gEdit = false;
            ed->bEdit = false;
        }
    }

    DrawRectangleLinesEx(hexR, 1, ed->hexEditing ? Color{ 0, 200, 255, 255 } : Color{ 80, 80, 80, 255 });
    DrawText(ed->hexInputBuf, (int)hexR.x + 3, (int)hexR.y + 3, 9, WHITE);

    if (ed->hexEditing)
    {
        int key;
        while ((key = GetCharPressed()) > 0)
        {
            int hl = (int)strlen(ed->hexInputBuf);

            if (hl < 7)
            {
                ed->hexInputBuf[hl] = (char)key;
                ed->hexInputBuf[hl + 1] = '\0';
            }
        }

        if (IsKeyPressed(KEY_BACKSPACE))
        {
            int hl = (int)strlen(ed->hexInputBuf);

            if (hl > 0)
                ed->hexInputBuf[--hl] = '\0';
        }

        Color parsed;

        if (TryParseHex(ed->hexInputBuf, &parsed))
        {
            ed->brushColor = parsed;
            ed->brushColor.a = 255;
            SyncBufsFromColor(ed);
        }

        if (IsKeyPressed(KEY_ENTER))
            ed->hexEditing = false;
    }

    if (GuiButton({ 118, (float)py - 2, 34, 17 }, "Rst"))
    {
        ed->brushColor = { 200, 200, 200, 255 };
        SyncBufsFromColor(ed);
    }

    py += 20;

    auto drawRGBField = [&](int fy, const char* label, char* buf, bool& editing, Color lc, uint8_t& channel)
        {
            DrawText(label, 8, fy, 9, lc);
            Rectangle fr = { 22, (float)fy - 1, 50, 14 };
            bool hov = CheckCollisionPointRec(mousePos, fr);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                if (hov)
                {
                    editing = true;
                    ed->hexEditing = false;
                    ed->rEdit = false;
                    ed->gEdit = false;
                    ed->bEdit = false;
                    editing = true;
                }
                else
                {
                    editing = false;
                }
            }

            DrawRectangleLinesEx(fr, 1, editing ? lc : Color{ 80, 80, 80, 255 });
            DrawText(buf, (int)fr.x + 2, (int)fr.y + 2, 9, WHITE);

            if (editing)
            {
                int key;
                while ((key = GetCharPressed()) > 0)
                {
                    int bl = (int)strlen(buf);

                    if (bl < 3 && key >= '0' && key <= '9')
                    {
                        buf[bl] = (char)key;
                        buf[bl + 1] = '\0';
                    }
                }

                if (IsKeyPressed(KEY_BACKSPACE))
                {
                    int bl = (int)strlen(buf);

                    if (bl > 0)
                        buf[--bl] = '\0';
                }

                if (IsKeyPressed(KEY_ENTER))
                    editing = false;

                int v = Clamp(atoi(buf), 0, 255);
                channel = (uint8_t)v;
                ed->brushColor.a = 255;
                SyncBufsFromColor(ed);
            }
        };

    drawRGBField(py, "R:", ed->rBuf, ed->rEdit, { 255, 80,  80,  255 }, ed->brushColor.r);
    drawRGBField(py + 16, "G:", ed->gBuf2, ed->gEdit, { 80,  220, 80,  255 }, ed->brushColor.g);
    drawRGBField(py + 32, "B:", ed->bBuf, ed->bEdit, { 80,  150, 255, 255 }, ed->brushColor.b);

    if (!ed->rEdit && !ed->gEdit && !ed->bEdit && !ed->hexEditing)
    {
        int rv = Clamp(atoi(ed->rBuf), 0, 255);
        int gv = Clamp(atoi(ed->gBuf2), 0, 255);
        int bv = Clamp(atoi(ed->bBuf), 0, 255);

        bool changed = (
            (uint8_t)rv != ed->brushColor.r
            || (uint8_t)gv != ed->brushColor.g
            || (uint8_t)bv != ed->brushColor.b
            );

        if (changed)
        {
            ed->brushColor.r = (uint8_t)rv;
            ed->brushColor.g = (uint8_t)gv;
            ed->brushColor.b = (uint8_t)bv;
            ed->brushColor.a = 255;
            SyncBufsFromColor(ed);
        }
    }

    DrawRectangle(76, py, 40, 48, ed->brushColor);
    DrawRectangleLinesEx({ 76, (float)py, 40, 48 }, 1, LIGHTGRAY);
    py += 52;

    DrawText(TextFormat("Pos: %d, %d", logicX, logicY), 8, py, 9, { 120, 140, 180, 255 });
}


GameScene DrawBottomBar(EditorState* ed, int sw, int sh)
{
    GameScene next = SCENE_WHITE;

    DrawRectangle(0, sh - 50, sw, 50, { 35, 35, 35, 255 });
    DrawLine(0, sh - 50, sw, sh - 50, GRAY);

    float cx = (float)sw / 2;

    if (GuiButton({ cx - 110, (float)sh - 40, 100, 30 }, "EDITOR"))
        next = SCENE_WHITE;

    if (GuiButton({ cx + 10, (float)sh - 40, 100, 30 }, "MAP"))
        next = SCENE_BLUE;

    if (ed->status.timer > 0)
    {
        float alpha = Clamp(ed->status.timer / 0.5f, 0.0f, 1.0f);
        int tw2 = MeasureText(ed->status.text, 12);
        int bx = sw / 2 - tw2 / 2 - 8;
        int by = sh - 90;

        DrawRectangle(bx, by, tw2 + 16, 22, Fade({ 20, 20, 20, 220 }, alpha));
        DrawText(ed->status.text, bx + 8, by + 5, 12, Fade(ed->status.color, alpha));
    }

    //DrawFPS(sw - 80, sh - 35);

    return next;
}

void DrawContextMenu(EditorState* ed, DynamicMap* map, Vector2 mousePos)
{
    if (!ed->menu.active)
        return;

    float mx2 = ed->menu.screenPosition.x + 15;
    float my2 = ed->menu.screenPosition.y + 15;

    if (ed->menu.editMode == 1)
    {
        DrawRectangleRec({ mx2, my2, 165, 230 }, { 230, 230, 230, 255 });
        Cell& tc = map->data[ed->menu.targetX][ed->menu.targetY];

        GuiColorPicker({ mx2 + 10, my2 + 10, 140, 140 }, NULL, &tc.color);

        if (GuiButton({ mx2 + 10, my2 + 155, 80, 22 }, "Reset"))
            tc.color = LIGHTGRAY;

        if (GuiButton({ mx2 + 95, my2 + 155, 60, 22 }, "Done"))
            ed->menu.editMode = 0;

        DrawText("Hex:", mx2 + 10, my2 + 182, 9, GRAY);

        static char menuHexBuf[16] = "";
        static bool menuHexEdit = false;
        Rectangle mhR = { mx2 + 35, my2 + 180, 80, 15 };
        bool mhHov = CheckCollisionPointRec(mousePos, mhR);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            menuHexEdit = mhHov;

        DrawRectangleLinesEx(mhR, 1, menuHexEdit ? Color{ 0, 200, 255, 255 } : Color{ 180, 180, 180, 255 });
        DrawText(menuHexBuf, (int)mhR.x + 2, (int)mhR.y + 2, 9, BLACK);

        if (menuHexEdit)
        {
            int key;
            while ((key = GetCharPressed()) > 0)
            {
                int hl = (int)strlen(menuHexBuf);

                if (hl < 7)
                {
                    menuHexBuf[hl] = (char)key;
                    menuHexBuf[hl + 1] = '\0';
                }
            }

            if (IsKeyPressed(KEY_BACKSPACE))
            {
                int hl = (int)strlen(menuHexBuf);

                if (hl > 0)
                    menuHexBuf[--hl] = '\0';
            }

            Color parsed;

            if (TryParseHex(menuHexBuf, &parsed))
                tc.color = parsed;
        }

        sprintf(menuHexBuf, "#%02X%02X%02X", tc.color.r, tc.color.g, tc.color.b);
        return;
    }

    Cell& tc = map->data[ed->menu.targetX][ed->menu.targetY];
    bool cellVisible = tc.height > 0.0f;
    bool cellExists = (tc.color.r != 80 || tc.color.g != 80 || tc.color.b != 80 || tc.textureIndex >= 0);

    float mh2;
    if (ed->menu.editMode == 0)
        mh2 = !cellExists ? 105.0f : (!cellVisible ? 115.0f : 210.0f);
    else if (ed->menu.editMode == 2)
        mh2 = 140.0f;
    else if (ed->menu.editMode == 3)
        mh2 = 125.0f;
    else
        mh2 = 80.0f;

    DrawRectangleRec({ mx2, my2, 130, mh2 }, { 45, 45, 45, 255 });
    DrawRectangleLinesEx({ mx2, my2, 130, mh2 }, 1, LIGHTGRAY);

    if (ed->menu.editMode == 0)
    {
        DrawText(
            TextFormat("[%d,%d]", ed->menu.targetX - map->offsetX, ed->menu.targetY - map->offsetY),
            mx2 + 10, my2 + 8, 10, SKYBLUE
        );

        if (!cellExists)
        {
            DrawText("EMPTY CELL", mx2 + 10, my2 + 24, 9, { 120, 120, 120, 255 });
            if (GuiButton({ mx2 + 10, my2 + 44, 110, 25 }, "Set Texture")) ed->menu.editMode = 3;
            if (GuiButton({ mx2 + 10, my2 + 74, 110, 20 }, "Close"))       ed->menu.active = false;
        }
        else if (!cellVisible)
        {
            DrawText("HIDDEN", mx2 + 10, my2 + 22, 9, { 180, 180, 80, 255 });

            const char* fullTex = (tc.textureIndex >= 0 && tc.textureIndex < gTextureCount)
                ? GetFileName(gTextures[tc.textureIndex].path)
                : nullptr;

            static char shortTex[17];
            if (fullTex)
            {
                strncpy(shortTex, fullTex, 15);
                shortTex[15] = '\0';
            }

            const char* texLabel = fullTex ? TextFormat("TEX: %s", shortTex) : "TEX: none";
            Color texColor = tc.textureIndex >= 0 ? Color{ 80, 220, 80, 255 } : Color{ 120, 120, 120, 255 };
            DrawText(texLabel, mx2 + 10, my2 + 33, 9, texColor);

            if (GuiButton({ mx2 + 10, my2 + 48, 110, 25 }, "Texture")) ed->menu.editMode = 3;
            if (GuiButton({ mx2 + 10, my2 + 78, 110, 20 }, "Close"))   ed->menu.active = false;
        }
        else
        {
            DrawText("VISIBLE", mx2 + 10, my2 + 22, 9, { 80, 220, 80, 255 });
            DrawText(
                tc.solid ? "COLLISION: ON" : "COLLISION: OFF",
                mx2 + 10, my2 + 33, 9,
                tc.solid ? Color{ 120, 120, 120, 255 } : Color{ 255, 80, 80, 255 }
            );

            const char* fullTex2 = (tc.textureIndex >= 0 && tc.textureIndex < gTextureCount)
                ? GetFileName(gTextures[tc.textureIndex].path)
                : nullptr;

            static char shortTex2[17];
            if (fullTex2)
            {
                strncpy(shortTex2, fullTex2, 15);
                shortTex2[15] = '\0';
            }

            const char* texLabel = fullTex2 ? TextFormat("TEX: %s", shortTex2) : "TEX: none";
            Color texColor = tc.textureIndex >= 0 ? Color{ 80, 220, 80, 255 } : Color{ 120, 120, 120, 255 };
            DrawText(texLabel, mx2 + 10, my2 + 44, 9, texColor);

            if (GuiButton({ mx2 + 10, my2 + 58,  110, 25 }, "Set Color"))     ed->menu.editMode = 1;
            if (GuiButton({ mx2 + 10, my2 + 88,  110, 25 }, "Set Collision")) ed->menu.editMode = 4;
            if (GuiButton({ mx2 + 10, my2 + 118, 110, 25 }, "Set Texture"))   ed->menu.editMode = 3;
            if (GuiButton({ mx2 + 10, my2 + 155, 110, 20 }, "Close"))         ed->menu.active = false;
        }
    }
    else if (ed->menu.editMode == 3)
    {
        DrawText("Texture", mx2 + 10, my2 + 8, 10, YELLOW);

        if (tc.textureIndex >= 0 && tc.textureIndex < gTextureCount)
        {
            const char* fullName = GetFileName(gTextures[tc.textureIndex].path);
            static char shortName[17];
            strncpy(shortName, fullName, 15);
            shortName[15] = '\0';
            DrawText(TextFormat("Tex: %s", shortName), mx2 + 10, my2 + 22, 9, GREEN);
        }
        else
        {
            DrawText("No texture", mx2 + 10, my2 + 22, 9, GRAY);
        }

        if (GuiButton({ mx2 + 10, my2 + 36, 110, 25 }, "Choose File..."))
        {
            std::string path = OpenFileDialog();

            if (!path.empty())
            {
                int idx = LoadOrFindTexture(path.c_str());

                if (idx >= 0)
                    tc.textureIndex = idx;
            }
        }

        if (GuiButton({ mx2 + 10, my2 + 65, 110, 25 }, "Delete Texture"))
            tc.textureIndex = -1;

        if (GuiButton({ mx2 + 10, my2 + 95, 110, 20 }, "Back"))
            ed->menu.editMode = 0;
    }
    else if (ed->menu.editMode == 4)
    {
        DrawText("Collision", mx2 + 10, my2 + 8, 10, { 255, 80, 80, 255 });

        DrawText(
            tc.solid ? "Collision: ON" : "Collision: OFF",
            mx2 + 10, my2 + 28, 10,
            tc.solid ? LIGHTGRAY : Color{ 255, 80, 80, 255 }
        );

        if (GuiButton({ mx2 + 10, my2 + 44, 110, 22 }, tc.solid ? "Disable" : "Enable"))
            tc.solid = !tc.solid;

        if (GuiButton({ mx2 + 10, my2 + 70, 110, 18 }, "Back"))
            ed->menu.editMode = 0;
    }
}

void DrawNewMapConfirm(EditorState* ed, DynamicMap* map, int sw, int sh)
{
    if (!ed->showNewConfirm)
        return;

    int dw = 280, dh = 100;
    int dx = sw / 2 - dw / 2;
    int dy = sh / 2 - dh / 2;

    DrawRectangle(dx, dy, dw, dh, { 40, 40, 40, 245 });
    DrawRectangleLinesEx({ (float)dx, (float)dy, (float)dw, (float)dh }, 1, LIGHTGRAY);
    DrawText("New map? Unsaved changes will be lost.", dx + 14, dy + 14, 10, LIGHTGRAY);

    if (GuiButton({ (float)dx + 20, (float)dy + 55, 110, 28 }, "#8# Yes, new map"))
    {
        FreeDynamicMap(map);
        *map = InitDynamicMap(50, 50);

        FreeSelectionGrid(&ed->selGrid);
        ed->selGrid = InitSelectionGrid(map->width, map->height);
        ClearSelection(&ed->selGrid, &ed->sel);
        ClearUndoStack(&ed->undoStack);
        PushUndo(&ed->undoStack, map);

        ed->pasteMode = false;
        ed->shapeFirstClick = false;
        ClearPreview();
        ed->camera2D.target = { 0, 0 };
        ed->showNewConfirm = false;
        ed->menu.active = false;

        SetStatus(&ed->status, "New map created", GREEN);
        MarkMapDirty(ed);
    }

    if (GuiButton({ (float)dx + 150, (float)dy + 55, 110, 28 }, "Cancel"))
        ed->showNewConfirm = false;
}