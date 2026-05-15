#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "editor.h"
#include "map.h"
#include "preview.h"
#include "utils.h"
#include "textures.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

std::string SaveFileDialog();
std::string OpenFileDialog();

void InitEditor(EditorState* ed, DynamicMap* map) {
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
    PushUndo(&ed->undoStack, map);
}

void FreeEditor(EditorState* ed) {
    free(ed->sel.cells);
    free(ed->sel.srcX);
    free(ed->sel.srcY);
    FreeClipboard(&ed->clipboard);
    FreeSelectionGrid(&ed->selGrid);
    ClearUndoStack(&ed->undoStack);
}

static void SyncBufsFromColor(EditorState* ed) {
    sprintf(ed->hexInputBuf, "#%02X%02X%02X", ed->brushColor.r, ed->brushColor.g, ed->brushColor.b);
    sprintf(ed->rBuf, "%d", ed->brushColor.r);
    sprintf(ed->gBuf2, "%d", ed->brushColor.g);
    sprintf(ed->bBuf, "%d", ed->brushColor.b);
}

GameScene UpdateDrawEditor(EditorState* ed, DynamicMap* map, Vector2 rcWorldPos, float rcAngle) {
    GameScene nextScene = SCENE_WHITE;

    if (IsCursorHidden()) EnableCursor();

    float dt = GetFrameTime();
    if (ed->status.timer > 0) ed->status.timer -= dt;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    Vector2 mousePos = GetMousePosition();
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    const int COLOR_PANEL_H = 16 + 144 + 4 + 16 + 20 + 48 + 12 + 10;
    Rectangle topBar = { 0, 0, (float)sw, 45 };
    Rectangle bottomBar = { 0, (float)sh - 50, (float)sw, 50 };
    Rectangle sidebarArea = { 0, 46, 160, (float)(sh - 96 - COLOR_PANEL_H) };
    Rectangle colorPanelArea = { 0, (float)(sh - 50 - COLOR_PANEL_H), 160, (float)COLOR_PANEL_H };

    bool mouseOnUI = false;
    auto checkRect = [&](Rectangle r) { if (CheckCollisionPointRec(mousePos, r)) mouseOnUI = true; };
    checkRect(topBar); checkRect(bottomBar); checkRect(sidebarArea); checkRect(colorPanelArea);

    if (ed->menu.active) {
        float mw = 0, mh = 0;
        if (ed->menu.editMode == 1) { mw = 165; mh = 230; }
        else if (ed->menu.editMode == 0) { mw = 130; mh = 210; }
        else if (ed->menu.editMode == 2) { mw = 130; mh = 140; }
        else if (ed->menu.editMode == 3) { mw = 130; mh = 80; }
        else if (ed->menu.editMode == 4) { mw = 160; mh = 80; }
        checkRect({ ed->menu.screenPosition.x + 15, ed->menu.screenPosition.y + 15, mw, mh });
    }
    if (ed->showNewConfirm) mouseOnUI = true;

    Rectangle fileInputRect = { 8, 50, 144, 18 };

    float wheel = GetMouseWheelMove();
    if (wheel != 0 && !mouseOnUI) {
        if (ed->menu.active) ed->menu.active = false;
        Vector2 mw2 = GetScreenToWorld2D(mousePos, ed->camera2D);
        ed->camera2D.offset = mousePos; ed->camera2D.target = mw2;
        ed->camera2D.zoom = Clamp(ed->camera2D.zoom + wheel * 0.12f, MAX_UNZOOM, MAX_ZOOM);
    }

    Vector2 worldMouse = GetScreenToWorld2D(mousePos, ed->camera2D);
    int logicX = (int)floor(worldMouse.x / CELL_BASE_SIZE);
    int logicY = (int)floor(worldMouse.y / CELL_BASE_SIZE);

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !mouseOnUI) {
        Vector2 delta = GetMouseDelta();
        if (Vector2Length(delta) > 0.5f) {
            ed->camera2D.target = Vector2Add(ed->camera2D.target, Vector2Scale(delta, -1.0f / ed->camera2D.zoom));
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && (ed->toolMode == TOOL_BRUSH || ed->toolMode == TOOL_ERASER)) {
        ed->toolMode = TOOL_NONE;
    }

    if (!ed->editingFilename && !ed->showNewConfirm) {
        if (ctrl && IsKeyPressed(KEY_Z)) {
            if (UndoStep(&ed->undoStack, map)) {
                FreeSelectionGrid(&ed->selGrid);
                ed->selGrid = InitSelectionGrid(map->width, map->height);
                ClearSelection(&ed->selGrid, &ed->sel);
                SetStatus(&ed->status, "Undo", SKYBLUE);
            }
        }

        if (ctrl && IsKeyPressed(KEY_Y)) {
            if (RedoStep(&ed->undoStack, map)) {
                FreeSelectionGrid(&ed->selGrid);
                ed->selGrid = InitSelectionGrid(map->width, map->height);
                ClearSelection(&ed->selGrid, &ed->sel);
                SetStatus(&ed->status, "Redo", SKYBLUE);
            }
        }

        if (ctrl && IsKeyPressed(KEY_C) && ed->toolMode == TOOL_SELECT && ed->sel.count > 0) {
            CopySelection(&ed->clipboard, &ed->sel, map);
            SetStatus(&ed->status, TextFormat("Copied %d cells", ed->clipboard.count), GREEN);
        }

        if (ctrl && IsKeyPressed(KEY_V) && ed->clipboard.count > 0) {
            ed->toolMode = TOOL_SELECT; ed->pasteMode = false;
            PushUndo(&ed->undoStack, map);
            PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, logicX, logicY);
            RebuildSelectionList(&ed->selGrid, map, &ed->sel);
            SetStatus(&ed->status, "Pasted", GREEN);
        }

        if (IsKeyPressed(KEY_R) && ed->toolMode == TOOL_SELECT) {
            if (ed->pasteMode) {
                RotateClipboardCW(&ed->clipboard);
                SetStatus(&ed->status, "Rotated clipboard", { 255,200,0,255 });
            }
            else if (ed->sel.count > 0) {
                PushUndo(&ed->undoStack, map);
                RotateSelectionCW(map, &ed->selGrid, &ed->sel);
                RebuildSelectionList(&ed->selGrid, map, &ed->sel);
                SetStatus(&ed->status, "Rotated selection", { 255,200,0,255 });
            }
        }

        if (ctrl && IsKeyPressed(KEY_LEFT) && ed->toolMode == TOOL_SELECT && ed->sel.count > 0) {
            CopySelection(&ed->clipboard, &ed->sel, map);
            MirrorClipboardX(&ed->clipboard);
            int minLX = ed->sel.srcX[0] - map->offsetX;
            int minLY = ed->sel.srcY[0] - map->offsetY;
            for (int i = 1; i < ed->sel.count; i++) {
                if (ed->sel.srcX[i] - map->offsetX < minLX) minLX = ed->sel.srcX[i] - map->offsetX;
                if (ed->sel.srcY[i] - map->offsetY < minLY) minLY = ed->sel.srcY[i] - map->offsetY;
            }
            int destLX = minLX - ed->clipboard.boundsW;
            int destLY = minLY;
            EnsureMapContainsLogicRect(map, &ed->selGrid, destLX, destLY, destLX + ed->clipboard.boundsW - 1, destLY + ed->clipboard.boundsH - 1);
            PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, destLX, destLY);
            RebuildSelectionList(&ed->selGrid, map, &ed->sel);
            FreeClipboard(&ed->clipboard);
            SetStatus(&ed->status, "Mirrored to the left", { 255,200,0,255 });
        }

        if (ctrl && IsKeyPressed(KEY_RIGHT) && ed->toolMode == TOOL_SELECT && ed->sel.count > 0) {
            CopySelection(&ed->clipboard, &ed->sel, map);
            MirrorClipboardX(&ed->clipboard);
            int minLY = ed->sel.srcY[0] - map->offsetY;
            int maxLX = ed->sel.srcX[0] - map->offsetX;
            for (int i = 1; i < ed->sel.count; i++) {
                if (ed->sel.srcX[i] - map->offsetX > maxLX) maxLX = ed->sel.srcX[i] - map->offsetX;
                if (ed->sel.srcY[i] - map->offsetY < minLY) minLY = ed->sel.srcY[i] - map->offsetY;
            }
            int destLX = maxLX + 1;
            int destLY = minLY;
            EnsureMapContainsLogicRect(map, &ed->selGrid, destLX, destLY, destLX + ed->clipboard.boundsW - 1, destLY + ed->clipboard.boundsH - 1);
            PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, destLX, destLY);
            RebuildSelectionList(&ed->selGrid, map, &ed->sel);
            FreeClipboard(&ed->clipboard);
            SetStatus(&ed->status, "Mirrored to the right", { 255,200,0,255 });
        }

        if (ctrl && IsKeyPressed(KEY_UP) && ed->toolMode == TOOL_SELECT && ed->sel.count > 0) {
            CopySelection(&ed->clipboard, &ed->sel, map);
            MirrorClipboardY(&ed->clipboard);
            int minLX = ed->sel.srcX[0] - map->offsetX;
            int minLY = ed->sel.srcY[0] - map->offsetY;
            for (int i = 1; i < ed->sel.count; i++) {
                if (ed->sel.srcX[i] - map->offsetX < minLX) minLX = ed->sel.srcX[i] - map->offsetX;
                if (ed->sel.srcY[i] - map->offsetY < minLY) minLY = ed->sel.srcY[i] - map->offsetY;
            }
            int destLX = minLX;
            int destLY = minLY - ed->clipboard.boundsH;
            EnsureMapContainsLogicRect(map, &ed->selGrid, destLX, destLY, destLX + ed->clipboard.boundsW - 1, destLY + ed->clipboard.boundsH - 1);
            PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, destLX, destLY);
            RebuildSelectionList(&ed->selGrid, map, &ed->sel);
            FreeClipboard(&ed->clipboard);
            SetStatus(&ed->status, "Mirror up", { 255,200,0,255 });
        }

        if (ctrl && IsKeyPressed(KEY_DOWN) && ed->toolMode == TOOL_SELECT && ed->sel.count > 0) {
            CopySelection(&ed->clipboard, &ed->sel, map);
            MirrorClipboardY(&ed->clipboard);
            int minLX = ed->sel.srcX[0] - map->offsetX;
            int maxLY = ed->sel.srcY[0] - map->offsetY;
            for (int i = 1; i < ed->sel.count; i++) {
                if (ed->sel.srcX[i] - map->offsetX < minLX) minLX = ed->sel.srcX[i] - map->offsetX;
                if (ed->sel.srcY[i] - map->offsetY > maxLY) maxLY = ed->sel.srcY[i] - map->offsetY;
            }
            int destLX = minLX;
            int destLY = maxLY + 1;
            EnsureMapContainsLogicRect(map, &ed->selGrid, destLX, destLY, destLX + ed->clipboard.boundsW - 1, destLY + ed->clipboard.boundsH - 1);
            PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, destLX, destLY);
            RebuildSelectionList(&ed->selGrid, map, &ed->sel);
            FreeClipboard(&ed->clipboard);
            SetStatus(&ed->status, "Mirror down", { 255,200,0,255 });
        }
    }

    if (ed->showNewConfirm) {
        if (IsKeyPressed(KEY_ESCAPE)) ed->showNewConfirm = false;
    }

    if (IsKeyPressed(KEY_B)) {
        ed->toolMode = (ed->toolMode == TOOL_BRUSH) ? TOOL_NONE : TOOL_BRUSH;
        if (ed->toolMode != TOOL_SELECT) { ClearSelection(&ed->selGrid, &ed->sel); ed->shapeFirstClick = false; ClearPreview(); }
        SetStatus(&ed->status, ed->toolMode == TOOL_BRUSH ? "Brush" : "No tool", LIGHTGRAY);
    }
    if (IsKeyPressed(KEY_E)) {
        ed->toolMode = (ed->toolMode == TOOL_ERASER) ? TOOL_NONE : TOOL_ERASER;
        if (ed->toolMode != TOOL_SELECT) { ClearSelection(&ed->selGrid, &ed->sel); ed->shapeFirstClick = false; ClearPreview(); }
        SetStatus(&ed->status, ed->toolMode == TOOL_ERASER ? "Eraser" : "No tool", LIGHTGRAY);
    }
    if (IsKeyPressed(KEY_S) && !ctrl) {
        ed->toolMode = (ed->toolMode == TOOL_SELECT) ? TOOL_NONE : TOOL_SELECT;
        if (ed->toolMode != TOOL_SELECT) { ClearSelection(&ed->selGrid, &ed->sel); ed->pasteMode = false; }
        ed->shapeFirstClick = false; ClearPreview();
        SetStatus(&ed->status, ed->toolMode == TOOL_SELECT ? "Select" : "No tool", LIGHTGRAY);
    }
    if (IsKeyPressed(KEY_L)) {
        ed->toolMode = (ed->toolMode == TOOL_LINE) ? TOOL_NONE : TOOL_LINE;
        if (ed->toolMode != TOOL_SELECT) { ClearSelection(&ed->selGrid, &ed->sel); ed->shapeFirstClick = false; ClearPreview(); }
        SetStatus(&ed->status, ed->toolMode == TOOL_LINE ? "Line" : "No tool", LIGHTGRAY);
    }

    if (IsKeyPressed(KEY_R) && ed->toolMode != TOOL_SELECT) {
        ed->toolMode = (ed->toolMode == TOOL_RECT) ? TOOL_NONE : TOOL_RECT;
        if (ed->toolMode != TOOL_SELECT) { ClearSelection(&ed->selGrid, &ed->sel); ed->shapeFirstClick = false; ClearPreview(); }
        SetStatus(&ed->status, ed->toolMode == TOOL_RECT ? "Rectangle" : "No tool", LIGHTGRAY);
    }
    if (IsKeyPressed(KEY_C) && !ctrl) {
        ed->toolMode = (ed->toolMode == TOOL_CIRCLE) ? TOOL_NONE : TOOL_CIRCLE;
        if (ed->toolMode != TOOL_SELECT) { ClearSelection(&ed->selGrid, &ed->sel); ed->shapeFirstClick = false; ClearPreview(); }
        SetStatus(&ed->status, ed->toolMode == TOOL_CIRCLE ? "Circle" : "No tool", LIGHTGRAY);
    }

    else if (ed->toolMode == TOOL_SELECT && !mouseOnUI) {
        if (ed->pasteMode) {
            ed->pasteLogicX = logicX;
            ed->pasteLogicY = logicY;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                PushUndo(&ed->undoStack, map);
                PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, ed->pasteLogicX, ed->pasteLogicY);
                RebuildSelectionList(&ed->selGrid, map, &ed->sel);
                ed->pasteMode = false;
                SetStatus(&ed->status, "Pasted", GREEN);
            }

            if (IsKeyPressed(KEY_ESCAPE)) ed->pasteMode = false;
        }
        else {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int ax = logicX + map->offsetX;
                int ay = logicY + map->offsetY;

                bool onSel = (ax >= 0 && ax < ed->selGrid.width && ay >= 0 && ay < ed->selGrid.height && ed->selGrid.data[ax][ay]);

                if (onSel && ed->sel.count > 0) {
                    PushUndo(&ed->undoStack, map);
                    ed->sel.isMoving = true;
                    ed->sel.moveStartWorld = worldMouse;
                    ed->sel.moveDeltaX = ed->sel.moveDeltaY = 0;
                }
                else {
                    if (!IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT)) {
                        ClearSelection(&ed->selGrid, &ed->sel);
                        ed->sel.isDragSelecting = true;
                        ed->sel.dragStart = ed->sel.dragEnd = worldMouse;
                    }
                }
            }

            if (ed->sel.isDragSelecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                ed->sel.dragEnd = worldMouse;
            }

            if (ed->sel.isMoving && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float ddx = worldMouse.x - ed->sel.moveStartWorld.x, ddy = worldMouse.y - ed->sel.moveStartWorld.y;
                ed->sel.moveDeltaX = (int)floor(ddx / CELL_BASE_SIZE + 0.5f);
                ed->sel.moveDeltaY = (int)floor(ddy / CELL_BASE_SIZE + 0.5f);
            }

            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (ed->sel.isDragSelecting) {
                    int lx0 = (int)floor(fminf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE);
                    int ly0 = (int)floor(fminf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE);
                    int lx1 = (int)floor(fmaxf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE);
                    int ly1 = (int)floor(fmaxf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE);
                    if (lx0 == lx1 && ly0 == ly1) {
                        int ax = lx0 + map->offsetX, ay = ly0 + map->offsetY;
                        if (ax >= 0 && ax < ed->selGrid.width && ay >= 0 && ay < ed->selGrid.height) {
                            ed->selGrid.data[ax][ay] = !ed->selGrid.data[ax][ay];
                        }
                    }
                    else {
                        for (int lx = lx0; lx <= lx1; lx++) {
                            for (int ly = ly0; ly <= ly1; ly++) {
                                int ax = lx + map->offsetX;
                                int ay = ly + map->offsetY;
                                if (ax >= 0 && ax < ed->selGrid.width && ay >= 0 && ay < ed->selGrid.height) {
                                    ed->selGrid.data[ax][ay] = true;
                                }
                            }
                        }
                    }
                    RebuildSelectionList(&ed->selGrid, map, &ed->sel); ed->sel.isDragSelecting = false;
                }

                if (ed->sel.isMoving) {
                    CommitMove(map, &ed->selGrid, &ed->sel, ed->sel.moveDeltaX, ed->sel.moveDeltaY);
                    ed->sel.isMoving = false;
                    ed->sel.moveDeltaX = ed->sel.moveDeltaY = 0;
                }
            }

            if (IsKeyPressed(KEY_ESCAPE)) ClearSelection(&ed->selGrid, &ed->sel);

            if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                PushUndo(&ed->undoStack, map);
                for (int i = 0; i < ed->sel.count; i++) map->data[ed->sel.srcX[i]][ed->sel.srcY[i]] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };
                ClearSelection(&ed->selGrid, &ed->sel);
            }
        }
    }
    else if ((ed->toolMode == TOOL_LINE || ed->toolMode == TOOL_RECT || ed->toolMode == TOOL_CIRCLE) && !mouseOnUI) {
        if (ed->shapeFirstClick) {
            if (ed->toolMode == TOOL_LINE) {
                BuildLinePreview(ed->shapeStartLX, ed->shapeStartLY, logicX, logicY);
            }
            else if (ed->toolMode == TOOL_RECT) {
                BuildRectPreview(ed->shapeStartLX, ed->shapeStartLY, logicX, logicY);
            }
            else {
                int rx = abs(logicX - ed->shapeStartLX);
                int ry = abs(logicY - ed->shapeStartLY);
                BuildCirclePreview(ed->shapeStartLX, ed->shapeStartLY, rx, ry);
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (!ed->shapeFirstClick) {
                ed->shapeFirstClick = true;
                ed->shapeStartLX = logicX;
                ed->shapeStartLY = logicY;
                ClearPreview();
            }
            else {
                PushUndo(&ed->undoStack, map);
                Cell c = { 2.5f, ed->brushColor, TYPE_BLOCK, ed->brushSolid, -1 };
                CommitPreview(map, c);
                if (ed->toolMode == TOOL_LINE) {
                    ed->shapeStartLX = logicX;
                    ed->shapeStartLY = logicY;
                    ClearPreview();
                }
                else {
                    ed->shapeFirstClick = false;
                    ClearPreview();
                }
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            ed->shapeFirstClick = false;
            ClearPreview();
        }
    }
    else if (ed->toolMode != TOOL_SELECT && !ed->showNewConfirm && !mouseOnUI) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && ed->toolMode == TOOL_NONE) {
            Vector2 delta = GetMouseDelta();
            if (Vector2Length(delta) > 0.5f) {
                ed->isDragging = true;
                ed->camera2D.target = Vector2Add(ed->camera2D.target, Vector2Scale(delta, -1.0f / ed->camera2D.zoom));
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ed->menu.active && !mouseOnUI) {
            ed->menu.active = false;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !mouseOnUI && ed->toolMode == TOOL_NONE) {
            int ax = logicX + map->offsetX, ay = logicY + map->offsetY;
            if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
                ed->menu.active = true;
                ed->menu.worldPosition = worldMouse;
                ed->menu.screenPosition = GetWorldToScreen2D(worldMouse, ed->camera2D);
                ed->menu.targetX = ax;
                ed->menu.targetY = ay;
                ed->menu.editMode = 0;
            }
        }

        int ax = logicX + map->offsetX, ay = logicY + map->offsetY;
        bool inside = (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height);

        if (inside && !mouseOnUI) {
            if (ed->toolMode == TOOL_ERASER) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    PushUndo(&ed->undoStack, map);
                    ed->strokeStarted = true;
                }
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    map->data[ax][ay] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };
                }
            }
            else if (ed->toolMode == TOOL_BRUSH) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    PushUndo(&ed->undoStack, map);
                    ed->strokeStarted = true;
                }
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    map->data[ax][ay].height = 2.5f;
                    map->data[ax][ay].color = ed->brushColor;
                    map->data[ax][ay].solid = ed->brushSolid;
                }
            }
            else {
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !ed->isDragging && !ed->menu.active && ed->toolMode == TOOL_NONE) {
                    bool hasData = (map->data[ax][ay].color.r != DARKGRAY.r || map->data[ax][ay].color.g != DARKGRAY.g || map->data[ax][ay].color.b != DARKGRAY.b || map->data[ax][ay].textureIndex >= 0);
                    if (hasData) {
                        PushUndo(&ed->undoStack, map);
                        map->data[ax][ay].height = (map->data[ax][ay].height > 0.0f) ? 0.0f : 2.5f;
                    }
                }
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            ed->isDragging = false;
            ed->strokeStarted = false;
        }
    }

    Vector2 vTL = GetScreenToWorld2D({ 0, 45 }, ed->camera2D);
    Vector2 vBR = GetScreenToWorld2D({ (float)sw, (float)sh - 50 }, ed->camera2D);
    int vx0 = (int)floor(vTL.x / CELL_BASE_SIZE);
    int vy0 = (int)floor(vTL.y / CELL_BASE_SIZE);
    int vx1 = (int)floor(vBR.x / CELL_BASE_SIZE);
    int vy1 = (int)floor(vBR.y / CELL_BASE_SIZE);
    int step = 20, thr = 5;

    bool active2D = (ed->isDragging || ed->toolMode != TOOL_NONE || wheel != 0 || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE));

    if (active2D) {
        if (vx0 < -map->offsetX + thr) {
            ExpandMap(map, map->width + step, map->height, step, 0);
            ExpandSelectionGrid(&ed->selGrid, map->width, map->height, step, 0);
        }
        if (vx1 > (map->width - map->offsetX) - thr) {
            ExpandMap(map, map->width + step, map->height, 0, 0);
            ExpandSelectionGrid(&ed->selGrid, map->width, map->height, 0, 0);
        }
        if (vy0 < -map->offsetY + thr) {
            ExpandMap(map, map->width, map->height + step, 0, step);
            ExpandSelectionGrid(&ed->selGrid, map->width, map->height, 0, step);
        }
        if (vy1 > (map->height - map->offsetY) - thr) {
            ExpandMap(map, map->width, map->height + step, 0, 0);
            ExpandSelectionGrid(&ed->selGrid, map->width, map->height, 0, 0);
        }
    }

    BeginDrawing();
    ClearBackground({ 25, 25, 25, 255 });
    BeginMode2D(ed->camera2D);

    if (ed->camera2D.zoom > 0.9f) {
        DrawLineEx({ -200000000, 0 }, { 20000000, 0 }, 2.0f, Fade(RED, 0.5f));
        DrawLineEx({ 0, -20000000 }, { 0, 20000000 }, 2.0f, Fade(GREEN, 0.5f));
    }

    Vector2 tl = GetScreenToWorld2D({ 0, 45 }, ed->camera2D);
    Vector2 br = GetScreenToWorld2D({ (float)sw, (float)sh - 50 }, ed->camera2D);

    int sLX = (int)floor(tl.x / CELL_BASE_SIZE) - 1, sLY = (int)floor(tl.y / CELL_BASE_SIZE) - 1;
    int eLX = (int)floor(br.x / CELL_BASE_SIZE) + 1, eLY = (int)floor(br.y / CELL_BASE_SIZE) + 1;

    for (int ly = sLY; ly <= eLY; ly++) {
        for (int lx = sLX; lx <= eLX; lx++) {
            int ax = lx + map->offsetX;
            int ay = ly + map->offsetY;

            if (ax < 0 || ax >= map->width || ay < 0 || ay >= map->height) continue;

            Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
            bool isSel = ed->selGrid.data[ax][ay];

            if (ed->sel.isMoving && isSel) {
                DrawRectangleRec(r, Fade(map->data[ax][ay].height > 0 ? map->data[ax][ay].color : DARKGRAY, 0.25f));
            }
            else {
                if (map->data[ax][ay].height > 0) {
                    if (map->data[ax][ay].type == TYPE_TEXTURE_ONLY) {
                        DrawRectangleRec(r, Fade(map->data[ax][ay].color, 0.25f));
                        DrawRectangleLinesEx(r, 2.0f / ed->camera2D.zoom, { 255, 220, 0, 255 });
                        float x1 = r.x, y1 = r.y, x2 = r.x + r.width, y2 = r.y + r.height;
                        DrawLine((int)x1, (int)y1, (int)x2, (int)y2, { 255, 220, 0, 160 });
                        DrawLine((int)x2, (int)y1, (int)x1, (int)y2, { 255, 220, 0, 160 });
                        if (ed->camera2D.zoom > 1.2f)
                            DrawText("TEX", (int)r.x + 2, (int)r.y + 12, 8, { 255, 220, 0, 255 });
                    }
                    else {
                        DrawRectangleRec(r, map->data[ax][ay].color);
                        if (!map->data[ax][ay].solid) {
                            DrawRectangleLinesEx(r, 2.0f / ed->camera2D.zoom, { 255, 80, 80, 200 });
                        }
                        else {
                            DrawRectangleRec(r, map->data[ax][ay].color);
                            if (!map->data[ax][ay].solid) {
                                DrawRectangleLinesEx(r, 2.0f / ed->camera2D.zoom, { 255, 80, 80, 200 });
                            }
                        }
                    }
                }
                else {
                    if (ed->camera2D.zoom > 0.9f) {
                        DrawRectangleLinesEx(r, 0.5f, DARKGRAY);
                        if (map->data[ax][ay].color.r != 80 || map->data[ax][ay].color.g != 80 || map->data[ax][ay].color.b != 80) {
                            float x1 = r.x, y1 = r.y, x2 = r.x + r.width, y2 = r.y + r.height;
                            DrawLine((int)x1, (int)y1, (int)x2, (int)y2, Fade(map->data[ax][ay].color, 0.5f));
                            DrawLine((int)x2, (int)y1, (int)x1, (int)y2, Fade(map->data[ax][ay].color, 0.5f));
                        }
                    }
                }
            }
            if (ed->toolMode == TOOL_SELECT && isSel && !ed->sel.isMoving) {
                DrawRectangleLinesEx(r, 2.5f / ed->camera2D.zoom, { 0,220,255,255 });
                DrawRectangleRec(r, { 0,180,255,40 });
            }
            if (ed->toolMode == TOOL_SELECT && isSel && ed->sel.isMoving) {
                DrawRectangleLinesEx(r, 1.5f / ed->camera2D.zoom, Fade(SKYBLUE, 0.5f));
            }
            if (map->data[ax][ay].textureIndex >= 0 && ed->camera2D.zoom > 0.4f) {
                DrawText("T", (int)r.x + 2, (int)r.y + (int)r.height - 12, 10, { 0, 220, 80, 255 });
            }
            if (ed->camera2D.zoom > 1.2f) {
                DrawText(TextFormat("%d,%d", lx, ly), (int)r.x + 2, (int)r.y + 2, 8, Fade(GRAY, 0.4f));
            }
        }
    }
    if (ed->toolMode == TOOL_SELECT && ed->sel.isMoving) {
        for (int i = 0; i < ed->sel.count; i++) {
            int lx = ed->sel.srcX[i] - map->offsetX + ed->sel.moveDeltaX;
            int ly = ed->sel.srcY[i] - map->offsetY + ed->sel.moveDeltaY;

            int nx = ed->sel.srcX[i] + ed->sel.moveDeltaX;
            int ny = ed->sel.srcY[i] + ed->sel.moveDeltaY;
            bool inB = (nx >= 0 && nx < map->width && ny >= 0 && ny < map->height);

            Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
            DrawRectangleRec(r, Fade(ed->sel.cells[i].color, inB ? 0.65f : 0.3f));
            DrawRectangleLinesEx(r, 2.0f / ed->camera2D.zoom, { 0,200,255,200 });
        }
    }
    if (ed->toolMode == TOOL_SELECT && ed->sel.isDragSelecting) {
        int lx0 = (int)floor(fminf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE);
        int ly0 = (int)floor(fminf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE);
        int lx1 = (int)floor(fmaxf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE) + 1;
        int ly1 = (int)floor(fmaxf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE) + 1;
        Rectangle sr = { lx0 * CELL_BASE_SIZE, ly0 * CELL_BASE_SIZE, (lx1 - lx0) * CELL_BASE_SIZE, (ly1 - ly0) * CELL_BASE_SIZE };
        DrawRectangleRec(sr, { 0,160,255,30 }); DrawRectangleLinesEx(sr, 1.5f / ed->camera2D.zoom, { 0,200,255,200 });
    }
    if (ed->pasteMode && ed->clipboard.count > 0) {
        int plx = logicX, ply = logicY;
        for (int i = 0; i < ed->clipboard.count; i++) {
            int lx = plx + ed->clipboard.relX[i], ly = ply + ed->clipboard.relY[i];
            Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
            DrawRectangleRec(r, Fade(ed->clipboard.cells[i].height > 0 ? ed->clipboard.cells[i].color : DARKGRAY, 0.5f));
            DrawRectangleLinesEx(r, 2.0f / ed->camera2D.zoom, { 255,200,0,200 });
        }
    }
    if ((ed->toolMode == TOOL_LINE || ed->toolMode == TOOL_RECT || ed->toolMode == TOOL_CIRCLE) && ed->shapeFirstClick) {
        for (int i = 0; i < gPreviewCount; i++) {
            Rectangle r = { gPreview[i].lx * CELL_BASE_SIZE, gPreview[i].ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
            DrawRectangleRec(r, Fade(ed->brushColor, 0.55f));
            DrawRectangleLinesEx(r, 1.5f / ed->camera2D.zoom, { 255,200,0,200 });
        }
        Rectangle sr = { ed->shapeStartLX * CELL_BASE_SIZE, ed->shapeStartLY * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
        DrawRectangleLinesEx(sr, 3.0f / ed->camera2D.zoom, WHITE);
    }
    if ((ed->toolMode == TOOL_BRUSH || ed->toolMode == TOOL_NONE) && !mouseOnUI) {
        Rectangle r = { logicX * CELL_BASE_SIZE, logicY * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
        DrawRectangleLinesEx(r, 1.5f / ed->camera2D.zoom, Fade(ed->brushColor, 0.7f));
    }
    if (ed->toolMode == TOOL_ERASER && !mouseOnUI) {
        Rectangle r = { logicX * CELL_BASE_SIZE, logicY * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
        DrawRectangleLinesEx(r, 1.5f / ed->camera2D.zoom, Fade(RED, 0.7f));
    }
    EndMode2D();
    {
        Vector2 rcScreen = GetWorldToScreen2D({ rcWorldPos.x * CELL_BASE_SIZE, rcWorldPos.y * CELL_BASE_SIZE }, ed->camera2D);
        float ix = Clamp(rcScreen.x, 160, (float)sw - 10);
        float iy = Clamp(rcScreen.y, 50, (float)sh - 60);
        DrawCircle((int)ix, (int)iy, 7, { 255, 100, 0, 220 });
        DrawCircleLines((int)ix, (int)iy, 7, WHITE);
        float dlen = 15.0f;
        DrawLine((int)ix, (int)iy,
            (int)(ix + cosf(rcAngle) * dlen),
            (int)(iy + sinf(rcAngle) * dlen), { 255,200,0,255 });
        DrawText("3D", (int)ix + 10, (int)iy - 6, 9, { 255,160,80,255 });
    }

    DrawRectangle(0, 0, sw, 45, { 35, 35, 35, 255 });
    DrawLine(0, 45, sw, 45, GRAY);
    DrawText("Raycaster", 24, 14, 16, LIGHTGRAY);
    Rectangle undoBtnRect = { (float)sw - 410, 10, 50, 25 };
    Rectangle redoBtnRect = { (float)sw - 355, 10, 50, 25 };
    Rectangle newBtnRect = { (float)sw - 300, 10, 50, 25 };
    Rectangle saveBtnRect = { (float)sw - 245, 10, 55, 25 };
    Rectangle loadBtnRect = { (float)sw - 185, 10, 55, 25 };
    Rectangle returnBtnRect = { (float)sw - 125, 10, 115, 25 };

    bool canUndo = (ed->undoStack.current > 0), canRedo = (ed->undoStack.current < ed->undoStack.count - 1);

    GuiSetState(canUndo ? STATE_NORMAL : STATE_DISABLED);

    if (GuiButton(undoBtnRect, "#56#Undo")) {
        if (UndoStep(&ed->undoStack, map)) {
            FreeSelectionGrid(&ed->selGrid);
            ed->selGrid = InitSelectionGrid(map->width, map->height);
            ClearSelection(&ed->selGrid, &ed->sel);
            SetStatus(&ed->status, "Undo", SKYBLUE);
        }
    }
    GuiSetState(canRedo ? STATE_NORMAL : STATE_DISABLED);

    if (GuiButton(redoBtnRect, "#57#Redo")) {
        if (RedoStep(&ed->undoStack, map)) {
            FreeSelectionGrid(&ed->selGrid);
            ed->selGrid = InitSelectionGrid(map->width, map->height);
            ClearSelection(&ed->selGrid, &ed->sel);
            SetStatus(&ed->status, "Redo", SKYBLUE);
        }
    }
    GuiSetState(STATE_NORMAL);

    if (GuiButton(newBtnRect, "#8#New")) ed->showNewConfirm = true;

    if (GuiButton(saveBtnRect, "#2#Save")) {
        if (SaveMap(ed->saveFile, map)) {
            SetStatus(&ed->status, TextFormat("Saved: %s", ed->saveFile), GREEN);
        }
        else {
            SetStatus(&ed->status, TextFormat("Save FAILED: %s", ed->saveFile), RED);
        }
    }

    if (GuiButton(loadBtnRect, "#1#Load")) {
        bool ok;
        DynamicMap loaded = LoadMap(ed->saveFile, &ok);

        if (ok) {
            FreeDynamicMap(map);
            *map = loaded;

            FreeSelectionGrid(&ed->selGrid);
            ed->selGrid = InitSelectionGrid(map->width, map->height);

            ClearSelection(&ed->selGrid, &ed->sel);
            ClearUndoStack(&ed->undoStack);
            PushUndo(&ed->undoStack, map);
            SetStatus(&ed->status, TextFormat("Loaded: %s", ed->saveFile), GREEN);
        }
        else {
            SetStatus(&ed->status, TextFormat("Load FAILED: %s", ed->saveFile), RED);
        }
    }

    if (GuiButton(returnBtnRect, "#101#Return to 0,0")) {
        ed->camera2D.target = { 0, 0 };
    }

    DrawRectangle(0, 46, 160, sh - 96, { 30, 30, 30, 200 });
    DrawLine(160, 46, 160, sh - 50, { 80, 80, 80, 200 });
    Color boxBorder = ed->editingFilename ? Color{ 0,200,255,255 } : Color{ 80,80,80,255 };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, fileInputRect))
        ed->editingFilename = true;
    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mousePos, fileInputRect))
        ed->editingFilename = false;

    if (ed->editingFilename) {
        int key;
        while ((key = GetCharPressed()) > 0) {
            int len = (int)strlen(ed->saveFile);
            if (len < 255) { ed->saveFile[len] = (char)key; ed->saveFile[len + 1] = '\0'; }
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            int len = (int)strlen(ed->saveFile);
            if (len > 0) ed->saveFile[--len] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER)) ed->editingFilename = false;
    }

    DrawRectangleLinesEx(fileInputRect, 1, boxBorder);
    DrawText(ed->saveFile, (int)fileInputRect.x + 3, (int)fileInputRect.y + 4, 9, LIGHTGRAY);
    if (ed->editingFilename && ((int)(GetTime() * 2) % 2 == 0)) {
        int cx2 = (int)fileInputRect.x + 3 + MeasureText(ed->saveFile, 9);
        if (cx2 < (int)(fileInputRect.x + fileInputRect.width - 2))
            DrawLine(cx2, (int)fileInputRect.y + 2, cx2, (int)fileInputRect.y + 15, { 0,200,255,255 });
    }
    int sby = 72;

    GuiSetState(ed->toolMode == TOOL_BRUSH ? STATE_PRESSED : STATE_NORMAL);
    if (GuiButton({ 8, (float)sby, 110, 26 }, "#22# Brush")) {
        ed->toolMode = (ed->toolMode == TOOL_BRUSH) ? TOOL_NONE : TOOL_BRUSH;
        if (ed->toolMode != TOOL_SELECT) { ClearSelection(&ed->selGrid, &ed->sel); ed->shapeFirstClick = false; }
    }
    GuiSetState(ed->brushSolid ? STATE_PRESSED : STATE_NORMAL);
    if (GuiButton({ 122, (float)sby, 30, 26 }, "COL")) {
        ed->brushSolid = !ed->brushSolid;
    }
    GuiSetState(STATE_NORMAL);

    GuiSetState(ed->toolMode == TOOL_ERASER ? STATE_PRESSED : STATE_NORMAL);
    if (GuiButton({ 8, (float)sby + 30, 144, 26 }, "#143# Eraser")) {
        ed->toolMode = (ed->toolMode == TOOL_ERASER) ? TOOL_NONE : TOOL_ERASER;
        if (ed->toolMode != TOOL_SELECT) {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
        }
    }

    GuiSetState(ed->toolMode == TOOL_SELECT ? STATE_PRESSED : STATE_NORMAL);
    if (GuiButton({ 8, (float)sby + 60, 144, 26 }, "#32# Select")) {
        ed->toolMode = (ed->toolMode == TOOL_SELECT) ? TOOL_NONE : TOOL_SELECT;
        if (ed->toolMode != TOOL_SELECT) {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->pasteMode = false;
        }
        ed->shapeFirstClick = false;
    }

    GuiSetState(ed->toolMode == TOOL_LINE ? STATE_PRESSED : STATE_NORMAL);
    if (GuiButton({ 8, (float)sby + 90, 144, 26 }, "Line")) {
        ed->toolMode = (ed->toolMode == TOOL_LINE) ? TOOL_NONE : TOOL_LINE;
        if (ed->toolMode != TOOL_SELECT) {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
            ClearPreview();
        }
    }

    GuiSetState(ed->toolMode == TOOL_RECT ? STATE_PRESSED : STATE_NORMAL);
    if (GuiButton({ 8, (float)sby + 120, 144, 26 }, "Rectangle")) {
        ed->toolMode = (ed->toolMode == TOOL_RECT) ? TOOL_NONE : TOOL_RECT;
        if (ed->toolMode != TOOL_SELECT) {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
            ClearPreview();
        }
    }

    GuiSetState(ed->toolMode == TOOL_CIRCLE ? STATE_PRESSED : STATE_NORMAL);
    if (GuiButton({ 8, (float)sby + 150, 144, 26 }, "Circle")) {
        ed->toolMode = (ed->toolMode == TOOL_CIRCLE) ? TOOL_NONE : TOOL_CIRCLE;
        if (ed->toolMode != TOOL_SELECT) {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->shapeFirstClick = false;
            ClearPreview();
        }
    }

    GuiSetState(STATE_NORMAL);
    int infoY = sby + 182;
    if (ed->toolMode == TOOL_SELECT && ed->sel.count > 0 && !ed->pasteMode) {
        const char* info = ed->sel.isMoving
            ? TextFormat("Moving %d\nDx:%d Dy:%d", ed->sel.count, ed->sel.moveDeltaX, ed->sel.moveDeltaY)
            : TextFormat("%d selected\nC=copy R=rot\nCtrl<>=mirr\nDel=erase", ed->sel.count);
        DrawText(info, 10, infoY, 9, { 0,200,255,255 }); infoY += 56;
    }

    if (ed->pasteMode) {
        DrawText("PASTE MODE\nClick to place\nR=rot Esc=cancel", 10, infoY, 9, { 255,200,0,255 });
        infoY += 42;
    }

    if ((ed->toolMode == TOOL_LINE || ed->toolMode == TOOL_RECT || ed->toolMode == TOOL_CIRCLE) && ed->shapeFirstClick)
        DrawText(TextFormat("Start: %d,%d\nClick 2nd point", ed->shapeStartLX, ed->shapeStartLY), 10, infoY, 9, { 255,200,0,255 });
    {
        int panelTop = sh - 50 - COLOR_PANEL_H;
        DrawRectangle(0, panelTop, 160, COLOR_PANEL_H, { 30, 30, 35, 245 });
        DrawLine(0, panelTop, 160, panelTop, { 80,80,80,200 });
        DrawText("Brush Color", 8, panelTop + 4, 9, { 160,160,200,255 });
        int py = panelTop + 16;
        Rectangle cpRect = { 8, (float)py, 120, 144 };
        Color newC = ed->brushColor;
        GuiColorPicker(cpRect, NULL, &newC);
        if (newC.r != ed->brushColor.r || newC.g != ed->brushColor.g || newC.b != ed->brushColor.b) {
            ed->brushColor = newC; ed->brushColor.a = 255; SyncBufsFromColor(ed);
        }
        py += 148;
        DrawText("Hex:", 8, py, 9, GRAY);
        Rectangle hexR = { 34, (float)py - 2, 80, 16 };
        bool hexHov = CheckCollisionPointRec(mousePos, hexR);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            ed->hexEditing = hexHov;
            if (hexHov) { ed->rEdit = false; ed->gEdit = false; ed->bEdit = false; }
        }
        DrawRectangleLinesEx(hexR, 1, ed->hexEditing ? Color{ 0,200,255,255 } : Color{ 80,80,80,255 });
        DrawText(ed->hexInputBuf, (int)hexR.x + 3, (int)hexR.y + 3, 9, WHITE);
        if (ed->hexEditing) {
            int key;
            while ((key = GetCharPressed()) > 0) { int hl = (int)strlen(ed->hexInputBuf); if (hl < 7) { ed->hexInputBuf[hl] = (char)key; ed->hexInputBuf[hl + 1] = '\0'; } }
            if (IsKeyPressed(KEY_BACKSPACE)) { int hl = (int)strlen(ed->hexInputBuf); if (hl > 0) ed->hexInputBuf[--hl] = '\0'; }
            Color parsed; if (TryParseHex(ed->hexInputBuf, &parsed)) { ed->brushColor = parsed; ed->brushColor.a = 255; SyncBufsFromColor(ed); }
            if (IsKeyPressed(KEY_ENTER)) ed->hexEditing = false;
        }
        if (GuiButton({ 118, (float)py - 2, 34, 17 }, "Rst")) { ed->brushColor = { 200,200,200,255 }; SyncBufsFromColor(ed); }
        py += 20;
        auto drawRGBField = [&](int fy, const char* label, char* buf, bool& editing, Color lc, uint8_t& channel) {
            DrawText(label, 8, fy, 9, lc);
            Rectangle fr = { 22, (float)fy - 1, 50, 14 };
            bool hov = CheckCollisionPointRec(mousePos, fr);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (hov) { editing = true; ed->hexEditing = false; ed->rEdit = false; ed->gEdit = false; ed->bEdit = false; editing = true; }
                else editing = false;
            }
            DrawRectangleLinesEx(fr, 1, editing ? lc : Color{ 80,80,80,255 });
            DrawText(buf, (int)fr.x + 2, (int)fr.y + 2, 9, WHITE);
            if (editing) {
                int key;
                while ((key = GetCharPressed()) > 0) { int bl = (int)strlen(buf); if (bl < 3 && key >= '0' && key <= '9') { buf[bl] = (char)key; buf[bl + 1] = '\0'; } }
                if (IsKeyPressed(KEY_BACKSPACE)) { int bl = (int)strlen(buf); if (bl > 0) buf[--bl] = '\0'; }
                if (IsKeyPressed(KEY_ENTER)) editing = false;
                int v = Clamp(atoi(buf), 0, 255);
                channel = (uint8_t)v;
                ed->brushColor.a = 255;
                SyncBufsFromColor(ed);
            }
        };
        drawRGBField(py, "R:", ed->rBuf, ed->rEdit, { 255,80,80,255 }, ed->brushColor.r);
        drawRGBField(py + 16, "G:", ed->gBuf2, ed->gEdit, { 80,220,80,255 }, ed->brushColor.g);
        drawRGBField(py + 32, "B:", ed->bBuf, ed->bEdit, { 80,150,255,255 }, ed->brushColor.b);
        if (!ed->rEdit && !ed->gEdit && !ed->bEdit && !ed->hexEditing) {
            int rv = Clamp(atoi(ed->rBuf), 0, 255);
            int gv = Clamp(atoi(ed->gBuf2), 0, 255);
            int bv = Clamp(atoi(ed->bBuf), 0, 255);
            if ((uint8_t)rv != ed->brushColor.r || (uint8_t)gv != ed->brushColor.g || (uint8_t)bv != ed->brushColor.b) {
                ed->brushColor.r = (uint8_t)rv; ed->brushColor.g = (uint8_t)gv; ed->brushColor.b = (uint8_t)bv; ed->brushColor.a = 255;
                SyncBufsFromColor(ed);
            }
        }
        DrawRectangle(76, py, 40, 48, ed->brushColor);
        DrawRectangleLinesEx({ 76, (float)py, 40, 48 }, 1, LIGHTGRAY);
        py += 52;
        DrawText(TextFormat("Pos: %d, %d", logicX, logicY), 8, py, 9, { 120,140,180,255 });
    }
    DrawRectangle(0, sh - 50, sw, 50, { 35,35,35,255 });
    DrawLine(0, sh - 50, sw, sh - 50, GRAY);
    float cx = (float)sw / 2;
    if (GuiButton({ cx - 110, (float)sh - 40, 100, 30 }, "EDITOR")) nextScene = SCENE_WHITE;
    if (GuiButton({ cx + 10,  (float)sh - 40, 100, 30 }, "MAP"))    nextScene = SCENE_BLUE;
    if (ed->status.timer > 0) {
        float alpha = Clamp(ed->status.timer / 0.5f, 0.0f, 1.0f);
        int tw2 = MeasureText(ed->status.text, 12), bx = sw / 2 - tw2 / 2 - 8, by = sh - 90;
        DrawRectangle(bx, by, tw2 + 16, 22, Fade({ 20,20,20,220 }, alpha));
        DrawText(ed->status.text, bx + 8, by + 5, 12, Fade(ed->status.color, alpha));
    }
    if (ed->menu.active) {
        float mx2 = ed->menu.screenPosition.x + 15;
        float my2 = ed->menu.screenPosition.y + 15;

        if (ed->menu.editMode == 1) {
            DrawRectangleRec({ mx2, my2, 165, 230 }, { 230,230,230,255 });
            Cell& tc = map->data[ed->menu.targetX][ed->menu.targetY];

            GuiColorPicker({ mx2 + 10, my2 + 10, 140, 140 }, NULL, &tc.color);

            if (GuiButton({ mx2 + 10, my2 + 155, 80, 22 }, "Reset")) {
                tc.color = LIGHTGRAY;
            }

            if (GuiButton({ mx2 + 95, my2 + 155, 60, 22 }, "Done")) {
                ed->menu.editMode = 0;
            }

            DrawText("Hex:", mx2 + 10, my2 + 182, 9, GRAY);
            static char menuHexBuf[16] = ""; static bool menuHexEdit = false;
            Rectangle mhR = { mx2 + 35, my2 + 180, 80, 15 };
            bool mhHov = CheckCollisionPointRec(mousePos, mhR);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                menuHexEdit = mhHov;
            }

            DrawRectangleLinesEx(mhR, 1, menuHexEdit ? Color{ 0,200,255,255 } : Color{ 180,180,180,255 });
            DrawText(menuHexBuf, (int)mhR.x + 2, (int)mhR.y + 2, 9, BLACK);

            if (menuHexEdit) {
                int key;
                while ((key = GetCharPressed()) > 0) {
                    int hl = (int)strlen(menuHexBuf);
                    if (hl < 7) {
                        menuHexBuf[hl] = (char)key; menuHexBuf[hl + 1] = '\0';
                    }
                }

                if (IsKeyPressed(KEY_BACKSPACE)) {
                    int hl = (int)strlen(menuHexBuf);
                    if (hl > 0) {
                        menuHexBuf[--hl] = '\0';
                    }
                }

                Color parsed;

                if (TryParseHex(menuHexBuf, &parsed)) {
                    tc.color = parsed;
                }
            }
            sprintf(menuHexBuf, "#%02X%02X%02X", tc.color.r, tc.color.g, tc.color.b);
        }
        else {
            float mh2 = (ed->menu.editMode == 0) ? 210.0f : (ed->menu.editMode == 2 ? 140.0f : 80.0f);
            DrawRectangleRec({ mx2, my2, 130, mh2 }, { 45,45,45,255 });
            DrawRectangleLinesEx({ mx2, my2, 130, mh2 }, 1, LIGHTGRAY);
            Cell& tc = map->data[ed->menu.targetX][ed->menu.targetY];

            if (ed->menu.editMode == 0) {
                DrawText(TextFormat("[%d,%d]", ed->menu.targetX - map->offsetX, ed->menu.targetY - map->offsetY), mx2 + 10, my2 + 8, 10, SKYBLUE);

                bool cellExists = (tc.color.r != 80 || tc.color.g != 80 || tc.color.b != 80 || tc.textureIndex >= 0);
                bool cellVisible = tc.height > 0.0f;

                if (!cellExists) {
                    DrawText("EMPTY CELL", mx2 + 10, my2 + 24, 9, { 120,120,120,255 });
                    if (GuiButton({ mx2 + 10, my2 + 44, 110, 25 }, "Set Texture")) ed->menu.editMode = 3;
                    if (GuiButton({ mx2 + 10, my2 + 74, 110, 20 }, "Close"))   ed->menu.active = false;
                }
                else if (!cellVisible) {
                    DrawText("HIDDEN", mx2 + 10, my2 + 22, 9, { 180,180,80,255 });
                    const char* texLabel = (tc.textureIndex >= 0 && tc.textureIndex < gTextureCount)
                        ? TextFormat("TEX: %s", GetFileName(gTextures[tc.textureIndex].path))
                        : "TEX: none";
                    DrawText(texLabel, mx2 + 10, my2 + 33, 9, tc.textureIndex >= 0 ? Color{ 80,220,80,255 } : Color{ 120,120,120,255 });
                    if (GuiButton({ mx2 + 10, my2 + 48,  110, 25 }, "Texture"))   ed->menu.editMode = 3;
                    if (GuiButton({ mx2 + 10, my2 + 78,  110, 20 }, "Close"))     ed->menu.active = false;
                }
                else {
                    DrawText("VISIBLE", mx2 + 10, my2 + 22, 9, { 80,220,80,255 });
                    DrawText(tc.solid ? "COLLISION: ON" : "COLLISION: OFF", mx2 + 10, my2 + 33, 9, tc.solid ? Color{ 120,120,120,255 } : Color{ 255,80,80,255 });
                    const char* texLabel = (tc.textureIndex >= 0 && tc.textureIndex < gTextureCount)
                        ? TextFormat("TEX: %s", GetFileName(gTextures[tc.textureIndex].path))
                        : "TEX: none";
                    DrawText(texLabel, mx2 + 10, my2 + 44, 9, tc.textureIndex >= 0 ? Color{ 80,220,80,255 } : Color{ 120,120,120,255 });
                    if (GuiButton({ mx2 + 10, my2 + 58,  110, 25 }, "Set Color"))     ed->menu.editMode = 1;
                    if (GuiButton({ mx2 + 10, my2 + 88,  110, 25 }, "Set Collision")) ed->menu.editMode = 4;
                    if (GuiButton({ mx2 + 10, my2 + 118, 110, 25 }, "Set Texture"))   ed->menu.editMode = 3;
                    if (GuiButton({ mx2 + 10, my2 + 155, 110, 20 }, "Close"))     ed->menu.active = false;
                }
            }
            else if (ed->menu.editMode == 3) {
                Cell& tc2 = map->data[ed->menu.targetX][ed->menu.targetY];
                DrawText("Texture", mx2 + 10, my2 + 8, 10, YELLOW);

                if (tc2.textureIndex >= 0 && tc2.textureIndex < gTextureCount) {
                    DrawText(TextFormat("Tex: %d", tc2.textureIndex), mx2 + 10, my2 + 22, 9, GREEN);
                }
                else {
                    DrawText("No texture", mx2 + 10, my2 + 22, 9, GRAY);
                }

                if (GuiButton({ mx2 + 10, my2 + 36, 110, 25 }, "Choose File...")) {
                    std::string path = OpenFileDialog();
                    if (!path.empty()) {
                        int idx = LoadOrFindTexture(path.c_str());
                        if (idx >= 0) tc2.textureIndex = idx;
                    }
                }
                if (GuiButton({ mx2 + 10, my2 + 65, 110, 25 }, "Delete Texture"))
                    tc2.textureIndex = -1;
                if (GuiButton({ mx2 + 10, my2 + 95, 110, 20 }, "Back")) ed->menu.editMode = 0;
            }
            else if (ed->menu.editMode == 4) {
                DrawText("Collision", mx2 + 10, my2 + 8, 10, { 255,80,80,255 });
                bool isSolid = tc.solid;
                DrawText(isSolid ? "Collision: ON" : "Collision: OFF", mx2 + 10, my2 + 28, 10, isSolid ? LIGHTGRAY : Color{ 255,80,80,255 });
                if (GuiButton({ mx2 + 10, my2 + 44, 110, 22 }, isSolid ? "Disable" : "Enable")) tc.solid = !tc.solid;
                if (GuiButton({ mx2 + 10, my2 + 70, 110, 18 }, "Back")) ed->menu.editMode = 0;
            }
        }
    }
    if (ed->showNewConfirm) {
        int dw = 280, dh = 100, dx = sw / 2 - dw / 2, dy = sh / 2 - dh / 2;
        DrawRectangle(dx, dy, dw, dh, { 40,40,40,245 });
        DrawRectangleLinesEx({ (float)dx, (float)dy, (float)dw, (float)dh }, 1, LIGHTGRAY);
        DrawText("New map? Unsaved changes will be lost.", dx + 14, dy + 14, 10, LIGHTGRAY);
        if (GuiButton({ (float)dx + 20, (float)dy + 55, 110, 28 }, "#8# Yes, new map")) {
            FreeDynamicMap(map); *map = InitDynamicMap(50, 50);
            FreeSelectionGrid(&ed->selGrid); ed->selGrid = InitSelectionGrid(map->width, map->height);
            ClearSelection(&ed->selGrid, &ed->sel); ClearUndoStack(&ed->undoStack); PushUndo(&ed->undoStack, map);
            ed->pasteMode = false; ed->shapeFirstClick = false; ClearPreview();
            ed->camera2D.target = { 0, 0 }; ed->showNewConfirm = false; ed->menu.active = false;
            SetStatus(&ed->status, "New map created", GREEN);
        }
        if (GuiButton({ (float)dx + 150, (float)dy + 55, 110, 28 }, "Cancel")) ed->showNewConfirm = false;
    }
    DrawFPS(sw - 80, sh - 35);
    EndDrawing();

    return nextScene;
}

Vector2 GetEditorCameraTarget(const EditorState* ed) {
    return ed->camera2D.target;
}
