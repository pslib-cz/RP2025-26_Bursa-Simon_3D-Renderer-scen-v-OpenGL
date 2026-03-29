#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include <cstdint>

typedef enum { SCENE_WHITE = 0, SCENE_BLUE } GameScene;
typedef enum { TYPE_BLOCK = 0, TYPE_DECOR, TYPE_TEXTURE_ONLY } CellType;

#define CELL_BASE_SIZE 40.0f
#define MAX_UNZOOM 0.15f
#define MAX_ZOOM 3.0f
#define UNDO_MAX 64

typedef struct {
    float height;
    Color color;
    CellType type;
} Cell;

typedef struct {
    Cell** data;
    int width;
    int height;
    int offsetX;
    int offsetY;
} DynamicMap;

typedef struct {
    bool active;
    Vector2 worldPosition;
    Vector2 screenPosition;
    int targetX;
    int targetY;
    int editMode;
} ContextMenu;

typedef struct {
    Cell* cells;
    int* srcX;
    int* srcY;
    int count;
    int capacity;
    bool isDragSelecting;
    Vector2 dragStart;
    Vector2 dragEnd;
    bool isMoving;
    Vector2 moveStartWorld;
    int moveDeltaX;
    int moveDeltaY;
} SelectionState;

typedef struct {
    bool** data;
    int width;
    int height;
} SelectionGrid;

typedef struct {
    Cell* cells;
    int* relX;
    int* relY;
    int count;
    int boundsW;
    int boundsH;
} Clipboard;

typedef struct {
    Cell** data;
    int width;
    int height;
    int offsetX;
    int offsetY;
} MapSnapshot;

typedef struct {
    MapSnapshot snapshots[UNDO_MAX];
    int count;  
    int current;
} UndoStack;

#define MAP_MAGIC "MAP\0"
#define MAP_VERSION 1

MapSnapshot SnapshotMap(const DynamicMap* m) {
    MapSnapshot s;
    s.width = m->width; s.height = m->height;
    s.offsetX = m->offsetX; s.offsetY = m->offsetY;
    s.data = (Cell**)malloc(m->width * sizeof(Cell*));
    for (int x = 0; x < m->width; x++) {
        s.data[x] = (Cell*)malloc(m->height * sizeof(Cell));
        memcpy(s.data[x], m->data[x], m->height * sizeof(Cell));
    }
    return s;
}

void FreeSnapshot(MapSnapshot* s) {
    if (!s->data) return;
    for (int i = 0; i < s->width; i++) free(s->data[i]);
    free(s->data);
    s->data = nullptr;
}

void RestoreSnapshot(DynamicMap* m, const MapSnapshot* s) {
    for (int i = 0; i < m->width; i++) free(m->data[i]);
    free(m->data);
    m->width = s->width; m->height = s->height;
    m->offsetX = s->offsetX; m->offsetY = s->offsetY;
    m->data = (Cell**)malloc(m->width * sizeof(Cell*));
    for (int x = 0; x < m->width; x++) {
        m->data[x] = (Cell*)malloc(m->height * sizeof(Cell));
        memcpy(m->data[x], s->data[x], m->height * sizeof(Cell));
    }
}

void PushUndo(UndoStack* us, const DynamicMap* m) {

    for (int i = us->current + 1; i < us->count; i++) FreeSnapshot(&us->snapshots[i]);
    us->count = us->current + 1;

    if (us->count >= UNDO_MAX) {
        FreeSnapshot(&us->snapshots[0]);
        for (int i = 0; i < UNDO_MAX - 1; i++) us->snapshots[i] = us->snapshots[i + 1];
        us->snapshots[UNDO_MAX - 1].data = nullptr;
        us->count = UNDO_MAX - 1;
        us->current = us->count - 1;
    }
    us->snapshots[us->count] = SnapshotMap(m);
    us->current = us->count;
    us->count++;
}

bool UndoStep(UndoStack* us, DynamicMap* m) {
    if (us->current <= 0) return false;
    us->current--;
    RestoreSnapshot(m, &us->snapshots[us->current]);
    return true;
}

bool RedoStep(UndoStack* us, DynamicMap* m) {
    if (us->current >= us->count - 1) return false;
    us->current++;
    RestoreSnapshot(m, &us->snapshots[us->current]);
    return true;
}

void InitUndoStack(UndoStack* us) {
    memset(us->snapshots, 0, sizeof(us->snapshots));
    us->count = 0; us->current = -1;
}

void ClearUndoStack(UndoStack* us) {
    for (int i = 0; i < us->count; i++) FreeSnapshot(&us->snapshots[i]);
    us->count = 0; us->current = -1;
}

void FreeClipboard(Clipboard* cb) {
    free(cb->cells); cb->cells = nullptr;
    free(cb->relX);  cb->relX = nullptr;
    free(cb->relY);  cb->relY = nullptr;
    cb->count = 0;
}

void CopySelection(Clipboard* cb, const SelectionState* sel, const DynamicMap* map) {
    if (sel->count == 0) return;
    FreeClipboard(cb);
    int minX = sel->srcX[0], minY = sel->srcY[0];
    int maxX = minX, maxY = minY;
    for (int i = 1; i < sel->count; i++) {
        if (sel->srcX[i] < minX) minX = sel->srcX[i];
        if (sel->srcY[i] < minY) minY = sel->srcY[i];
        if (sel->srcX[i] > maxX) maxX = sel->srcX[i];
        if (sel->srcY[i] > maxY) maxY = sel->srcY[i];
    }
    cb->boundsW = maxX - minX + 1;
    cb->boundsH = maxY - minY + 1;
    cb->count = sel->count;
    cb->cells = (Cell*)malloc(cb->count * sizeof(Cell));
    cb->relX = (int*)malloc(cb->count * sizeof(int));
    cb->relY = (int*)malloc(cb->count * sizeof(int));
    for (int i = 0; i < sel->count; i++) {
        cb->cells[i] = sel->cells[i];
        cb->relX[i] = sel->srcX[i] - minX;
        cb->relY[i] = sel->srcY[i] - minY;
    }
}

