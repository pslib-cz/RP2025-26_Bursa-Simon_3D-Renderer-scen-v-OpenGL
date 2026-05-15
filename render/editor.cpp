#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"
#include "editor.h"
#include "editor_draw.h"
#include "editor_input.h"
#include "map.h"
#include "preview.h"
#include "utils.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

void InitEditor(EditorState* ed, DynamicMap* map)
{
    ed->toolMode = TOOL_NONE;
    ed->mirrorMode = MIRROR_NONE;
    ed->menu = { false, {0,0}, {0,0}, 0, 0, 0 };
    ed->status = { "", 0.0f, WHITE };
    ed->selGrid = InitSelectionGrid(map->width, map->height);
    ed->sel = { nullptr, nullptr, nullptr, 0, 0, false, {0,0}, {0,0}, false, {0,0}, 0, 0 };
    ed->clipboard = { nullptr, nullptr, nullptr, 0, 0, 0 };

    InitUndoStack(&ed->undoStack);

    ed->camera2D = {};
    ed->camera2D.target = { 0, 0 };
    ed->camera2D.offset = { 600.0f, 400.0f };
    ed->camera2D.zoom = 1.0f;

    ed->brushColor = { 200, 200, 200, 255 };
    ed->brushSolid = true;
    ed->shapeFirstClick = false;
    ed->shapeStartLX = 0;
    ed->shapeStartLY = 0;
    ed->pasteMode = false;
    ed->pasteLogicX = 0;
    ed->pasteLogicY = 0;
    ed->showNewConfirm = false;
    ed->isDragging = false;
    ed->strokeStarted = false;
    ed->editingFilename = false;

    strncpy(ed->saveFile, "map.bin", 255);

    sprintf(ed->hexInputBuf, "#%02X%02X%02X", ed->brushColor.r, ed->brushColor.g, ed->brushColor.b);
    ed->hexEditing = false;

    sprintf(ed->rBuf, "%d", ed->brushColor.r);
    sprintf(ed->gBuf2, "%d", ed->brushColor.g);
    sprintf(ed->bBuf, "%d", ed->brushColor.b);

    ed->rEdit = false;
    ed->gEdit = false;
    ed->bEdit = false;

    ed->mapRenderTex = { 0 };
    ed->mapDirty = true;
    ed->lastCamTarget = { 0, 0 };
    ed->lastCamZoom = 0.0f;
    ed->lastOffsetX = 0;
    ed->lastOffsetY = 0;

    InitMapRenderTexture(ed, GetScreenWidth(), GetScreenHeight());
}

void FreeEditor(EditorState* ed)
{
    free(ed->sel.cells);
    free(ed->sel.srcX);
    free(ed->sel.srcY);
    FreeClipboard(&ed->clipboard);
    FreeSelectionGrid(&ed->selGrid);
    ClearUndoStack(&ed->undoStack);

    if (ed->mapRenderTex.id != 0)
        UnloadRenderTexture(ed->mapRenderTex);
}

void SyncBufsFromColor(EditorState* ed)
{
    sprintf(ed->hexInputBuf, "#%02X%02X%02X", ed->brushColor.r, ed->brushColor.g, ed->brushColor.b);
    sprintf(ed->rBuf, "%d", ed->brushColor.r);
    sprintf(ed->gBuf2, "%d", ed->brushColor.g);
    sprintf(ed->bBuf, "%d", ed->brushColor.b);
}

GameScene UpdateDrawEditor(EditorState* ed, DynamicMap* map, Vector2 rcWorldPos, float rcAngle)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    Vector2 mousePos = GetMousePosition();
    Vector2 worldMouse = GetScreenToWorld2D(mousePos, ed->camera2D);

    int logicX = (int)floor(worldMouse.x / CELL_BASE_SIZE);
    int logicY = (int)floor(worldMouse.y / CELL_BASE_SIZE);

    float wheel = GetMouseWheelMove();

    bool mouseOnUI = (
        mousePos.x < 160 ||
        mousePos.y < 46 ||
        mousePos.y > sh - 50
        );

    bool isShapeTool = (
        ed->toolMode == TOOL_LINE ||
        ed->toolMode == TOOL_RECT ||
        ed->toolMode == TOOL_CIRCLE
        );

    // --- Input ---

    ed->status.timer -= GetFrameTime();

    if (wheel != 0 && !mouseOnUI)
        HandleZoom(ed, mousePos, wheel);

    HandleFilenameInput(ed, mousePos);
    HandleUndoRedo(ed, map);
    HandleToolKeys(ed);

    if (!ed->showNewConfirm && !ed->menu.active && !mouseOnUI)
    {
        if (ed->toolMode == TOOL_SELECT)
        {
            HandleSelectTool(ed, map, worldMouse, logicX, logicY);
            HandleCopyPaste(ed, map, logicX, logicY);
            HandleMirror(ed, map);
            HandleRotate(ed, map);
        }
        else if (isShapeTool)
        {
            HandleShapeTools(ed, map, logicX, logicY);
        }
        else
        {
            HandleCanvasTools(ed, map, mouseOnUI, logicX, logicY);
        }
    }

    if (!ed->showNewConfirm)
        HandleContextMenu(ed, map, worldMouse, logicX, logicY);

    HandleMapExpansion(ed, map, sw, sh, wheel);

    // --- Draw ---

    BeginDrawing();
    ClearBackground({ 20, 20, 20, 255 });

    DrawMapCells(ed, map, sw, sh);

    BeginMode2D(ed->camera2D);

    if (ed->sel.isMoving)
        DrawMovingSelection(ed, map);

    if (ed->sel.isDragSelecting)
        DrawDragSelect(ed);

    if (ed->pasteMode)
        DrawPastePreview(ed, logicX, logicY);

    if (isShapeTool && ed->shapeFirstClick)
        DrawShapePreview(ed);

    DrawCursorHighlight(ed, mouseOnUI, logicX, logicY);
    DrawPlayerMarker(ed, rcWorldPos, rcAngle, sw, sh);

    EndMode2D();

    GameScene next = DrawTopBar(ed, map, sw, sh);
    DrawSidebar(ed, map, mousePos, sh, logicX, logicY);
    DrawColorPanel(ed, mousePos, sw, sh, logicX, logicY);

    GameScene bottomScene = DrawBottomBar(ed, sw, sh);
    if (bottomScene != SCENE_WHITE)
        next = bottomScene;

    DrawContextMenu(ed, map, mousePos);
    DrawNewMapConfirm(ed, map, sw, sh);

    EndDrawing();

    return next;
}