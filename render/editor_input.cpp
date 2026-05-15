#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"
#include "editor.h"
#include "editor_input.h"
#include "editor_draw.h"
#include "map.h"
#include "preview.h"
#include "utils.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

void SyncBufsFromColor(EditorState* ed);

static void ResetSelectionGrid(EditorState* ed, DynamicMap* map)
{
    FreeSelectionGrid(&ed->selGrid);
    ed->selGrid = InitSelectionGrid(map->width, map->height);
    ClearSelection(&ed->selGrid, &ed->sel);
}

static void SwitchTool(EditorState* ed, ToolMode next)
{
    ed->toolMode = next;

    if (next != TOOL_SELECT)
    {
        ClearSelection(&ed->selGrid, &ed->sel);
        ed->shapeFirstClick = false;
        ClearPreview();
        MarkMapDirty(ed);
    }
}

void HandleZoom(EditorState* ed, Vector2 mousePos, float wheel)
{
    if (ed->menu.active)
        ed->menu.active = false;

    Vector2 worldPos = GetScreenToWorld2D(mousePos, ed->camera2D);
    ed->camera2D.offset = mousePos;
    ed->camera2D.target = worldPos;
    ed->camera2D.zoom = Clamp(ed->camera2D.zoom + wheel * 0.12f, MAX_UNZOOM, MAX_ZOOM);
}


void HandleUndoRedo(EditorState* ed, DynamicMap* map)
{
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (ctrl && IsKeyPressed(KEY_Z))
    {
        if (UndoStep(&ed->undoStack, map))
        {
            ResetSelectionGrid(ed, map);
            SetStatus(&ed->status, "Undo", SKYBLUE);
        }
    }

    if (ctrl && IsKeyPressed(KEY_Y))
    {
        if (RedoStep(&ed->undoStack, map))
        {
            ResetSelectionGrid(ed, map);
            SetStatus(&ed->status, "Redo", SKYBLUE);
        }
    }
}

void HandleCopyPaste(EditorState* ed, DynamicMap* map, int logicX, int logicY)
{
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (ctrl && IsKeyPressed(KEY_C) && ed->toolMode == TOOL_SELECT && ed->sel.count > 0)
    {
        CopySelection(&ed->clipboard, &ed->sel, map);
        SetStatus(&ed->status, TextFormat("Copied %d cells", ed->clipboard.count), GREEN);
    }

    if (ctrl && IsKeyPressed(KEY_V) && ed->clipboard.count > 0)
    {
        ed->toolMode = TOOL_SELECT;
        ed->pasteMode = false;
        PushUndo(&ed->undoStack, map);
        PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, logicX, logicY);
        RebuildSelectionList(&ed->selGrid, map, &ed->sel);
        SetStatus(&ed->status, "Pasted", GREEN);
        MarkMapDirty(ed);
    }
}

static void ComputeSelectionBoundsMin(EditorState* ed, DynamicMap* map, int& minLX, int& minLY)
{
    minLX = ed->sel.srcX[0] - map->offsetX;
    minLY = ed->sel.srcY[0] - map->offsetY;

    for (int i = 1; i < ed->sel.count; i++)
    {
        if (ed->sel.srcX[i] - map->offsetX < minLX)
            minLX = ed->sel.srcX[i] - map->offsetX;

        if (ed->sel.srcY[i] - map->offsetY < minLY)
            minLY = ed->sel.srcY[i] - map->offsetY;
    }
}

static void PasteMirroredClipboard(EditorState* ed, DynamicMap* map, int destLX, int destLY)
{
    EnsureMapContainsLogicRect(
        map, &ed->selGrid,
        destLX, destLY,
        destLX + ed->clipboard.boundsW - 1,
        destLY + ed->clipboard.boundsH - 1
    );
    PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, destLX, destLY);
    RebuildSelectionList(&ed->selGrid, map, &ed->sel);
    FreeClipboard(&ed->clipboard);
}