void PasteClipboard(Clipboard* cb, DynamicMap* map, SelectionGrid* sg, SelectionState* sel,
    int pasteLogicX, int pasteLogicY) {
    if (cb->count == 0) return;
    for (int x = 0; x < sg->width; x++) memset(sg->data[x], 0, sg->height * sizeof(bool));
    sel->count = 0; sel->isMoving = false;

    for (int i = 0; i < cb->count; i++) {
        int lx = pasteLogicX + cb->relX[i];
        int ly = pasteLogicY + cb->relY[i];
        int ax = lx + map->offsetX;
        int ay = ly + map->offsetY;
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            map->data[ax][ay] = cb->cells[i];
            sg->data[ax][ay] = true;
        }
    }
}

void RotateClipboardCW(Clipboard* cb) {
    if (cb->count == 0) return;
    int newW = cb->boundsH;
    int newH = cb->boundsW;
    for (int i = 0; i < cb->count; i++) {
        int ox = cb->relX[i], oy = cb->relY[i];
        cb->relX[i] = (cb->boundsH - 1) - oy;
        cb->relY[i] = ox;
    }
    cb->boundsW = newW;
    cb->boundsH = newH;
}

void RotateSelectionCW(DynamicMap* map, SelectionGrid* sg, SelectionState* sel) {
    if (sel->count == 0) return;

    int minX = sel->srcX[0], minY = sel->srcY[0];
    int maxX = minX, maxY = minY;
    for (int i = 1; i < sel->count; i++) {
        if (sel->srcX[i] < minX) minX = sel->srcX[i];
        if (sel->srcY[i] < minY) minY = sel->srcY[i];
        if (sel->srcX[i] > maxX) maxX = sel->srcX[i];
        if (sel->srcY[i] > maxY) maxY = sel->srcY[i];
    }
    int bW = maxX - minX + 1;
    int bH = maxY - minY + 1;

    Cell* tmp = (Cell*)malloc(sel->count * sizeof(Cell));
    int* nxArr = (int*)malloc(sel->count * sizeof(int));
    int* nyArr = (int*)malloc(sel->count * sizeof(int));
    for (int i = 0; i < sel->count; i++) {
        tmp[i] = sel->cells[i];
        int rx = sel->srcX[i] - minX;
        int ry = sel->srcY[i] - minY;
        nxArr[i] = minX + ((bH - 1) - ry);
        nyArr[i] = minY + rx;
    }

    for (int i = 0; i < sel->count; i++)
        map->data[sel->srcX[i]][sel->srcY[i]] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK };
    for (int x = 0; x < sg->width; x++) memset(sg->data[x], 0, sg->height * sizeof(bool));


    for (int i = 0; i < sel->count; i++) {
        int ax = nxArr[i], ay = nyArr[i];
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            map->data[ax][ay] = tmp[i];
            sg->data[ax][ay] = true;
            sel->srcX[i] = ax;
            sel->srcY[i] = ay;
        }
    }
    free(tmp); free(nxArr); free(nyArr);
}

bool SaveMap(const char* path, const DynamicMap* m) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(MAP_MAGIC, 1, 4, f);
    int ver = MAP_VERSION;
    fwrite(&ver, sizeof(int), 1, f);
    fwrite(&m->width, sizeof(int), 1, f);
    fwrite(&m->height, sizeof(int), 1, f);
    fwrite(&m->offsetX, sizeof(int), 1, f);
    fwrite(&m->offsetY, sizeof(int), 1, f);
    for (int x = 0; x < m->width; x++) {
        for (int y = 0; y < m->height; y++) {
            const Cell* c = &m->data[x][y];
            fwrite(&c->height, sizeof(float), 1, f);
            uint8_t rgba[4] = { c->color.r, c->color.g, c->color.b, c->color.a };
            fwrite(rgba, 1, 4, f);
            int t = (int)c->type;
            fwrite(&t, sizeof(int), 1, f);
        }
    }
    fclose(f);
    return true;
}

DynamicMap LoadMap(const char* path, bool* ok) {
    *ok = false;
    DynamicMap m = { nullptr, 0, 0, 0, 0 };
    FILE* f = fopen(path, "rb");
    if (!f) return m;
    char magic[4]; fread(magic, 1, 4, f);
    if (memcmp(magic, MAP_MAGIC, 4) != 0) { fclose(f); return m; }
    int ver; fread(&ver, sizeof(int), 1, f);
    if (ver != MAP_VERSION) { fclose(f); return m; }
    int w, h, ox, oy;
    fread(&w, sizeof(int), 1, f); fread(&h, sizeof(int), 1, f);
    fread(&ox, sizeof(int), 1, f); fread(&oy, sizeof(int), 1, f);
    if (w <= 0 || h <= 0 || w > 100000 || h > 100000) { fclose(f); return m; }
    m.width = w; m.height = h; m.offsetX = ox; m.offsetY = oy;
    m.data = (Cell**)malloc(w * sizeof(Cell*));
    for (int x = 0; x < w; x++) {
        m.data[x] = (Cell*)malloc(h * sizeof(Cell));
        for (int y = 0; y < h; y++) {
            Cell c;
            fread(&c.height, sizeof(float), 1, f);
            uint8_t rgba[4]; fread(rgba, 1, 4, f);
            c.color = { rgba[0], rgba[1], rgba[2], rgba[3] };
            int t; fread(&t, sizeof(int), 1, f);
            c.type = (CellType)t;
            m.data[x][y] = c;
        }
    }
    fclose(f);
    *ok = true;
    return m;
}

void FreeDynamicMap(DynamicMap* m) {
    for (int i = 0; i < m->width; i++) free(m->data[i]);
    free(m->data);
    m->data = nullptr; m->width = 0; m->height = 0;
}

SelectionGrid InitSelectionGrid(int w, int h) {
    SelectionGrid g; g.width = w; g.height = h;
    g.data = (bool**)malloc(w * sizeof(bool*));
    for (int i = 0; i < w; i++) g.data[i] = (bool*)calloc(h, sizeof(bool));
    return g;
}

void FreeSelectionGrid(SelectionGrid* sg) {
    for (int i = 0; i < sg->width; i++) free(sg->data[i]);
    free(sg->data);
    sg->data = nullptr; sg->width = 0; sg->height = 0;
}

void ExpandSelectionGrid(SelectionGrid* sg, int newW, int newH, int shiftX, int shiftY) {
    bool** nd = (bool**)malloc(newW * sizeof(bool*));
    for (int i = 0; i < newW; i++) {
        nd[i] = (bool*)calloc(newH, sizeof(bool));
        for (int j = 0; j < newH; j++) {
            int ox = i - shiftX, oy = j - shiftY;
            if (ox >= 0 && ox < sg->width && oy >= 0 && oy < sg->height)
                nd[i][j] = sg->data[ox][oy];
        }
    }
    FreeSelectionGrid(sg);
    sg->data = nd; sg->width = newW; sg->height = newH;
}

void ClearSelection(SelectionGrid* sg, SelectionState* sel) {
    for (int i = 0; i < sg->width; i++) memset(sg->data[i], 0, sg->height * sizeof(bool));
    sel->count = 0; sel->isMoving = false;
}

void AddToCapacity(SelectionState* sel, int needed) {
    if (needed <= sel->capacity) return;
    int nc = (needed + 63) & ~63;
    sel->cells = (Cell*)realloc(sel->cells, nc * sizeof(Cell));
    sel->srcX = (int*)realloc(sel->srcX, nc * sizeof(int));
    sel->srcY = (int*)realloc(sel->srcY, nc * sizeof(int));
    sel->capacity = nc;
}

void RebuildSelectionList(SelectionGrid* sg, DynamicMap* map, SelectionState* sel) {
    sel->count = 0;
    for (int x = 0; x < sg->width; x++)
        for (int y = 0; y < sg->height; y++)
            if (sg->data[x][y]) sel->count++;
    AddToCapacity(sel, sel->count);
    int idx = 0;
    for (int x = 0; x < sg->width; x++)
        for (int y = 0; y < sg->height; y++)
            if (sg->data[x][y]) {
                sel->srcX[idx] = x; sel->srcY[idx] = y;
                sel->cells[idx] = map->data[x][y]; idx++;
            }
}

DynamicMap InitDynamicMap(int w, int h) {
    DynamicMap m; m.width = w; m.height = h; m.offsetX = 0; m.offsetY = 0;
    m.data = (Cell**)malloc(w * sizeof(Cell*));
    for (int i = 0; i < w; i++) {
        m.data[i] = (Cell*)malloc(h * sizeof(Cell));
        for (int j = 0; j < h; j++) m.data[i][j] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK };
    }
    return m;
}

void ExpandMap(DynamicMap* m, int newW, int newH, int shiftX, int shiftY) {
    Cell** nd = (Cell**)malloc(newW * sizeof(Cell*));
    for (int i = 0; i < newW; i++) {
        nd[i] = (Cell*)malloc(newH * sizeof(Cell));
        for (int j = 0; j < newH; j++) {
            int ox = i - shiftX, oy = j - shiftY;
            nd[i][j] = (ox >= 0 && ox < m->width && oy >= 0 && oy < m->height)
                ? m->data[ox][oy] : Cell{ 0.0f, DARKGRAY, TYPE_BLOCK };
        }
    }
    FreeDynamicMap(m);
    m->data = nd; m->width = newW; m->height = newH;
    m->offsetX += shiftX; m->offsetY += shiftY;
}

Rectangle GetMenuRect(Vector2 sPos, int editMode) {
    float mx = sPos.x + 15, my = sPos.y + 15, w = 0, h = 0;
    if (editMode == 1) { w = 165; h = 210; }
    else if (editMode == 0) { w = 130; h = 145; }
    else if (editMode == 2) { w = 130; h = 140; }
    else if (editMode == 3) { w = 130; h = 80; }
    return { mx, my, w, h };
}

void CommitMove(DynamicMap* map, SelectionGrid* sg, SelectionState* sel, int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    for (int i = 0; i < sel->count; i++)
        map->data[sel->srcX[i]][sel->srcY[i]].height = -1.0f;
    for (int i = 0; i < sel->count; i++) {
        int nx = sel->srcX[i] + dx, ny = sel->srcY[i] + dy;
        if (nx >= 0 && nx < map->width && ny >= 0 && ny < map->height)
            map->data[nx][ny] = sel->cells[i];
    }
    for (int x = 0; x < map->width; x++)
        for (int y = 0; y < map->height; y++)
            if (map->data[x][y].height == -1.0f)
                map->data[x][y] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK };
    for (int x = 0; x < sg->width; x++) memset(sg->data[x], 0, sg->height);
    for (int i = 0; i < sel->count; i++) {
        int nx = sel->srcX[i] + dx, ny = sel->srcY[i] + dy;
        if (nx >= 0 && nx < sg->width && ny >= 0 && ny < sg->height)
            sg->data[nx][ny] = true;
    }
    RebuildSelectionList(sg, map, sel);
}