void HandleMirror(EditorState* ed, DynamicMap* map)
{
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (ed->toolMode != TOOL_SELECT || ed->sel.count == 0)
        return;

    if (ctrl && IsKeyPressed(KEY_LEFT))
    {
        CopySelection(&ed->clipboard, &ed->sel, map);
        MirrorClipboardX(&ed->clipboard);
        int minLX, minLY;
        ComputeSelectionBoundsMin(ed, map, minLX, minLY);
        PasteMirroredClipboard(ed, map, minLX - ed->clipboard.boundsW, minLY);
        SetStatus(&ed->status, "Mirrored to the left", { 255, 200, 0, 255 });
        MarkMapDirty(ed);
    }

    if (ctrl && IsKeyPressed(KEY_RIGHT))
    {
        CopySelection(&ed->clipboard, &ed->sel, map);
        MirrorClipboardX(&ed->clipboard);

        int maxLX = ed->sel.srcX[0] - map->offsetX;
        int minLY = ed->sel.srcY[0] - map->offsetY;

        for (int i = 1; i < ed->sel.count; i++)
        {
            if (ed->sel.srcX[i] - map->offsetX > maxLX)
                maxLX = ed->sel.srcX[i] - map->offsetX;

            if (ed->sel.srcY[i] - map->offsetY < minLY)
                minLY = ed->sel.srcY[i] - map->offsetY;
        }

        PasteMirroredClipboard(ed, map, maxLX + 1, minLY);
        SetStatus(&ed->status, "Mirrored to the right", { 255, 200, 0, 255 });
        MarkMapDirty(ed);
    }

    if (ctrl && IsKeyPressed(KEY_UP))
    {
        CopySelection(&ed->clipboard, &ed->sel, map);
        MirrorClipboardY(&ed->clipboard);
        int minLX, minLY;
        ComputeSelectionBoundsMin(ed, map, minLX, minLY);
        PasteMirroredClipboard(ed, map, minLX, minLY - ed->clipboard.boundsH);
        SetStatus(&ed->status, "Mirror up", { 255, 200, 0, 255 });
        MarkMapDirty(ed);
    }

    if (ctrl && IsKeyPressed(KEY_DOWN))
    {
        CopySelection(&ed->clipboard, &ed->sel, map);
        MirrorClipboardY(&ed->clipboard);

        int minLX = ed->sel.srcX[0] - map->offsetX;
        int maxLY = ed->sel.srcY[0] - map->offsetY;

        for (int i = 1; i < ed->sel.count; i++)
        {
            if (ed->sel.srcX[i] - map->offsetX < minLX)
                minLX = ed->sel.srcX[i] - map->offsetX;

            if (ed->sel.srcY[i] - map->offsetY > maxLY)
                maxLY = ed->sel.srcY[i] - map->offsetY;
        }

        PasteMirroredClipboard(ed, map, minLX, maxLY + 1);
        SetStatus(&ed->status, "Mirror down", { 255, 200, 0, 255 });
        MarkMapDirty(ed);
    }
}

void HandleRotate(EditorState* ed, DynamicMap* map)
{
    if (!IsKeyPressed(KEY_R))
        return;

    if (ed->toolMode != TOOL_SELECT)
        return;

    if (ed->pasteMode)
    {
        RotateClipboardCW(&ed->clipboard);
        SetStatus(&ed->status, "Rotated clipboard", { 255, 200, 0, 255 });
        return;
    }

    if (ed->sel.count > 0)
    {
        PushUndo(&ed->undoStack, map);
        RotateSelectionCW(map, &ed->selGrid, &ed->sel);
        RebuildSelectionList(&ed->selGrid, map, &ed->sel);
        SetStatus(&ed->status, "Rotated selection", { 255, 200, 0, 255 });
        MarkMapDirty(ed);
    }
}