typedef struct { char text[128]; float timer; Color color; } StatusMsg;
void SetStatus(StatusMsg* s, const char* txt, Color c) {
    strncpy(s->text, txt, 127); s->text[127] = '\0';
    s->timer = 2.5f; s->color = c;
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1200, 800, "demo");
    SetWindowMinSize(800, 600);

    GameScene currentScene = SCENE_WHITE;
    ContextMenu menu = { false, {0,0}, {0,0}, 0, 0, 0 };
    bool brushMode = false, eraserMode = false, selectionMode = false;
    bool cursorLocked = true;
    StatusMsg status = { "", 0.0f, WHITE };

    DynamicMap map = InitDynamicMap(50, 50);
    SelectionGrid selGrid = InitSelectionGrid(50, 50);
    SelectionState sel = { nullptr, nullptr, nullptr, 0, 0,
                           false, {0,0}, {0,0}, false, {0,0}, 0, 0 };

    Clipboard clipboard = { nullptr, nullptr, nullptr, 0, 0, 0 };

    bool pasteMode = false;
    int  pasteLogicX = 0, pasteLogicY = 0;

    UndoStack undoStack;
    InitUndoStack(&undoStack);
    PushUndo(&undoStack, &map);

    bool showNewConfirm = false;

    Camera2D camera2D = {};
    camera2D.target = { 0, 0 };
    camera2D.offset = { 600.0f, 400.0f };
    camera2D.zoom = 1.0f;

    Camera3D camera3D = {};
    camera3D.position = { 0.0f, 0.8f, 0.0f };
    camera3D.target = { 1.0f, 0.8f, 1.0f };
    camera3D.up = { 0.0f, 1.0f, 0.0f };
    camera3D.fovy = 65.0f;
    camera3D.projection = CAMERA_PERSPECTIVE;

    bool isDragging = false;

    char saveFile[256] = "map.bin";
    bool editingFilename = false;
    int  fileLen = (int)strlen(saveFile);

    bool strokeStarted = false;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (status.timer > 0) status.timer -= dt;

        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        Vector2 mousePos = GetMousePosition();

        Rectangle topBar = { 0, 0, (float)sw, 45 };
        Rectangle bottomBar = { 0, (float)sh - 50, (float)sw, 50 };
        Rectangle sidebarArea = { 0, 46, 140, (float)(sh - 96) };

        Rectangle fileInputRect = { 8, 50, 124, 18 };
        Rectangle brushBtnRect = { 8,  74, 124, 30 };
        Rectangle eraserBtnRect = { 8, 108, 124, 30 };
        Rectangle selectBtnRect = { 8, 142, 124, 30 };


        Rectangle undoBtnRect = { (float)sw - 410, 10, 50, 25 };
        Rectangle redoBtnRect = { (float)sw - 355, 10, 50, 25 };
        Rectangle newBtnRect = { (float)sw - 300, 10, 50, 25 }; 
        Rectangle saveBtnRect = { (float)sw - 245, 10, 55, 25 };
        Rectangle loadBtnRect = { (float)sw - 185, 10, 55, 25 };
        Rectangle returnBtnRect = { (float)sw - 125, 10, 115, 25 };

        bool mouseOnUI = false;
        auto checkRect = [&](Rectangle r) { if (CheckCollisionPointRec(mousePos, r)) mouseOnUI = true; };
        checkRect(topBar);
        checkRect(bottomBar);
        checkRect(sidebarArea);
        if (menu.active) checkRect(GetMenuRect(menu.screenPosition, menu.editMode));
        if (showNewConfirm) mouseOnUI = true;

        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        if (currentScene == SCENE_WHITE) {
            if (IsCursorHidden()) EnableCursor();


            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                editingFilename = CheckCollisionPointRec(mousePos, fileInputRect) && !mouseOnUI;
            if (!CheckCollisionPointRec(mousePos, fileInputRect) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && editingFilename)
                editingFilename = false;

            if (editingFilename) {
                int key;
                while ((key = GetCharPressed()) > 0)
                    if (key >= 32 && fileLen < 255) { saveFile[fileLen++] = (char)key; saveFile[fileLen] = '\0'; }
                if (IsKeyPressed(KEY_BACKSPACE) && fileLen > 0) saveFile[--fileLen] = '\0';
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) editingFilename = false;
            }

            float wheel = GetMouseWheelMove();
            if (wheel != 0 && !mouseOnUI) {
                Vector2 mw = GetScreenToWorld2D(mousePos, camera2D);
                camera2D.offset = mousePos;
                camera2D.target = mw;
                camera2D.zoom = Clamp(camera2D.zoom + wheel * 0.12f, MAX_UNZOOM, MAX_ZOOM);
            }

            Vector2 worldMouse = GetScreenToWorld2D(mousePos, camera2D);
            int logicX = (int)floor(worldMouse.x / CELL_BASE_SIZE);
            int logicY = (int)floor(worldMouse.y / CELL_BASE_SIZE);

            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !mouseOnUI) {
                Vector2 delta = GetMouseDelta();
                if (Vector2Length(delta) > 0.5f)
                    camera2D.target = Vector2Add(camera2D.target,
                        Vector2Scale(delta, -1.0f / camera2D.zoom));
            }

            if (!editingFilename && !showNewConfirm) {
                if (ctrl && IsKeyPressed(KEY_Z)) {
                    if (UndoStep(&undoStack, &map)) {
                        FreeSelectionGrid(&selGrid);
                        selGrid = InitSelectionGrid(map.width, map.height);
                        ClearSelection(&selGrid, &sel);
                        SetStatus(&status, "Undo", SKYBLUE);
                    }
                }
                if (ctrl && IsKeyPressed(KEY_Y)) {
                    if (RedoStep(&undoStack, &map)) {
                        FreeSelectionGrid(&selGrid);
                        selGrid = InitSelectionGrid(map.width, map.height);
                        ClearSelection(&selGrid, &sel);
                        SetStatus(&status, "Redo", SKYBLUE);
                    }
                }
                if (ctrl && IsKeyPressed(KEY_C) && selectionMode && sel.count > 0) {
                    CopySelection(&clipboard, &sel, &map);
                    SetStatus(&status, TextFormat("Copied %d cells", clipboard.count), GREEN);
                }
                if (ctrl && IsKeyPressed(KEY_V) && clipboard.count > 0) {
                    selectionMode = true; brushMode = false; eraserMode = false;
                    pasteMode = true;
                    SetStatus(&status, "Click to paste (R=rotate)", { 255, 200, 0, 255 });
                }

                if (IsKeyPressed(KEY_R) && selectionMode) {
                    if (pasteMode) {
                        RotateClipboardCW(&clipboard);
                        SetStatus(&status, "Rotated clipboard CW", { 255, 200, 0, 255 });
                    }
                    else if (sel.count > 0) {
                        PushUndo(&undoStack, &map);
                        RotateSelectionCW(&map, &selGrid, &sel);
                        RebuildSelectionList(&selGrid, &map, &sel);
                        SetStatus(&status, "Rotated selection CW", { 255, 200, 0, 255 });
                    }
                }
            }

            if (showNewConfirm) {
                if (IsKeyPressed(KEY_ESCAPE)) showNewConfirm = false;
            }
            else if (selectionMode && !mouseOnUI) {
                if (pasteMode) {
                    pasteLogicX = logicX;
                    pasteLogicY = logicY;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        PushUndo(&undoStack, &map);
                        PasteClipboard(&clipboard, &map, &selGrid, &sel, pasteLogicX, pasteLogicY);
                        RebuildSelectionList(&selGrid, &map, &sel);
                        pasteMode = false;
                        SetStatus(&status, "Pasted", GREEN);
                    }
                    if (IsKeyPressed(KEY_ESCAPE)) pasteMode = false;
                }
                else {
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        int ax = logicX + map.offsetX, ay = logicY + map.offsetY;
                        bool onSel = (ax >= 0 && ax < selGrid.width &&
                            ay >= 0 && ay < selGrid.height && selGrid.data[ax][ay]);
                        if (onSel && sel.count > 0) {
                            PushUndo(&undoStack, &map);
                            sel.isMoving = true;
                            sel.moveStartWorld = worldMouse;
                            sel.moveDeltaX = sel.moveDeltaY = 0;
                        }
                        else {
                            if (!IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT))
                                ClearSelection(&selGrid, &sel);
                            sel.isDragSelecting = true;
                            sel.dragStart = sel.dragEnd = worldMouse;
                        }
                    }
                    if (sel.isDragSelecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                        sel.dragEnd = worldMouse;
                    if (sel.isMoving && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                        float dx = worldMouse.x - sel.moveStartWorld.x;
                        float dy = worldMouse.y - sel.moveStartWorld.y;
                        sel.moveDeltaX = (int)floor(dx / CELL_BASE_SIZE + 0.5f);
                        sel.moveDeltaY = (int)floor(dy / CELL_BASE_SIZE + 0.5f);
                    }
                    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                        if (sel.isDragSelecting) {
                            int lx0 = (int)floor(fminf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE);
                            int ly0 = (int)floor(fminf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE);
                            int lx1 = (int)floor(fmaxf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE);
                            int ly1 = (int)floor(fmaxf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE);
                            if (lx0 == lx1 && ly0 == ly1) {
                                int ax = lx0 + map.offsetX, ay = ly0 + map.offsetY;
                                if (ax >= 0 && ax < selGrid.width && ay >= 0 && ay < selGrid.height)
                                    selGrid.data[ax][ay] = !selGrid.data[ax][ay];
                            }
                            else {
                                for (int lx = lx0; lx <= lx1; lx++)
                                    for (int ly = ly0; ly <= ly1; ly++) {
                                        int ax = lx + map.offsetX, ay = ly + map.offsetY;
                                        if (ax >= 0 && ax < selGrid.width && ay >= 0 && ay < selGrid.height)
                                            selGrid.data[ax][ay] = true;
                                    }
                            }
                            RebuildSelectionList(&selGrid, &map, &sel);
                            sel.isDragSelecting = false;
                        }
                        if (sel.isMoving) {
                            CommitMove(&map, &selGrid, &sel, sel.moveDeltaX, sel.moveDeltaY);
                            sel.isMoving = false; sel.moveDeltaX = sel.moveDeltaY = 0;
                        }
                    }
                    if (IsKeyPressed(KEY_ESCAPE)) ClearSelection(&selGrid, &sel);
                    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                        PushUndo(&undoStack, &map);
                        for (int i = 0; i < sel.count; i++)
                            map.data[sel.srcX[i]][sel.srcY[i]].height = 0.0f;
                        ClearSelection(&selGrid, &sel);
                    }
                }
            }

            else if (!selectionMode && !showNewConfirm) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !mouseOnUI && !brushMode && !eraserMode) {
                    Vector2 delta = GetMouseDelta();
                    if (Vector2Length(delta) > 0.5f) {
                        isDragging = true;
                        camera2D.target = Vector2Add(camera2D.target,
                            Vector2Scale(delta, -1.0f / camera2D.zoom));
                    }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && menu.active && !mouseOnUI)
                    menu.active = false;
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !mouseOnUI) {
                    int ax = logicX + map.offsetX, ay = logicY + map.offsetY;
                    if (ax >= 0 && ax < map.width && ay >= 0 && ay < map.height) {
                        menu.active = true;
                        menu.worldPosition = worldMouse;
                        menu.screenPosition = GetWorldToScreen2D(worldMouse, camera2D);
                        menu.targetX = ax; menu.targetY = ay; menu.editMode = 0;
                    }
                }
                int ax = logicX + map.offsetX, ay = logicY + map.offsetY;
                bool inside = (ax >= 0 && ax < map.width && ay >= 0 && ay < map.height);
                if (inside && !mouseOnUI) {
                    if (eraserMode) {
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            PushUndo(&undoStack, &map);
                            strokeStarted = true;
                        }
                        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) map.data[ax][ay].height = 0.0f;
                    }
                    else if (brushMode) {
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            PushUndo(&undoStack, &map);
                            strokeStarted = true;
                        }
                        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) map.data[ax][ay].height = 2.5f;
                    }
                    else {
                        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !isDragging && !menu.active) {
                            PushUndo(&undoStack, &map);
                            map.data[ax][ay].height = (map.data[ax][ay].height == 0.0f) ? 2.5f : 0.0f;
                        }
                    }
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { isDragging = false; strokeStarted = false; }
            }

            Vector2 vTL = GetScreenToWorld2D({ 0, 45 }, camera2D);
            Vector2 vBR = GetScreenToWorld2D({ (float)sw, (float)sh - 50 }, camera2D);
            int vx0 = (int)floor(vTL.x / CELL_BASE_SIZE), vy0 = (int)floor(vTL.y / CELL_BASE_SIZE);
            int vx1 = (int)floor(vBR.x / CELL_BASE_SIZE), vy1 = (int)floor(vBR.y / CELL_BASE_SIZE);
            int step = 20, thr = 5;
            if (isDragging || brushMode || eraserMode || selectionMode) {
                if (vx0 < -map.offsetX + thr) {
                    ExpandMap(&map, map.width + step, map.height, step, 0);
                    ExpandSelectionGrid(&selGrid, map.width, map.height, step, 0);
                }
                if (vx1 > (map.width - map.offsetX) - thr) {
                    ExpandMap(&map, map.width + step, map.height, 0, 0);
                    ExpandSelectionGrid(&selGrid, map.width, map.height, 0, 0);
                }
                if (vy0 < -map.offsetY + thr) {
                    ExpandMap(&map, map.width, map.height + step, 0, step);
                    ExpandSelectionGrid(&selGrid, map.width, map.height, 0, step);
                }
                if (vy1 > (map.height - map.offsetY) - thr) {
                    ExpandMap(&map, map.width, map.height + step, 0, 0);
                    ExpandSelectionGrid(&selGrid, map.width, map.height, 0, 0);
                }
            }

        }
        else {
            if (IsKeyPressed(KEY_BACKSPACE)) cursorLocked = !cursorLocked;
            if (cursorLocked) {
                if (!IsCursorHidden()) DisableCursor();
                UpdateCamera(&camera3D, CAMERA_FIRST_PERSON);
            }
            else {
                if (IsCursorHidden()) EnableCursor();
            }
            camera3D.position.y = 0.8f; camera3D.target.y = 0.8f;
            if (IsKeyPressed(KEY_ESCAPE)) { currentScene = SCENE_WHITE; cursorLocked = true; }
        }

        BeginDrawing();
        ClearBackground({ 25, 25, 25, 255 });

        if (currentScene == SCENE_WHITE) {
            BeginMode2D(camera2D);
            DrawLineEx({ -2000, 0 }, { 2000, 0 }, 2.0f, Fade(RED, 0.5f));
            DrawLineEx({ 0, -2000 }, { 0, 2000 }, 2.0f, Fade(GREEN, 0.5f));

            Vector2 tl = GetScreenToWorld2D({ 0, 45 }, camera2D);
            Vector2 br = GetScreenToWorld2D({ (float)sw, (float)sh - 50 }, camera2D);
            int sLX = (int)floor(tl.x / CELL_BASE_SIZE) - 1;
            int sLY = (int)floor(tl.y / CELL_BASE_SIZE) - 1;
            int eLX = (int)floor(br.x / CELL_BASE_SIZE) + 1;
            int eLY = (int)floor(br.y / CELL_BASE_SIZE) + 1;

            for (int ly = sLY; ly <= eLY; ly++) {
                for (int lx = sLX; lx <= eLX; lx++) {
                    int ax = lx + map.offsetX, ay = ly + map.offsetY;
                    if (ax < 0 || ax >= map.width || ay < 0 || ay >= map.height) continue;
                    Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                    bool isSel = selGrid.data[ax][ay];
                    if (sel.isMoving && isSel)
                        DrawRectangleRec(r, Fade(map.data[ax][ay].height > 0 ? map.data[ax][ay].color : DARKGRAY, 0.25f));
                    else {
                        if (map.data[ax][ay].height > 0) DrawRectangleRec(r, map.data[ax][ay].color);
                        else DrawRectangleLinesEx(r, 0.5f, DARKGRAY);
                    }
                    if (selectionMode && isSel && !sel.isMoving) DrawRectangleLinesEx(r, 2.0f / camera2D.zoom, { 0, 200, 255, 220 });
                    if (selectionMode && isSel && sel.isMoving)  DrawRectangleLinesEx(r, 1.0f / camera2D.zoom, Fade(SKYBLUE, 0.4f));
                    if (camera2D.zoom > 0.6f)
                        DrawText(TextFormat("%d,%d", lx, ly), (int)r.x + 2, (int)r.y + 2, 8, Fade(GRAY, 0.4f));
                }
            }

            if (selectionMode && sel.isMoving) {
                for (int i = 0; i < sel.count; i++) {
                    int lx = sel.srcX[i] - map.offsetX + sel.moveDeltaX;
                    int ly = sel.srcY[i] - map.offsetY + sel.moveDeltaY;
                    int nx = sel.srcX[i] + sel.moveDeltaX, ny = sel.srcY[i] + sel.moveDeltaY;
                    bool inB = (nx >= 0 && nx < map.width && ny >= 0 && ny < map.height);
                    Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                    DrawRectangleRec(r, Fade(sel.cells[i].color, inB ? 0.65f : 0.3f));
                    DrawRectangleLinesEx(r, 2.0f / camera2D.zoom, { 0, 200, 255, 200 });
                }
            }

            if (selectionMode && sel.isDragSelecting) {
                int lx0 = (int)floor(fminf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE);
                int ly0 = (int)floor(fminf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE);
                int lx1 = (int)floor(fmaxf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE) + 1;
                int ly1 = (int)floor(fmaxf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE) + 1;
                Rectangle sr = { lx0 * CELL_BASE_SIZE, ly0 * CELL_BASE_SIZE,
                                 (lx1 - lx0) * CELL_BASE_SIZE, (ly1 - ly0) * CELL_BASE_SIZE };
                DrawRectangleRec(sr, { 0, 160, 255, 30 });
                DrawRectangleLinesEx(sr, 1.5f / camera2D.zoom, { 0, 200, 255, 200 });
            }

            if (pasteMode && clipboard.count > 0) {
                Vector2 wm = GetScreenToWorld2D(mousePos, camera2D);
                int plx = (int)floor(wm.x / CELL_BASE_SIZE);
                int ply = (int)floor(wm.y / CELL_BASE_SIZE);
                for (int i = 0; i < clipboard.count; i++) {
                    int lx = plx + clipboard.relX[i];
                    int ly = ply + clipboard.relY[i];
                    Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                    DrawRectangleRec(r, Fade(clipboard.cells[i].height > 0 ? clipboard.cells[i].color : DARKGRAY, 0.5f));
                    DrawRectangleLinesEx(r, 2.0f / camera2D.zoom, { 255, 200, 0, 200 });
                }
            }

            EndMode2D();

            DrawRectangle(0, 0, sw, 45, { 35, 35, 35, 255 });
            DrawLine(0, 45, sw, 45, GRAY);
            DrawText("Demo", 20, 14, 16, LIGHTGRAY);

            bool canUndo = (undoStack.current > 0);
            bool canRedo = (undoStack.current < undoStack.count - 1);

            GuiSetState(canUndo ? STATE_NORMAL : STATE_DISABLED);
            if (GuiButton(undoBtnRect, "#56#Undo")) {
                if (UndoStep(&undoStack, &map)) {
                    FreeSelectionGrid(&selGrid);
                    selGrid = InitSelectionGrid(map.width, map.height);
                    ClearSelection(&selGrid, &sel);
                    SetStatus(&status, "Undo", SKYBLUE);
                }
            }
            GuiSetState(canRedo ? STATE_NORMAL : STATE_DISABLED);
            if (GuiButton(redoBtnRect, "#57#Redo")) {
                if (RedoStep(&undoStack, &map)) {
                    FreeSelectionGrid(&selGrid);
                    selGrid = InitSelectionGrid(map.width, map.height);
                    ClearSelection(&selGrid, &sel);
                    SetStatus(&status, "Redo", SKYBLUE);
                }
            }
            GuiSetState(STATE_NORMAL);

            if (GuiButton(newBtnRect, "#8#New")) showNewConfirm = true;
            if (GuiButton(saveBtnRect, "#2#Save")) {
                if (SaveMap(saveFile, &map)) SetStatus(&status, TextFormat("Saved: %s", saveFile), GREEN);
                else                         SetStatus(&status, TextFormat("Save FAILED: %s", saveFile), RED);
            }
            if (GuiButton(loadBtnRect, "#1#Load")) {
                bool ok; DynamicMap loaded = LoadMap(saveFile, &ok);
                if (ok) {
                    FreeDynamicMap(&map); map = loaded;
                    FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height);
                    ClearSelection(&selGrid, &sel);
                    ClearUndoStack(&undoStack); PushUndo(&undoStack, &map);
                    SetStatus(&status, TextFormat("Loaded: %s", saveFile), GREEN);
                }
                else {
                    SetStatus(&status, TextFormat("Load FAILED: %s", saveFile), RED);
                }
            }
            if (GuiButton(returnBtnRect, "#101#Return to 0,0")) camera2D.target = { 0, 0 };

            DrawRectangle(0, 46, 140, sh - 96, { 30, 30, 30, 200 });
            DrawLine(140, 46, 140, sh - 50, { 80, 80, 80, 200 });

            Color boxBorder = editingFilename ? Color{ 0, 200, 255, 255 } : Color{ 80, 80, 80, 255 };
            DrawRectangleLinesEx(fileInputRect, 1, boxBorder);
            DrawText(saveFile, (int)fileInputRect.x + 3, (int)fileInputRect.y + 4, 9, LIGHTGRAY);
            if (editingFilename && ((int)(GetTime() * 2) % 2 == 0)) {
                int cx2 = (int)fileInputRect.x + 3 + MeasureText(saveFile, 9);
                if (cx2 < (int)(fileInputRect.x + fileInputRect.width - 2))
                    DrawLine(cx2, (int)fileInputRect.y + 2, cx2, (int)fileInputRect.y + 15, { 0, 200, 255, 255 });
            }

            GuiSetState(brushMode ? STATE_PRESSED : STATE_NORMAL);
            if (GuiButton(brushBtnRect, "#22# Brush")) {
                brushMode = !brushMode; eraserMode = false; selectionMode = false;
                pasteMode = false; ClearSelection(&selGrid, &sel);
            }
            GuiSetState(eraserMode ? STATE_PRESSED : STATE_NORMAL);
            if (GuiButton(eraserBtnRect, "#143# Eraser")) {
                eraserMode = !eraserMode; brushMode = false; selectionMode = false;
                pasteMode = false; ClearSelection(&selGrid, &sel);
            }
            GuiSetState(selectionMode ? STATE_PRESSED : STATE_NORMAL);
            if (GuiButton(selectBtnRect, "#32# Select")) {
                selectionMode = !selectionMode; brushMode = false; eraserMode = false;
                if (!selectionMode) { ClearSelection(&selGrid, &sel); pasteMode = false; }
            }
            GuiSetState(STATE_NORMAL);

            if (selectionMode && sel.count > 0 && !pasteMode) {
                const char* info = sel.isMoving
                    ? TextFormat("Moving %d\nDx:%d Dy:%d", sel.count, sel.moveDeltaX, sel.moveDeltaY)
                    : TextFormat("%d selected\nC=copy  R=rot\nDel=erase", sel.count);
                DrawText(info, 10, 178, 9, { 0, 200, 255, 255 });
            }
            if (pasteMode) {
                DrawText("PASTE MODE\nClick to place\nR=rotate  Esc=cancel", 10, 178, 9, { 255, 200, 0, 255 });
            }

            DrawRectangle(0, sh - 50, sw, 50, { 35, 35, 35, 255 });
            DrawLine(0, sh - 50, sw, sh - 50, GRAY);
            float cx = (float)sw / 2;
            if (GuiButton({ cx - 110, (float)sh - 40, 100, 30 }, "EDITOR"))    currentScene = SCENE_WHITE;
            if (GuiButton({ cx + 10,  (float)sh - 40, 100, 30 }, "MAP")) currentScene = SCENE_BLUE;

            if (status.timer > 0) {
                float alpha = Clamp(status.timer / 0.5f, 0.0f, 1.0f);
                int tw = MeasureText(status.text, 12);
                int bx = sw / 2 - tw / 2 - 8, by = sh - 90;
                DrawRectangle(bx, by, tw + 16, 22, Fade({ 20, 20, 20, 220 }, alpha));
                DrawText(status.text, bx + 8, by + 5, 12, Fade(status.color, alpha));
            }

            if (menu.active) {
                float mx = menu.screenPosition.x + 15, my = menu.screenPosition.y + 15;
                if (menu.editMode == 1) {
                    DrawRectangleRec({ mx, my, 165, 210 }, { 230, 230, 230, 255 });
                    GuiColorPicker({ mx + 10, my + 10, 140, 140 }, NULL, &map.data[menu.targetX][menu.targetY].color);
                    if (GuiButton({ mx + 10, my + 170, 145, 30 }, "Done")) menu.editMode = 0;
                }
                else {
                    float h2 = (menu.editMode == 0) ? 145.0f : (menu.editMode == 2 ? 140.0f : 80.0f);
                    DrawRectangleRec({ mx, my, 130, h2 }, { 45, 45, 45, 255 });
                    DrawRectangleLinesEx({ mx, my, 130, h2 }, 1, LIGHTGRAY);
                    if (menu.editMode == 0) {
                        DrawText(TextFormat("[%d,%d]", menu.targetX - map.offsetX, menu.targetY - map.offsetY),
                            mx + 10, my + 8, 10, SKYBLUE);
                        if (GuiButton({ mx + 10, my + 25,  110, 25 }, "Color"))   menu.editMode = 1;
                        if (GuiButton({ mx + 10, my + 55,  110, 25 }, "Type"))    menu.editMode = 2;
                        if (GuiButton({ mx + 10, my + 85,  110, 25 }, "Texture")) menu.editMode = 3;
                        if (GuiButton({ mx + 10, my + 115, 110, 20 }, "Close"))   menu.active = false;
                    }
                    else if (menu.editMode == 2) {
                        DrawText("Select Type", mx + 10, my + 8, 10, GRAY);
                        if (GuiButton({ mx + 10, my + 25,  110, 25 }, "BLOCK")) { map.data[menu.targetX][menu.targetY].type = TYPE_BLOCK;        menu.editMode = 0; }
                        if (GuiButton({ mx + 10, my + 55,  110, 25 }, "DECOR")) { map.data[menu.targetX][menu.targetY].type = TYPE_DECOR;        menu.editMode = 0; }
                        if (GuiButton({ mx + 10, my + 85,  110, 25 }, "TEX_ONLY")) { map.data[menu.targetX][menu.targetY].type = TYPE_TEXTURE_ONLY; menu.editMode = 0; }
                        if (GuiButton({ mx + 10, my + 115, 110, 20 }, "Back"))    menu.editMode = 0;
                    }
                    else if (menu.editMode == 3) {
                        DrawText("Textures...", mx + 10, my + 10, 10, YELLOW);
                        if (GuiButton({ mx + 10, my + 45, 110, 25 }, "Back")) menu.editMode = 0;
                    }
                }
            }

            if (showNewConfirm) {
                int dw = 280, dh = 100;
                int dx = sw / 2 - dw / 2, dy = sh / 2 - dh / 2;
                DrawRectangle(dx, dy, dw, dh, { 40, 40, 40, 245 });
                DrawRectangleLinesEx({ (float)dx, (float)dy, (float)dw, (float)dh }, 1, LIGHTGRAY);
                DrawText("New map? Unsaved changes will be lost.", dx + 14, dy + 14, 10, LIGHTGRAY);
                if (GuiButton({ (float)dx + 20, (float)dy + 55, 110, 28 }, "#8# Yes, new map")) {
                    FreeDynamicMap(&map); map = InitDynamicMap(50, 50);
                    FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(50, 50);
                    ClearSelection(&selGrid, &sel);
                    ClearUndoStack(&undoStack); PushUndo(&undoStack, &map);
                    pasteMode = false;
                    camera2D.target = { 0, 0 };
                    showNewConfirm = false;
                    SetStatus(&status, "New map created", GREEN);
                }
                if (GuiButton({ (float)dx + 150, (float)dy + 55, 110, 28 }, "Cancel"))
                    showNewConfirm = false;
            }
        }
        else { 
            BeginMode3D(camera3D);
            Vector3 fp = { (map.width / 2.0f) - map.offsetX, 0, (map.height / 2.0f) - map.offsetY };
            DrawPlane(fp, { (float)map.width * 2, (float)map.height * 2 }, { 30, 30, 30, 255 });
            for (int y = 0; y < map.height; y++)
                for (int x = 0; x < map.width; x++)
                    if (map.data[x][y].height > 0) {
                        Vector3 pos = { (float)(x - map.offsetX), map.data[x][y].height / 2.0f, (float)(y - map.offsetY) };
                        DrawCube(pos, 1.0f, map.data[x][y].height, 1.0f, map.data[x][y].color);
                        DrawCubeWires(pos, 1.0f, map.data[x][y].height, 1.0f, Fade(BLACK, 0.3f));
                    }
            EndMode3D();
            DrawRectangle(0, sh - 50, sw, 50, { 35, 35, 35, 200 });
            float cx2 = (float)sw / 2;
            if (GuiButton({ cx2 - 110, (float)sh - 40, 100, 30 }, "EDITOR"))    currentScene = SCENE_WHITE;
            if (GuiButton({ cx2 + 10,  (float)sh - 40, 100, 30 }, "MAP")) currentScene = SCENE_BLUE;
        }

        DrawFPS(sw - 80, sh - 35);
        EndDrawing();
    }

    if (sel.cells) free(sel.cells);
    if (sel.srcX)  free(sel.srcX);
    if (sel.srcY)  free(sel.srcY);
    FreeClipboard(&clipboard);
    FreeSelectionGrid(&selGrid);
    FreeDynamicMap(&map);
    ClearUndoStack(&undoStack);
    CloseWindow();
    return 0;
}