void HandleToolKeys(EditorState* ed)
{
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (IsKeyPressed(KEY_B))
    {
        ToolMode next = (ed->toolMode == TOOL_BRUSH) ? TOOL_NONE : TOOL_BRUSH;
        SwitchTool(ed, next);
        SetStatus(&ed->status, next == TOOL_BRUSH ? "Brush" : "No tool", LIGHTGRAY);
    }

    if (IsKeyPressed(KEY_E))
    {
        ToolMode next = (ed->toolMode == TOOL_ERASER) ? TOOL_NONE : TOOL_ERASER;
        SwitchTool(ed, next);
        SetStatus(&ed->status, next == TOOL_ERASER ? "Eraser" : "No tool", LIGHTGRAY);
    }

    if (IsKeyPressed(KEY_S) && !ctrl)
    {
        ToolMode next = (ed->toolMode == TOOL_SELECT) ? TOOL_NONE : TOOL_SELECT;
        ed->toolMode = next;

        if (next != TOOL_SELECT)
        {
            ClearSelection(&ed->selGrid, &ed->sel);
            ed->pasteMode = false;
            MarkMapDirty(ed);
        }

        ed->shapeFirstClick = false;
        ClearPreview();
        SetStatus(&ed->status, next == TOOL_SELECT ? "Select" : "No tool", LIGHTGRAY);
    }

    if (IsKeyPressed(KEY_L))
    {
        ToolMode next = (ed->toolMode == TOOL_LINE) ? TOOL_NONE : TOOL_LINE;
        SwitchTool(ed, next);
        SetStatus(&ed->status, next == TOOL_LINE ? "Line" : "No tool", LIGHTGRAY);
    }

    if (IsKeyPressed(KEY_R) && ed->toolMode != TOOL_SELECT)
    {
        ToolMode next = (ed->toolMode == TOOL_RECT) ? TOOL_NONE : TOOL_RECT;
        SwitchTool(ed, next);
        SetStatus(&ed->status, next == TOOL_RECT ? "Rectangle" : "No tool", LIGHTGRAY);
    }

    if (IsKeyPressed(KEY_C) && !ctrl)
    {
        ToolMode next = (ed->toolMode == TOOL_CIRCLE) ? TOOL_NONE : TOOL_CIRCLE;
        SwitchTool(ed, next);
        SetStatus(&ed->status, next == TOOL_CIRCLE ? "Circle" : "No tool", LIGHTGRAY);
    }
}

void HandleSelectTool(EditorState* ed, DynamicMap* map, Vector2 worldMouse, int logicX, int logicY)
{
    if (ed->pasteMode)
    {
        ed->pasteLogicX = logicX;
        ed->pasteLogicY = logicY;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            PushUndo(&ed->undoStack, map);
            PasteClipboard(&ed->clipboard, map, &ed->selGrid, &ed->sel, ed->pasteLogicX, ed->pasteLogicY);
            RebuildSelectionList(&ed->selGrid, map, &ed->sel);
            ed->pasteMode = false;
            SetStatus(&ed->status, "Pasted", GREEN);
            MarkMapDirty(ed);
        }

        if (IsKeyPressed(KEY_ESCAPE))
            ed->pasteMode = false;

        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        int ax = logicX + map->offsetX;
        int ay = logicY + map->offsetY;
        bool onSel = (ax >= 0 && ax < ed->selGrid.width
            && ay >= 0 && ay < ed->selGrid.height
            && ed->selGrid.data[ax][ay]);

        if (onSel && ed->sel.count > 0)
        {
            PushUndo(&ed->undoStack, map);
            ed->sel.isMoving = true;
            ed->sel.moveStartWorld = worldMouse;
            ed->sel.moveDeltaX = 0;
            ed->sel.moveDeltaY = 0;
        }
        else
        {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

            if (!shift)
            {
                ClearSelection(&ed->selGrid, &ed->sel);
                MarkMapDirty(ed);
            }

            ed->sel.isDragSelecting = true;
            ed->sel.dragStart = worldMouse;
            ed->sel.dragEnd = worldMouse;
        }
    }

    if (ed->sel.isDragSelecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        ed->sel.dragEnd = worldMouse;

    if (ed->sel.isMoving && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        float ddx = worldMouse.x - ed->sel.moveStartWorld.x;
        float ddy = worldMouse.y - ed->sel.moveStartWorld.y;
        ed->sel.moveDeltaX = (int)floor(ddx / CELL_BASE_SIZE + 0.5f);
        ed->sel.moveDeltaY = (int)floor(ddy / CELL_BASE_SIZE + 0.5f);
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (ed->sel.isDragSelecting)
        {
            int lx0 = (int)floor(fminf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE);
            int ly0 = (int)floor(fminf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE);
            int lx1 = (int)floor(fmaxf(ed->sel.dragStart.x, ed->sel.dragEnd.x) / CELL_BASE_SIZE);
            int ly1 = (int)floor(fmaxf(ed->sel.dragStart.y, ed->sel.dragEnd.y) / CELL_BASE_SIZE);

            if (lx0 == lx1 && ly0 == ly1)
            {
                int ax = lx0 + map->offsetX;
                int ay = ly0 + map->offsetY;

                if (ax >= 0 && ax < ed->selGrid.width && ay >= 0 && ay < ed->selGrid.height)
                    ed->selGrid.data[ax][ay] = !ed->selGrid.data[ax][ay];
            }
            else
            {
                for (int lx = lx0; lx <= lx1; lx++)
                {
                    for (int ly = ly0; ly <= ly1; ly++)
                    {
                        int ax = lx + map->offsetX;
                        int ay = ly + map->offsetY;

                        if (ax >= 0 && ax < ed->selGrid.width && ay >= 0 && ay < ed->selGrid.height)
                            ed->selGrid.data[ax][ay] = true;
                    }
                }
            }

            RebuildSelectionList(&ed->selGrid, map, &ed->sel);
            ed->sel.isDragSelecting = false;
            MarkMapDirty(ed);
        }

        if (ed->sel.isMoving)
        {
            CommitMove(map, &ed->selGrid, &ed->sel, ed->sel.moveDeltaX, ed->sel.moveDeltaY);
            ed->sel.isMoving = false;
            ed->sel.moveDeltaX = 0;
            ed->sel.moveDeltaY = 0;
            MarkMapDirty(ed);
        }
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        ClearSelection(&ed->selGrid, &ed->sel);
        MarkMapDirty(ed);
    }

    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))
    {
        PushUndo(&ed->undoStack, map);

        for (int i = 0; i < ed->sel.count; i++)
            map->data[ed->sel.srcX[i]][ed->sel.srcY[i]] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };

        ClearSelection(&ed->selGrid, &ed->sel);
        MarkMapDirty(ed);
    }
}

void HandleShapeTools(EditorState* ed, DynamicMap* map, int logicX, int logicY)
{
    if (ed->shapeFirstClick)
    {
        if (ed->toolMode == TOOL_LINE)
        {
            BuildLinePreview(ed->shapeStartLX, ed->shapeStartLY, logicX, logicY);
        }
        else if (ed->toolMode == TOOL_RECT)
        {
            BuildRectPreview(ed->shapeStartLX, ed->shapeStartLY, logicX, logicY);
        }
        else
        {
            int rx = abs(logicX - ed->shapeStartLX);
            int ry = abs(logicY - ed->shapeStartLY);
            BuildCirclePreview(ed->shapeStartLX, ed->shapeStartLY, rx, ry);
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (!ed->shapeFirstClick)
        {
            ed->shapeFirstClick = true;
            ed->shapeStartLX = logicX;
            ed->shapeStartLY = logicY;
            ClearPreview();
        }
        else
        {
            PushUndo(&ed->undoStack, map);
            Cell c = { 2.5f, ed->brushColor, TYPE_BLOCK, ed->brushSolid, -1 };
            CommitPreview(map, c);
            MarkMapDirty(ed);

            if (ed->toolMode == TOOL_LINE)
            {
                ed->shapeStartLX = logicX;
                ed->shapeStartLY = logicY;
                ClearPreview();
            }
            else
            {
                ed->shapeFirstClick = false;
                ClearPreview();
            }
        }
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        ed->shapeFirstClick = false;
        ClearPreview();
    }
}

void HandleCanvasTools(EditorState* ed, DynamicMap* map, bool mouseOnUI, int logicX, int logicY)
{
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && ed->toolMode == TOOL_NONE)
    {
        Vector2 delta = GetMouseDelta();

        if (Vector2Length(delta) > 0.5f)
        {
            ed->isDragging = true;
            ed->camera2D.target = Vector2Add(
                ed->camera2D.target,
                Vector2Scale(delta, -1.0f / ed->camera2D.zoom)
            );
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ed->menu.active && !mouseOnUI)
        ed->menu.active = false;

    int ax = logicX + map->offsetX;
    int ay = logicY + map->offsetY;
    bool inside = (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height);

    if (inside && !mouseOnUI)
    {
        if (ed->toolMode == TOOL_ERASER)
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                PushUndo(&ed->undoStack, map);
                ed->strokeStarted = true;
            }

            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            {
                map->data[ax][ay] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };
                MarkMapDirty(ed);
            }
        }
        else if (ed->toolMode == TOOL_BRUSH)
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                if (ed->undoStack.count == 0)
                    PushUndo(&ed->undoStack, map);
            }

            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            {
                map->data[ax][ay].height = 2.5f;
                map->data[ax][ay].color = ed->brushColor;
                map->data[ax][ay].solid = ed->brushSolid;
                MarkMapDirty(ed);
            }

            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                PushUndo(&ed->undoStack, map);
        }
        else if (ed->toolMode == TOOL_NONE)
        {
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !ed->isDragging && !ed->menu.active)
            {
                bool hasData = (
                    map->data[ax][ay].color.r != DARKGRAY.r
                    || map->data[ax][ay].color.g != DARKGRAY.g
                    || map->data[ax][ay].color.b != DARKGRAY.b
                    || map->data[ax][ay].textureIndex >= 0
                    );

                if (hasData)
                {
                    PushUndo(&ed->undoStack, map);
                    map->data[ax][ay].height = (map->data[ax][ay].height > 0.0f) ? 0.0f : 2.5f;
                    MarkMapDirty(ed);
                }
            }
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        ed->isDragging = false;
        ed->strokeStarted = false;
    }
}

void HandleContextMenu(EditorState* ed, DynamicMap* map, Vector2 worldMouse, int logicX, int logicY)
{
    if (ed->toolMode != TOOL_NONE)
        return;

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        int ax = logicX + map->offsetX;
        int ay = logicY + map->offsetY;

        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height)
        {
            ed->menu.active = true;
            ed->menu.worldPosition = worldMouse;
            ed->menu.screenPosition = GetWorldToScreen2D(worldMouse, ed->camera2D);
            ed->menu.targetX = ax;
            ed->menu.targetY = ay;
            ed->menu.editMode = 0;
        }
    }
}

void HandleMapExpansion(EditorState* ed, DynamicMap* map, int sw, int sh, float wheel)
{
    bool active2D = (
        ed->isDragging
        || ed->toolMode != TOOL_NONE
        || wheel != 0
        || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)
        );

    if (!active2D)
        return;

    Vector2 vTL = GetScreenToWorld2D({ 0, 45 }, ed->camera2D);
    Vector2 vBR = GetScreenToWorld2D({ (float)sw, (float)sh - 50 }, ed->camera2D);
    int vx0 = (int)floor(vTL.x / CELL_BASE_SIZE);
    int vy0 = (int)floor(vTL.y / CELL_BASE_SIZE);
    int vx1 = (int)floor(vBR.x / CELL_BASE_SIZE);
    int vy1 = (int)floor(vBR.y / CELL_BASE_SIZE);
    int step = 20;
    int thr = 5;

    if (vx0 < -map->offsetX + thr)
    {
        ExpandMap(map, map->width + step, map->height, step, 0);
        ExpandSelectionGrid(&ed->selGrid, map->width, map->height, step, 0);
    }

    if (vx1 > (map->width - map->offsetX) - thr)
    {
        ExpandMap(map, map->width + step, map->height, 0, 0);
        ExpandSelectionGrid(&ed->selGrid, map->width, map->height, 0, 0);
    }

    if (vy0 < -map->offsetY + thr)
    {
        ExpandMap(map, map->width, map->height + step, 0, step);
        ExpandSelectionGrid(&ed->selGrid, map->width, map->height, 0, step);
    }

    if (vy1 > (map->height - map->offsetY) - thr)
    {
        ExpandMap(map, map->width, map->height + step, 0, 0);
        ExpandSelectionGrid(&ed->selGrid, map->width, map->height, 0, 0);
    }
}

void HandleFilenameInput(EditorState* ed, Vector2 mousePos)
{
    Rectangle fileInputRect = { 8, 50, 144, 18 };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        ed->editingFilename = CheckCollisionPointRec(mousePos, fileInputRect);

    if (!ed->editingFilename)
        return;

    int key;
    while ((key = GetCharPressed()) > 0)
    {
        int len = (int)strlen(ed->saveFile);

        if (len < 255)
        {
            ed->saveFile[len] = (char)key;
            ed->saveFile[len + 1] = '\0';
        }
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        int len = (int)strlen(ed->saveFile);

        if (len > 0)
            ed->saveFile[--len] = '\0';
    }

    if (IsKeyPressed(KEY_ENTER))
        ed->editingFilename = false;
}