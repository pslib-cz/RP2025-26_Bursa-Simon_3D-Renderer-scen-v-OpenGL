#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include <cstdint>

typedef enum { SCENE_LAUNCH = 0, SCENE_WHITE, SCENE_BLUE } GameScene;
typedef enum { TYPE_BLOCK = 0, TYPE_DECOR, TYPE_TEXTURE_ONLY } CellType;
typedef enum { TOOL_NONE = 0, TOOL_BRUSH, TOOL_ERASER, TOOL_SELECT, TOOL_LINE, TOOL_RECT, TOOL_CIRCLE } ToolMode;
typedef enum { MIRROR_NONE = 0, MIRROR_X, MIRROR_Y, MIRROR_BOTH } MirrorMode;

#define CELL_BASE_SIZE 40.0f
#define MAX_UNZOOM 0.15f
#define MAX_ZOOM   3.0f
#define UNDO_MAX   64

typedef struct {
    float    height;
    Color    color;
    CellType type;
    bool     solid; 
} Cell;

typedef struct {
    Cell** data;
    int    width, height;
    int    offsetX, offsetY;
} DynamicMap;

typedef struct {
    bool    active;
    Vector2 worldPosition;
    Vector2 screenPosition;
    int     targetX, targetY;
    int     editMode; 
} ContextMenu;

typedef struct {
    Cell* cells;
    int* srcX;
    int* srcY;
    int     count, capacity;
    bool    isDragSelecting;
    Vector2 dragStart, dragEnd;
    bool    isMoving;
    Vector2 moveStartWorld;
    int     moveDeltaX, moveDeltaY;
} SelectionState;

typedef struct {
    bool** data;
    int    width, height;
} SelectionGrid;

typedef struct {
    Cell* cells;
    int* relX, * relY;
    int   count, boundsW, boundsH;
} Clipboard;

typedef struct {
    Cell** data;
    int    width, height, offsetX, offsetY;
} MapSnapshot;

typedef struct {
    MapSnapshot snapshots[UNDO_MAX];
    int         count, current;
} UndoStack;

typedef struct { char text[128]; float timer; Color color; } StatusMsg;

#define MAP_MAGIC   "MAP\0"
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
    free(s->data); s->data = nullptr;
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
        us->count = UNDO_MAX - 1; us->current = us->count - 1;
    }
    us->snapshots[us->count] = SnapshotMap(m);
    us->current = us->count; us->count++;
}
bool UndoStep(UndoStack* us, DynamicMap* m) {
    if (us->current <= 0) return false;
    us->current--; RestoreSnapshot(m, &us->snapshots[us->current]); return true;
}
bool RedoStep(UndoStack* us, DynamicMap* m) {
    if (us->current >= us->count - 1) return false;
    us->current++; RestoreSnapshot(m, &us->snapshots[us->current]); return true;
}
void InitUndoStack(UndoStack* us) { memset(us->snapshots, 0, sizeof(us->snapshots)); us->count = 0; us->current = -1; }
void ClearUndoStack(UndoStack* us) { for (int i = 0; i < us->count; i++) FreeSnapshot(&us->snapshots[i]); us->count = 0; us->current = -1; }

void FreeClipboard(Clipboard* cb) {
    free(cb->cells); cb->cells = nullptr;
    free(cb->relX);  cb->relX = nullptr;
    free(cb->relY);  cb->relY = nullptr;
    cb->count = 0;
}
void CopySelection(Clipboard* cb, const SelectionState* sel, const DynamicMap*) {
    if (sel->count == 0) return;
    FreeClipboard(cb);
    int minX = sel->srcX[0], minY = sel->srcY[0], maxX = minX, maxY = minY;
    for (int i = 1; i < sel->count; i++) {
        if (sel->srcX[i] < minX) minX = sel->srcX[i]; if (sel->srcY[i] < minY) minY = sel->srcY[i];
        if (sel->srcX[i] > maxX) maxX = sel->srcX[i]; if (sel->srcY[i] > maxY) maxY = sel->srcY[i];
    }
    cb->boundsW = maxX - minX + 1; cb->boundsH = maxY - minY + 1;
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
void PasteClipboard(Clipboard* cb, DynamicMap* map, SelectionGrid* sg, SelectionState* sel, int plx, int ply) {
    if (cb->count == 0) return;
    for (int x = 0; x < sg->width; x++) memset(sg->data[x], 0, sg->height * sizeof(bool));
    sel->count = 0; sel->isMoving = false;
    for (int i = 0; i < cb->count; i++) {
        int lx = plx + cb->relX[i], ly = ply + cb->relY[i];
        int ax = lx + map->offsetX, ay = ly + map->offsetY;
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            map->data[ax][ay] = cb->cells[i]; sg->data[ax][ay] = true;
        }
    }
}
void RotateClipboardCW(Clipboard* cb) {
    if (cb->count == 0) return;
    int newW = cb->boundsH, newH = cb->boundsW;
    for (int i = 0; i < cb->count; i++) { int ox = cb->relX[i], oy = cb->relY[i]; cb->relX[i] = (cb->boundsH - 1) - oy; cb->relY[i] = ox; }
    cb->boundsW = newW; cb->boundsH = newH;
}
void MirrorClipboardX(Clipboard* cb) {
    if (cb->count == 0) return;
    for (int i = 0; i < cb->count; i++) cb->relX[i] = (cb->boundsW - 1) - cb->relX[i];
}
void MirrorClipboardY(Clipboard* cb) {
    if (cb->count == 0) return;
    for (int i = 0; i < cb->count; i++) cb->relY[i] = (cb->boundsH - 1) - cb->relY[i];
}
void RotateSelectionCW(DynamicMap* map, SelectionGrid* sg, SelectionState* sel) {
    if (sel->count == 0) return;
    int minX = sel->srcX[0], minY = sel->srcY[0], maxX = minX, maxY = minY;
    for (int i = 1; i < sel->count; i++) {
        if (sel->srcX[i] < minX) minX = sel->srcX[i]; if (sel->srcY[i] < minY) minY = sel->srcY[i];
        if (sel->srcX[i] > maxX) maxX = sel->srcX[i]; if (sel->srcY[i] > maxY) maxY = sel->srcY[i];
    }
    int bH = maxY - minY + 1;
    Cell* tmp = (Cell*)malloc(sel->count * sizeof(Cell));
    int* nxArr = (int*)malloc(sel->count * sizeof(int));
    int* nyArr = (int*)malloc(sel->count * sizeof(int));
    for (int i = 0; i < sel->count; i++) {
        tmp[i] = sel->cells[i];
        nxArr[i] = minX + ((bH - 1) - (sel->srcY[i] - minY));
        nyArr[i] = minY + (sel->srcX[i] - minX);
    }
    for (int i = 0; i < sel->count; i++) map->data[sel->srcX[i]][sel->srcY[i]] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false };
    for (int x = 0; x < sg->width; x++) memset(sg->data[x], 0, sg->height * sizeof(bool));
    for (int i = 0; i < sel->count; i++) {
        int ax = nxArr[i], ay = nyArr[i];
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            map->data[ax][ay] = tmp[i]; sg->data[ax][ay] = true; sel->srcX[i] = ax; sel->srcY[i] = ay;
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
    fwrite(&m->width, sizeof(int), 1, f); fwrite(&m->height, sizeof(int), 1, f);
    fwrite(&m->offsetX, sizeof(int), 1, f); fwrite(&m->offsetY, sizeof(int), 1, f);
    for (int x = 0; x < m->width; x++)
        for (int y = 0; y < m->height; y++) {
            const Cell* c = &m->data[x][y];
            fwrite(&c->height, sizeof(float), 1, f);
            uint8_t rgba[4] = { c->color.r, c->color.g, c->color.b, c->color.a };
            fwrite(rgba, 1, 4, f);
            int t = (int)c->type; fwrite(&t, sizeof(int), 1, f);
            fwrite(&c->solid, sizeof(bool), 1, f);
        }
    fclose(f); return true;
}
DynamicMap LoadMap(const char* path, bool* ok) {
    *ok = false; DynamicMap m = { nullptr, 0, 0, 0, 0 };
    FILE* f = fopen(path, "rb"); if (!f) return m;
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
            int t; fread(&t, sizeof(int), 1, f); c.type = (CellType)t;
            if (fread(&c.solid, sizeof(bool), 1, f) != 1) c.solid = false;
            m.data[x][y] = c;
        }
    }
    fclose(f); *ok = true; return m;
}
void FreeDynamicMap(DynamicMap* m) {
    for (int i = 0; i < m->width; i++) free(m->data[i]);
    free(m->data); m->data = nullptr; m->width = 0; m->height = 0;
}

SelectionGrid InitSelectionGrid(int w, int h) {
    SelectionGrid g; g.width = w; g.height = h;
    g.data = (bool**)malloc(w * sizeof(bool*));
    for (int i = 0; i < w; i++) g.data[i] = (bool*)calloc(h, sizeof(bool));
    return g;
}
void FreeSelectionGrid(SelectionGrid* sg) {
    for (int i = 0; i < sg->width; i++) free(sg->data[i]);
    free(sg->data); sg->data = nullptr; sg->width = 0; sg->height = 0;
}
void ExpandSelectionGrid(SelectionGrid* sg, int newW, int newH, int shiftX, int shiftY) {
    bool** nd = (bool**)malloc(newW * sizeof(bool*));
    for (int i = 0; i < newW; i++) {
        nd[i] = (bool*)calloc(newH, sizeof(bool));
        for (int j = 0; j < newH; j++) {
            int ox = i - shiftX, oy = j - shiftY;
            if (ox >= 0 && ox < sg->width && oy >= 0 && oy < sg->height) nd[i][j] = sg->data[ox][oy];
        }
    }
    FreeSelectionGrid(sg); sg->data = nd; sg->width = newW; sg->height = newH;
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
    for (int x = 0; x < sg->width; x++) for (int y = 0; y < sg->height; y++) if (sg->data[x][y]) sel->count++;
    AddToCapacity(sel, sel->count);
    int idx = 0;
    for (int x = 0; x < sg->width; x++) for (int y = 0; y < sg->height; y++) if (sg->data[x][y]) {
        sel->srcX[idx] = x; sel->srcY[idx] = y; sel->cells[idx] = map->data[x][y]; idx++;
    }
}

DynamicMap InitDynamicMap(int w, int h) {
    DynamicMap m; m.width = w; m.height = h;
    m.offsetX = w / 2; m.offsetY = h / 2;
    m.data = (Cell**)malloc(w * sizeof(Cell*));
    for (int i = 0; i < w; i++) {
        m.data[i] = (Cell*)malloc(h * sizeof(Cell));
        for (int j = 0; j < h; j++) m.data[i][j] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false };
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
                ? m->data[ox][oy] : Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false };
        }
    }
    FreeDynamicMap(m);
    m->data = nd; m->width = newW; m->height = newH;
    m->offsetX += shiftX; m->offsetY += shiftY;
}

void CommitMove(DynamicMap* map, SelectionGrid* sg, SelectionState* sel, int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    for (int i = 0; i < sel->count; i++) map->data[sel->srcX[i]][sel->srcY[i]].height = -1.0f;
    for (int i = 0; i < sel->count; i++) {
        int nx = sel->srcX[i] + dx, ny = sel->srcY[i] + dy;
        if (nx >= 0 && nx < map->width && ny >= 0 && ny < map->height) map->data[nx][ny] = sel->cells[i];
    }
    for (int x = 0; x < map->width; x++) for (int y = 0; y < map->height; y++)
        if (map->data[x][y].height == -1.0f) map->data[x][y] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false };
    for (int x = 0; x < sg->width; x++) memset(sg->data[x], 0, sg->height);
    for (int i = 0; i < sel->count; i++) {
        int nx = sel->srcX[i] + dx, ny = sel->srcY[i] + dy;
        if (nx >= 0 && nx < sg->width && ny >= 0 && ny < sg->height) sg->data[nx][ny] = true;
    }
    RebuildSelectionList(sg, map, sel);
}

void SetStatus(StatusMsg* s, const char* txt, Color c) {
    strncpy(s->text, txt, 127); s->text[127] = '\0'; s->timer = 2.5f; s->color = c;
}

void DrawLine2DMap(DynamicMap* map, int x0, int y0, int x1, int y1, Cell cell, bool erase) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        int ax = x0 + map->offsetX, ay = y0 + map->offsetY;
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            if (erase) map->data[ax][ay] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false };
            else       map->data[ax][ay] = cell;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

#define MAX_PREVIEW 4096
struct PreviewCell { int lx, ly; };
static PreviewCell gPreview[MAX_PREVIEW];
static int         gPreviewCount = 0;

void ClearPreview() { gPreviewCount = 0; }

void AddPreview(int lx, int ly) {
    if (gPreviewCount < MAX_PREVIEW) { gPreview[gPreviewCount].lx = lx; gPreview[gPreviewCount].ly = ly; gPreviewCount++; }
}

void BuildLinePreview(int x0, int y0, int x1, int y1) {
    ClearPreview();
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1, err = dx - dy;
    for (;;) { AddPreview(x0, y0); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; } }
}

void BuildRectPreview(int x0, int y0, int x1, int y1) {
    ClearPreview();
    int lx0 = (x0 < x1) ? x0 : x1, lx1 = (x0 < x1) ? x1 : x0;
    int ly0 = (y0 < y1) ? y0 : y1, ly1 = (y0 < y1) ? y1 : y0;
    for (int x = lx0; x <= lx1; x++) { AddPreview(x, ly0); if (ly1 != ly0) AddPreview(x, ly1); }
    for (int y = ly0 + 1; y < ly1; y++) { AddPreview(lx0, y); if (lx1 != lx0) AddPreview(lx1, y); }
}

void BuildCirclePreview(int cx, int cy, int rx, int ry) {
    ClearPreview();
    int x = 0, y = ry;
    long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
    long dx = 0, dy = 2 * rx2 * y, d = ry2 - rx2 * ry + rx2 / 4;
    auto put4 = [&](int px, int py) { AddPreview(cx + px, cy + py); AddPreview(cx - px, cy + py); AddPreview(cx + px, cy - py); AddPreview(cx - px, cy - py); };
    while (dx < dy) { put4(x, y); x++; dx += 2 * ry2; if (d < 0) d += ry2 + dx; else { y--; dy -= 2 * rx2; d += ry2 + dx - dy; } }
    d = (long)ry2 * (x + 0.5f) * (x + 0.5f) + (long)rx2 * (y - 1) * (y - 1) - (long)rx2 * ry2;
    while (y >= 0) { put4(x, y); y--; dy -= 2 * rx2; if (d > 0) d += rx2 - dy; else { x++; dx += 2 * ry2; d += rx2 - dy + dx; } }
}

void CommitPreview(DynamicMap* map, Cell cell) {
    for (int i = 0; i < gPreviewCount; i++) {
        int ax = gPreview[i].lx + map->offsetX, ay = gPreview[i].ly + map->offsetY;
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) map->data[ax][ay] = cell;
    }
}


bool TryParseHex(const char* s, Color* out) {
    const char* p = s;
    if (*p == '#') p++;
    if (strlen(p) != 6) return false;
    unsigned int r, g, b;
    if (sscanf(p, "%02x%02x%02x", &r, &g, &b) != 3) return false;
    out->r = (uint8_t)r; out->g = (uint8_t)g; out->b = (uint8_t)b; out->a = 255; return true;
}


int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1200, 800, "Demo");
    SetWindowMinSize(800, 600);

    GameScene currentScene = SCENE_LAUNCH;
    ContextMenu menu = { false, {0,0}, {0,0}, 0, 0, 0 };
    ToolMode toolMode = TOOL_NONE;
    MirrorMode mirrorMode = MIRROR_NONE;
    bool cursorLocked = true;
    StatusMsg status = { "", 0.0f, WHITE };

    char launchFilePath[256] = "map.bin";
    bool launchEditingFile = false;
    int  launchFileLen = (int)strlen(launchFilePath);

    DynamicMap map = InitDynamicMap(50, 50);
    SelectionGrid selGrid = InitSelectionGrid(map.width, map.height);
    SelectionState sel = { nullptr, nullptr, nullptr, 0, 0,
                           false, {0,0}, {0,0}, false, {0,0}, 0, 0 };
    Clipboard clipboard = { nullptr, nullptr, nullptr, 0, 0, 0 };

    bool pasteMode = false;
    int  pasteLogicX = 0, pasteLogicY = 0;

    UndoStack undoStack;
    InitUndoStack(&undoStack);
    PushUndo(&undoStack, &map);

    bool showNewConfirm = false;

  
    Color brushColor = { 200, 200, 200, 255 };

   
    bool  shapeFirstClick = false;
    int   shapeStartLX = 0, shapeStartLY = 0;

    
    Color skyColor = { 25, 25, 45, 255 };
    bool  showSkyColorPicker = false;

  
    char hexInputBuf[16] = "";
    bool hexEditing = false;
    char rBuf[8] = "", gBuf2[8] = "", bBuf[8] = "";
    bool rEdit = false, gEdit = false, bEdit = false;

    auto syncBufsFromColor = [&]() {
        sprintf(hexInputBuf, "#%02X%02X%02X", brushColor.r, brushColor.g, brushColor.b);
        sprintf(rBuf, "%d", brushColor.r); sprintf(gBuf2, "%d", brushColor.g); sprintf(bBuf, "%d", brushColor.b);
        };
    syncBufsFromColor();

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
    bool strokeStarted = false;

    char saveFile[256] = "map.bin";
    bool editingFilename = false;
    int  fileLen = (int)strlen(saveFile);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (status.timer > 0) status.timer -= dt;

        int sw = GetScreenWidth(), sh = GetScreenHeight();
        Vector2 mousePos = GetMousePosition();

        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        if (currentScene == SCENE_LAUNCH) {
            if (IsCursorHidden()) EnableCursor();
            BeginDrawing();
            ClearBackground({ 20, 20, 28, 255 });

            int titleFontSize = 48;
            const char* title = "DEMO";
            int tw = MeasureText(title, titleFontSize);
            int cx = sw / 2, cy = sh / 2;
            DrawText(title, cx - tw / 2, cy - 140, titleFontSize, { 200, 220, 255, 255 });
            DrawLine(cx - 100, cy - 82, cx + 100, cy - 82, { 80, 120, 200, 180 });

            Rectangle fileR = { (float)(cx - 140), (float)(cy - 55), 280, 28 };
            bool fileHov = CheckCollisionPointRec(mousePos, fileR);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) launchEditingFile = fileHov;
            DrawRectangleLinesEx(fileR, 1, launchEditingFile ? Color{ 0, 200, 255, 255 } : (fileHov ? Color{ 120, 120, 180, 255 } : Color{ 60, 60, 80, 255 }));
            DrawText(launchFilePath, (int)fileR.x + 6, (int)fileR.y + 8, 10, { 180, 200, 255, 255 });
            DrawText("File:", cx - 140, cy - 70, 9, { 120, 140, 180, 200 });

            if (launchEditingFile) {
                int key;
                while ((key = GetCharPressed()) > 0) if (key >= 32 && launchFileLen < 255) { launchFilePath[launchFileLen++] = (char)key; launchFilePath[launchFileLen] = '\0'; }
                if (IsKeyPressed(KEY_BACKSPACE) && launchFileLen > 0) launchFilePath[--launchFileLen] = '\0';
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) launchEditingFile = false;
                if ((int)(GetTime() * 2) % 2 == 0) {
                    int cx2 = (int)fileR.x + 6 + MeasureText(launchFilePath, 10);
                    DrawLine(cx2, (int)fileR.y + 4, cx2, (int)fileR.y + 22, { 0, 200, 255, 255 });
                }
            }

   
            Rectangle newR = { (float)(cx - 100), (float)(cy), 200, 42 };
            bool newHov = CheckCollisionPointRec(mousePos, newR);
            DrawRectangle((int)newR.x, (int)newR.y, (int)newR.width, (int)newR.height, newHov ? Color{ 60, 100, 180, 255 } : Color{ 40, 60, 120, 255 });
            DrawRectangleLinesEx(newR, 1, { 100, 140, 220, 255 });
            int ntw = MeasureText("NEW MAP", 16);
            DrawText("NEW MAP", cx - ntw / 2, (int)newR.y + 13, 16, { 220, 235, 255, 255 });

            Rectangle openR = { (float)(cx - 100), (float)(cy + 56), 200, 42 };
            bool openHov = CheckCollisionPointRec(mousePos, openR);
            DrawRectangle((int)openR.x, (int)openR.y, (int)openR.width, (int)openR.height, openHov ? Color{ 50, 90, 60, 255 } : Color{ 30, 60, 40, 255 });
            DrawRectangleLinesEx(openR, 1, { 80, 160, 100, 255 });
            int otw = MeasureText("OPEN MAP", 16);
            DrawText("OPEN MAP", cx - otw / 2, (int)openR.y + 13, 16, { 200, 240, 210, 255 });

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && newHov && !launchEditingFile) {
                strncpy(saveFile, launchFilePath, 255); fileLen = (int)strlen(saveFile);
                currentScene = SCENE_WHITE;
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && openHov && !launchEditingFile) {
                strncpy(saveFile, launchFilePath, 255); fileLen = (int)strlen(saveFile);
                bool ok; DynamicMap loaded = LoadMap(saveFile, &ok);
                if (ok) {
                    FreeDynamicMap(&map); map = loaded;
                    FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height);
                    ClearSelection(&selGrid, &sel);
                    ClearUndoStack(&undoStack); PushUndo(&undoStack, &map);
                    currentScene = SCENE_WHITE;
                    SetStatus(&status, TextFormat("Loaded: %s", saveFile), GREEN);
                }
                else SetStatus(&status, TextFormat("Load FAILED: %s", saveFile), RED);
            }

            DrawFPS(sw - 80, sh - 20);
            EndDrawing();
            continue;
        }

        if (currentScene == SCENE_WHITE) {
            if (IsCursorHidden()) EnableCursor();

            Rectangle topBar = { 0, 0, (float)sw, 45 };
            Rectangle bottomBar = { 0, (float)sh - 50, (float)sw, 50 };
            Rectangle sidebarArea = { 0, 46, 160, (float)(sh - 96) };

            bool mouseOnUI = false;
            auto checkRect = [&](Rectangle r) { if (CheckCollisionPointRec(mousePos, r)) mouseOnUI = true; };
            checkRect(topBar); checkRect(bottomBar); checkRect(sidebarArea);
            if (menu.active) {
               
                float mw = 0, mh = 0;
                if (menu.editMode == 1) { mw = 165; mh = 230; }
                else if (menu.editMode == 0) { mw = 130; mh = 185; }
                else if (menu.editMode == 2) { mw = 130; mh = 140; }
                else if (menu.editMode == 3) { mw = 130; mh = 80; }
                else if (menu.editMode == 4) { mw = 160; mh = 80; }
                checkRect({ menu.screenPosition.x + 15, menu.screenPosition.y + 15, mw, mh });
            }
            if (showNewConfirm) mouseOnUI = true;
            
            Rectangle colorPanelRect = { 0, (float)(sh - 50 - 310), 160, 310 };
            if (CheckCollisionPointRec(mousePos, colorPanelRect)) mouseOnUI = true;

         
            Rectangle fileInputRect = { 8, 50, 144, 18 };
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                editingFilename = CheckCollisionPointRec(mousePos, fileInputRect) && !mouseOnUI;
            if (!CheckCollisionPointRec(mousePos, fileInputRect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && editingFilename)
                editingFilename = false;
            if (editingFilename) {
                int key;
                while ((key = GetCharPressed()) > 0) if (key >= 32 && fileLen < 255) { saveFile[fileLen++] = (char)key; saveFile[fileLen] = '\0'; }
                if (IsKeyPressed(KEY_BACKSPACE) && fileLen > 0) saveFile[--fileLen] = '\0';
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) editingFilename = false;
            }


            float wheel = GetMouseWheelMove();
            if (wheel != 0 && !mouseOnUI) {
                if (menu.active) menu.active = false; 
                Vector2 mw2 = GetScreenToWorld2D(mousePos, camera2D);
                camera2D.offset = mousePos; camera2D.target = mw2;
                camera2D.zoom = Clamp(camera2D.zoom + wheel * 0.12f, MAX_UNZOOM, MAX_ZOOM);
            }

            Vector2 worldMouse = GetScreenToWorld2D(mousePos, camera2D);
            int logicX = (int)floor(worldMouse.x / CELL_BASE_SIZE);
            int logicY = (int)floor(worldMouse.y / CELL_BASE_SIZE);

            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !mouseOnUI) {
                Vector2 delta = GetMouseDelta();
                if (Vector2Length(delta) > 0.5f)
                    camera2D.target = Vector2Add(camera2D.target, Vector2Scale(delta, -1.0f / camera2D.zoom));
            }

            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && (toolMode == TOOL_BRUSH || toolMode == TOOL_ERASER)) {
                toolMode = TOOL_NONE;
            }

            if (!editingFilename && !showNewConfirm) {
                if (ctrl && IsKeyPressed(KEY_Z)) { if (UndoStep(&undoStack, &map)) { FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height); ClearSelection(&selGrid, &sel); SetStatus(&status, "Undo", SKYBLUE); } }
                if (ctrl && IsKeyPressed(KEY_Y)) { if (RedoStep(&undoStack, &map)) { FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height); ClearSelection(&selGrid, &sel); SetStatus(&status, "Redo", SKYBLUE); } }
                if (ctrl && IsKeyPressed(KEY_C) && toolMode == TOOL_SELECT && sel.count > 0) { CopySelection(&clipboard, &sel, &map); SetStatus(&status, TextFormat("Copied %d cells", clipboard.count), GREEN); }
                if (ctrl && IsKeyPressed(KEY_V) && clipboard.count > 0) {
                    toolMode = TOOL_SELECT; pasteMode = false;
                    PushUndo(&undoStack, &map);
                    PasteClipboard(&clipboard, &map, &selGrid, &sel, logicX, logicY);
                    RebuildSelectionList(&selGrid, &map, &sel);
                    SetStatus(&status, "Pasted", GREEN);
                }
                if (IsKeyPressed(KEY_R) && toolMode == TOOL_SELECT) {
                    if (pasteMode) { RotateClipboardCW(&clipboard); SetStatus(&status, "Rotated clipboard CW", { 255,200,0,255 }); }
                    else if (sel.count > 0) { PushUndo(&undoStack, &map); RotateSelectionCW(&map, &selGrid, &sel); RebuildSelectionList(&selGrid, &map, &sel); SetStatus(&status, "Rotated selection CW", { 255,200,0,255 }); }
                }

                if (ctrl && IsKeyPressed(KEY_LEFT) && toolMode == TOOL_SELECT && sel.count > 0) {
    
                    if (clipboard.count == 0 || sel.count > 0) {
                        CopySelection(&clipboard, &sel, &map);
                        MirrorClipboardX(&clipboard);
                        PasteClipboard(&clipboard, &map, &selGrid, &sel, sel.srcX[0] - map.offsetX - clipboard.boundsW + 1, sel.srcY[0] - map.offsetY);
                        RebuildSelectionList(&selGrid, &map, &sel);
                        FreeClipboard(&clipboard);
                        SetStatus(&status, "Mirrored X", { 255,200,0,255 });
                    }
                }

                if (ctrl && IsKeyPressed(KEY_UP) && toolMode == TOOL_SELECT && sel.count > 0) {
                    CopySelection(&clipboard, &sel, &map);
                    MirrorClipboardY(&clipboard);
                    PasteClipboard(&clipboard, &map, &selGrid, &sel, sel.srcX[0] - map.offsetX, sel.srcY[0] - map.offsetY - clipboard.boundsH + 1);
                    RebuildSelectionList(&selGrid, &map, &sel);
                    FreeClipboard(&clipboard);
                    SetStatus(&status, "Mirrored Y", { 255,200,0,255 });
                }
            }

            if (showNewConfirm) { if (IsKeyPressed(KEY_ESCAPE)) showNewConfirm = false; }
            else if (toolMode == TOOL_SELECT && !mouseOnUI) {
                if (pasteMode) {
                    pasteLogicX = logicX; pasteLogicY = logicY;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { PushUndo(&undoStack, &map); PasteClipboard(&clipboard, &map, &selGrid, &sel, pasteLogicX, pasteLogicY); RebuildSelectionList(&selGrid, &map, &sel); pasteMode = false; SetStatus(&status, "Pasted", GREEN); }
                    if (IsKeyPressed(KEY_ESCAPE)) pasteMode = false;
                }
                else {
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        int ax = logicX + map.offsetX, ay = logicY + map.offsetY;
                        bool onSel = (ax >= 0 && ax < selGrid.width && ay >= 0 && ay < selGrid.height && selGrid.data[ax][ay]);
                        if (onSel && sel.count > 0) { PushUndo(&undoStack, &map); sel.isMoving = true; sel.moveStartWorld = worldMouse; sel.moveDeltaX = sel.moveDeltaY = 0; }
                        else { if (!IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT)) ClearSelection(&selGrid, &sel); sel.isDragSelecting = true; sel.dragStart = sel.dragEnd = worldMouse; }
                    }
                    if (sel.isDragSelecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) sel.dragEnd = worldMouse;
                    if (sel.isMoving && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                        float ddx = worldMouse.x - sel.moveStartWorld.x, ddy = worldMouse.y - sel.moveStartWorld.y;
                        sel.moveDeltaX = (int)floor(ddx / CELL_BASE_SIZE + 0.5f);
                        sel.moveDeltaY = (int)floor(ddy / CELL_BASE_SIZE + 0.5f);
                    }
                    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                        if (sel.isDragSelecting) {
                            int lx0 = (int)floor(fminf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE);
                            int ly0 = (int)floor(fminf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE);
                            int lx1 = (int)floor(fmaxf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE);
                            int ly1 = (int)floor(fmaxf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE);
                            if (lx0 == lx1 && ly0 == ly1) {
                                int ax = lx0 + map.offsetX, ay = ly0 + map.offsetY;
                                if (ax >= 0 && ax < selGrid.width && ay >= 0 && ay < selGrid.height) selGrid.data[ax][ay] = !selGrid.data[ax][ay];
                            }
                            else { for (int lx = lx0; lx <= lx1; lx++) for (int ly = ly0; ly <= ly1; ly++) { int ax = lx + map.offsetX, ay = ly + map.offsetY; if (ax >= 0 && ax < selGrid.width && ay >= 0 && ay < selGrid.height) selGrid.data[ax][ay] = true; } }
                            RebuildSelectionList(&selGrid, &map, &sel); sel.isDragSelecting = false;
                        }
                        if (sel.isMoving) { CommitMove(&map, &selGrid, &sel, sel.moveDeltaX, sel.moveDeltaY); sel.isMoving = false; sel.moveDeltaX = sel.moveDeltaY = 0; }
                    }
                    if (IsKeyPressed(KEY_ESCAPE)) ClearSelection(&selGrid, &sel);
                    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                        PushUndo(&undoStack, &map);
                        for (int i = 0; i < sel.count; i++) { map.data[sel.srcX[i]][sel.srcY[i]] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false }; }
                        ClearSelection(&selGrid, &sel);
                    }
                }
            }
            else if ((toolMode == TOOL_LINE || toolMode == TOOL_RECT || toolMode == TOOL_CIRCLE) && !mouseOnUI) {
                if (shapeFirstClick) {
                    if (toolMode == TOOL_LINE)   BuildLinePreview(shapeStartLX, shapeStartLY, logicX, logicY);
                    else if (toolMode == TOOL_RECT) BuildRectPreview(shapeStartLX, shapeStartLY, logicX, logicY);
                    else {
                        int rx = abs(logicX - shapeStartLX), ry = abs(logicY - shapeStartLY);
                        BuildCirclePreview(shapeStartLX, shapeStartLY, rx, ry);
                    }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (!shapeFirstClick) { shapeFirstClick = true; shapeStartLX = logicX; shapeStartLY = logicY; ClearPreview(); }
                    else {
                        PushUndo(&undoStack, &map);
                        Cell c = { 2.5f, brushColor, TYPE_BLOCK, false };
                        CommitPreview(&map, c);
                        shapeFirstClick = false; ClearPreview();
                    }
                }
                if (IsKeyPressed(KEY_ESCAPE)) { shapeFirstClick = false; ClearPreview(); }
            }
            else if (toolMode != TOOL_SELECT && !showNewConfirm && !mouseOnUI) {

                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && toolMode == TOOL_NONE) {
                    Vector2 delta = GetMouseDelta();
                    if (Vector2Length(delta) > 0.5f) {
                        isDragging = true;
                        camera2D.target = Vector2Add(camera2D.target, Vector2Scale(delta, -1.0f / camera2D.zoom));
                    }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && menu.active && !mouseOnUI) menu.active = false;
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !mouseOnUI && toolMode == TOOL_NONE) {
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
                    if (toolMode == TOOL_ERASER) {
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { PushUndo(&undoStack, &map); strokeStarted = true; }
                        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) map.data[ax][ay] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false };
                    }
                    else if (toolMode == TOOL_BRUSH) {
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { PushUndo(&undoStack, &map); strokeStarted = true; }
                        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { map.data[ax][ay].height = 2.5f; map.data[ax][ay].color = brushColor; }
                    }
                    else {
                        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !isDragging && !menu.active) {
                            PushUndo(&undoStack, &map);
                            if (map.data[ax][ay].height == 0.0f) { map.data[ax][ay].height = 2.5f; map.data[ax][ay].color = brushColor; }
                            else { map.data[ax][ay] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false }; }
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
            bool active2D = (isDragging || toolMode != TOOL_NONE);
            if (active2D) {
                if (vx0 < -map.offsetX + thr) { ExpandMap(&map, map.width + step, map.height, step, 0); ExpandSelectionGrid(&selGrid, map.width, map.height, step, 0); }
                if (vx1 > (map.width - map.offsetX) - thr) { ExpandMap(&map, map.width + step, map.height, 0, 0);    ExpandSelectionGrid(&selGrid, map.width, map.height, 0, 0); }
                if (vy0 < -map.offsetY + thr) { ExpandMap(&map, map.width, map.height + step, 0, step); ExpandSelectionGrid(&selGrid, map.width, map.height, 0, step); }
                if (vy1 > (map.height - map.offsetY) - thr) { ExpandMap(&map, map.width, map.height + step, 0, 0);    ExpandSelectionGrid(&selGrid, map.width, map.height, 0, 0); }
            }

       
            BeginDrawing();
            ClearBackground({ 25, 25, 25, 255 });
            BeginMode2D(camera2D);

            DrawLineEx({ -2000, 0 }, { 2000, 0 }, 2.0f, Fade(RED, 0.5f));
            DrawLineEx({ 0, -2000 }, { 0, 2000 }, 2.0f, Fade(GREEN, 0.5f));

            Vector2 tl = GetScreenToWorld2D({ 0, 45 }, camera2D);
            Vector2 br = GetScreenToWorld2D({ (float)sw, (float)sh - 50 }, camera2D);
            int sLX = (int)floor(tl.x / CELL_BASE_SIZE) - 1, sLY = (int)floor(tl.y / CELL_BASE_SIZE) - 1;
            int eLX = (int)floor(br.x / CELL_BASE_SIZE) + 1, eLY = (int)floor(br.y / CELL_BASE_SIZE) + 1;

            for (int ly = sLY; ly <= eLY; ly++) for (int lx = sLX; lx <= eLX; lx++) {
                int ax = lx + map.offsetX, ay = ly + map.offsetY;
                if (ax < 0 || ax >= map.width || ay < 0 || ay >= map.height) continue;
                Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                bool isSel = selGrid.data[ax][ay];
                if (sel.isMoving && isSel) DrawRectangleRec(r, Fade(map.data[ax][ay].height > 0 ? map.data[ax][ay].color : DARKGRAY, 0.25f));
                else {
                    if (map.data[ax][ay].height > 0) {
                        DrawRectangleRec(r, map.data[ax][ay].color);
                       
                        if (map.data[ax][ay].solid) DrawRectangleLinesEx(r, 2.0f / camera2D.zoom, { 255,80,80,200 });
                    }
                    else DrawRectangleLinesEx(r, 0.5f, DARKGRAY);
                }
               
                if (toolMode == TOOL_SELECT && isSel && !sel.isMoving) {
                    DrawRectangleLinesEx(r, 2.5f / camera2D.zoom, { 0, 220, 255, 255 });
                    DrawRectangleRec(r, { 0, 180, 255, 40 });
                }
                if (toolMode == TOOL_SELECT && isSel && sel.isMoving) DrawRectangleLinesEx(r, 1.5f / camera2D.zoom, Fade(SKYBLUE, 0.5f));
                if (camera2D.zoom > 0.6f) DrawText(TextFormat("%d,%d", lx, ly), (int)r.x + 2, (int)r.y + 2, 8, Fade(GRAY, 0.4f));
            }

            if (toolMode == TOOL_SELECT && sel.isMoving) {
                for (int i = 0; i < sel.count; i++) {
                    int lx = sel.srcX[i] - map.offsetX + sel.moveDeltaX, ly = sel.srcY[i] - map.offsetY + sel.moveDeltaY;
                    int nx = sel.srcX[i] + sel.moveDeltaX, ny = sel.srcY[i] + sel.moveDeltaY;
                    bool inB = (nx >= 0 && nx < map.width && ny >= 0 && ny < map.height);
                    Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                    DrawRectangleRec(r, Fade(sel.cells[i].color, inB ? 0.65f : 0.3f));
                    DrawRectangleLinesEx(r, 2.0f / camera2D.zoom, { 0,200,255,200 });
                }
            }
            
            if (toolMode == TOOL_SELECT && sel.isDragSelecting) {
                int lx0 = (int)floor(fminf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE);
                int ly0 = (int)floor(fminf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE);
                int lx1 = (int)floor(fmaxf(sel.dragStart.x, sel.dragEnd.x) / CELL_BASE_SIZE) + 1;
                int ly1 = (int)floor(fmaxf(sel.dragStart.y, sel.dragEnd.y) / CELL_BASE_SIZE) + 1;
                Rectangle sr = { lx0 * CELL_BASE_SIZE, ly0 * CELL_BASE_SIZE, (lx1 - lx0) * CELL_BASE_SIZE, (ly1 - ly0) * CELL_BASE_SIZE };
                DrawRectangleRec(sr, { 0,160,255,30 }); DrawRectangleLinesEx(sr, 1.5f / camera2D.zoom, { 0,200,255,200 });
            }

          
            if (pasteMode && clipboard.count > 0) {
                int plx = logicX, ply = logicY;
                for (int i = 0; i < clipboard.count; i++) {
                    int lx = plx + clipboard.relX[i], ly = ply + clipboard.relY[i];
                    Rectangle r = { lx * CELL_BASE_SIZE, ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                    DrawRectangleRec(r, Fade(clipboard.cells[i].height > 0 ? clipboard.cells[i].color : DARKGRAY, 0.5f));
                    DrawRectangleLinesEx(r, 2.0f / camera2D.zoom, { 255,200,0,200 });
                }
            }

        
            if ((toolMode == TOOL_LINE || toolMode == TOOL_RECT || toolMode == TOOL_CIRCLE) && shapeFirstClick) {
                for (int i = 0; i < gPreviewCount; i++) {
                    Rectangle r = { gPreview[i].lx * CELL_BASE_SIZE, gPreview[i].ly * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                    DrawRectangleRec(r, Fade(brushColor, 0.55f));
                    DrawRectangleLinesEx(r, 1.5f / camera2D.zoom, { 255,200,0,200 });
                }
               
                Rectangle sr = { shapeStartLX * CELL_BASE_SIZE, shapeStartLY * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                DrawRectangleLinesEx(sr, 3.0f / camera2D.zoom, WHITE);
            }
            
            if ((toolMode == TOOL_BRUSH || toolMode == TOOL_NONE) && !mouseOnUI) {
                Rectangle r = { logicX * CELL_BASE_SIZE, logicY * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                DrawRectangleLinesEx(r, 1.5f / camera2D.zoom, Fade(brushColor, 0.7f));
            }
            if (toolMode == TOOL_ERASER && !mouseOnUI) {
                Rectangle r = { logicX * CELL_BASE_SIZE, logicY * CELL_BASE_SIZE, CELL_BASE_SIZE - 1, CELL_BASE_SIZE - 1 };
                DrawRectangleLinesEx(r, 1.5f / camera2D.zoom, Fade(RED, 0.7f));
            }

            EndMode2D();

            
            {
                Vector2 cam3dXZ = { camera3D.position.x * CELL_BASE_SIZE + map.offsetX * CELL_BASE_SIZE,
                                    camera3D.position.z * CELL_BASE_SIZE + map.offsetY * CELL_BASE_SIZE };
                Vector2 indicatorScreen = GetWorldToScreen2D(cam3dXZ, camera2D);
                // clamp to visible area
                float ix = Clamp(indicatorScreen.x, 160, (float)sw - 10);
                float iy = Clamp(indicatorScreen.y, 50, (float)sh - 60);
                DrawCircle((int)ix, (int)iy, 7, { 255, 100, 0, 220 });
                DrawCircleLines((int)ix, (int)iy, 7, WHITE);
                DrawText("3D", (int)ix + 10, (int)iy - 6, 9, { 255,160,80,255 });
            }

          
            DrawRectangle(0, 0, sw, 45, { 35, 35, 35, 255 });
            DrawLine(0, 45, sw, 45, GRAY);
            DrawText("Demo", 20, 14, 16, LIGHTGRAY);

            Rectangle undoBtnRect = { (float)sw - 410, 10, 50, 25 };
            Rectangle redoBtnRect = { (float)sw - 355, 10, 50, 25 };
            Rectangle newBtnRect = { (float)sw - 300, 10, 50, 25 };
            Rectangle saveBtnRect = { (float)sw - 245, 10, 55, 25 };
            Rectangle loadBtnRect = { (float)sw - 185, 10, 55, 25 };
            Rectangle returnBtnRect = { (float)sw - 125, 10, 115, 25 };

            bool canUndo = (undoStack.current > 0), canRedo = (undoStack.current < undoStack.count - 1);
            GuiSetState(canUndo ? STATE_NORMAL : STATE_DISABLED);
            if (GuiButton(undoBtnRect, "#56#Undo")) { if (UndoStep(&undoStack, &map)) { FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height); ClearSelection(&selGrid, &sel); SetStatus(&status, "Undo", SKYBLUE); } }
            GuiSetState(canRedo ? STATE_NORMAL : STATE_DISABLED);
            if (GuiButton(redoBtnRect, "#57#Redo")) { if (RedoStep(&undoStack, &map)) { FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height); ClearSelection(&selGrid, &sel); SetStatus(&status, "Redo", SKYBLUE); } }
            GuiSetState(STATE_NORMAL);
            if (GuiButton(newBtnRect, "#8#New"))  showNewConfirm = true;
            if (GuiButton(saveBtnRect, "#2#Save")) { if (SaveMap(saveFile, &map)) SetStatus(&status, TextFormat("Saved: %s", saveFile), GREEN); else SetStatus(&status, TextFormat("Save FAILED: %s", saveFile), RED); }
            if (GuiButton(loadBtnRect, "#1#Load")) {
                bool ok; DynamicMap loaded = LoadMap(saveFile, &ok);
                if (ok) { FreeDynamicMap(&map); map = loaded; FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height); ClearSelection(&selGrid, &sel); ClearUndoStack(&undoStack); PushUndo(&undoStack, &map); SetStatus(&status, TextFormat("Loaded: %s", saveFile), GREEN); }
                else SetStatus(&status, TextFormat("Load FAILED: %s", saveFile), RED);
            }
            if (GuiButton(returnBtnRect, "#101#Return to 0,0")) camera2D.target = { 0, 0 };

            DrawRectangle(0, 46, 160, sh - 96, { 30, 30, 30, 200 });
            DrawLine(160, 46, 160, sh - 50, { 80, 80, 80, 200 });

            Color boxBorder = editingFilename ? Color{ 0,200,255,255 } : Color{ 80,80,80,255 };
            DrawRectangleLinesEx(fileInputRect, 1, boxBorder);
            DrawText(saveFile, (int)fileInputRect.x + 3, (int)fileInputRect.y + 4, 9, LIGHTGRAY);
            if (editingFilename && ((int)(GetTime() * 2) % 2 == 0)) {
                int cx2 = (int)fileInputRect.x + 3 + MeasureText(saveFile, 9);
                if (cx2 < (int)(fileInputRect.x + fileInputRect.width - 2)) DrawLine(cx2, (int)fileInputRect.y + 2, cx2, (int)fileInputRect.y + 15, { 0,200,255,255 });
            }

            int sby = 72;
            GuiSetState(toolMode == TOOL_BRUSH ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 8, (float)sby,      144, 26 }, "#22# Brush")) { toolMode = (toolMode == TOOL_BRUSH) ? TOOL_NONE : TOOL_BRUSH;   if (toolMode != TOOL_SELECT) ClearSelection(&selGrid, &sel); shapeFirstClick = false; }
            GuiSetState(toolMode == TOOL_ERASER ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 8, (float)sby + 30, 144, 26 }, "#143# Eraser")) { toolMode = (toolMode == TOOL_ERASER) ? TOOL_NONE : TOOL_ERASER;  if (toolMode != TOOL_SELECT) ClearSelection(&selGrid, &sel); shapeFirstClick = false; }
            GuiSetState(toolMode == TOOL_SELECT ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 8, (float)sby + 60, 144, 26 }, "#32# Select")) { toolMode = (toolMode == TOOL_SELECT) ? TOOL_NONE : TOOL_SELECT;  if (toolMode != TOOL_SELECT) { ClearSelection(&selGrid, &sel); pasteMode = false; } shapeFirstClick = false; }
            GuiSetState(toolMode == TOOL_LINE ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 8, (float)sby + 90, 144, 26 }, "Line")) { toolMode = (toolMode == TOOL_LINE) ? TOOL_NONE : TOOL_LINE;    if (toolMode != TOOL_SELECT) ClearSelection(&selGrid, &sel); shapeFirstClick = false; ClearPreview(); }
            GuiSetState(toolMode == TOOL_RECT ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 8, (float)sby + 120, 144, 26 }, "Rectangle")) { toolMode = (toolMode == TOOL_RECT) ? TOOL_NONE : TOOL_RECT;    if (toolMode != TOOL_SELECT) ClearSelection(&selGrid, &sel); shapeFirstClick = false; ClearPreview(); }
            GuiSetState(toolMode == TOOL_CIRCLE ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 8, (float)sby + 150, 144, 26 }, "Circle")) { toolMode = (toolMode == TOOL_CIRCLE) ? TOOL_NONE : TOOL_CIRCLE;  if (toolMode != TOOL_SELECT) ClearSelection(&selGrid, &sel); shapeFirstClick = false; ClearPreview(); }
            GuiSetState(STATE_NORMAL);

            int infoY = sby + 182;
            if (toolMode == TOOL_SELECT && sel.count > 0 && !pasteMode) {
                const char* info = sel.isMoving
                    ? TextFormat("Moving %d\nDx:%d Dy:%d", sel.count, sel.moveDeltaX, sel.moveDeltaY)
                    : TextFormat("%d selected\nC=copy R=rot\nCtrl<>=mirr\nDel=erase", sel.count);
                DrawText(info, 10, infoY, 9, { 0,200,255,255 }); infoY += 56;
            }
            if (pasteMode) { DrawText("PASTE MODE\nClick to place\nR=rot Esc=cancel", 10, infoY, 9, { 255,200,0,255 }); infoY += 42; }
            if ((toolMode == TOOL_LINE || toolMode == TOOL_RECT || toolMode == TOOL_CIRCLE) && shapeFirstClick)
                DrawText(TextFormat("Start: %d,%d\nClick 2nd point", shapeStartLX, shapeStartLY), 10, infoY, 9, { 255,200,0,255 });

            {
                int panelBottom = sh - 55;
                int panelTop = panelBottom - 305;
                DrawRectangle(0, panelTop, 160, 305, { 35, 35, 40, 230 });
                DrawLine(0, panelTop, 160, panelTop, { 80,80,80,200 });
                DrawText("Brush Color", 8, panelTop + 4, 9, { 160,160,200,255 });

                Rectangle cpRect = { 8, (float)(panelTop + 18), 144, 144 };
                Color newC = brushColor;
                GuiColorPicker(cpRect, NULL, &newC);
                if (newC.r != brushColor.r || newC.g != brushColor.g || newC.b != brushColor.b) {
                    brushColor = newC; brushColor.a = 255; syncBufsFromColor();
                }

                
                DrawText("Hex:", 8, panelTop + 166, 9, GRAY);
                Rectangle hexR = { 34, (float)(panelTop + 163), 84, 16 };
                bool hexHov = CheckCollisionPointRec(mousePos, hexR);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { hexEditing = hexHov; rEdit = false; gEdit = false; bEdit = false; }
                DrawRectangleLinesEx(hexR, 1, hexEditing ? Color{ 0,200,255,255 } : Color{ 80,80,80,255 });
                DrawText(hexInputBuf, (int)hexR.x + 3, (int)hexR.y + 3, 9, WHITE);
                if (hexEditing) {
                    int key; while ((key = GetCharPressed()) > 0) { int hl = (int)strlen(hexInputBuf); if (hl < 7) { hexInputBuf[hl] = (char)key; hexInputBuf[hl + 1] = '\0'; } }
                    if (IsKeyPressed(KEY_BACKSPACE)) { int hl = (int)strlen(hexInputBuf); if (hl > 0) hexInputBuf[--hl] = '\0'; }
                    Color parsed; if (TryParseHex(hexInputBuf, &parsed)) { brushColor = parsed; syncBufsFromColor(); }
                    if (IsKeyPressed(KEY_ENTER)) hexEditing = false;
                }
   
                if (GuiButton({ 122, (float)(panelTop + 162), 30, 17 }, "Rst")) { brushColor = { 200,200,200,255 }; syncBufsFromColor(); }

                DrawText("R:", 8, panelTop + 184, 9, { 255,80,80,255 });
                DrawText("G:", 8, panelTop + 200, 9, { 80,220,80,255 });
                DrawText("B:", 8, panelTop + 216, 9, { 80,150,255,255 });
                Rectangle rR = { 20, (float)(panelTop + 182), 50, 14 };
                Rectangle gR = { 20, (float)(panelTop + 198), 50, 14 };
                Rectangle bR = { 20, (float)(panelTop + 214), 50, 14 };
                auto drawRGBField = [&](Rectangle fr, char* buf, bool& editing, Color lc) {
                    bool hov = CheckCollisionPointRec(mousePos, fr);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { if (hov) { editing = true; hexEditing = false; rEdit = false; gEdit = false; bEdit = false; editing = true; } else editing = false; }
                    DrawRectangleLinesEx(fr, 1, editing ? lc : Color{ 80,80,80,255 });
                    DrawText(buf, (int)fr.x + 2, (int)fr.y + 2, 9, WHITE);
                    if (editing) {
                        int key; while ((key = GetCharPressed()) > 0) { int bl = (int)strlen(buf); if (bl < 3 && key >= '0' && key <= '9') { buf[bl] = (char)key; buf[bl + 1] = '\0'; } }
                        if (IsKeyPressed(KEY_BACKSPACE)) { int bl = (int)strlen(buf); if (bl > 0) buf[--bl] = '\0'; }
                        if (IsKeyPressed(KEY_ENTER)) editing = false;
                    }
                    };
                drawRGBField(rR, rBuf, rEdit, { 255,80,80,255 });
                drawRGBField(gR, gBuf2, gEdit, { 80,220,80,255 });
                drawRGBField(bR, bBuf, bEdit, { 80,150,255,255 });

                if (!rEdit && !gEdit && !bEdit) {
                    int rv = atoi(rBuf), gv = atoi(gBuf2), bv = atoi(bBuf);
                    rv = Clamp(rv, 0, 255); gv = Clamp(gv, 0, 255); bv = Clamp(bv, 0, 255);
                    brushColor.r = rv; brushColor.g = gv; brushColor.b = bv; brushColor.a = 255;
                }

                DrawRectangle(76, panelTop + 182, 40, 50, brushColor);
                DrawRectangleLinesEx({ 76, (float)(panelTop + 182), 40, 50 }, 1, LIGHTGRAY);

                // mirror buttons
                //DrawText("Mirror:", 8, panelTop + 240, 9, GRAY);
                //GuiSetState(mirrorMode == MIRROR_X ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 8, (float)(panelTop + 252), 44, 18 }, "X"))    mirrorMode = (mirrorMode == MIRROR_X) ? MIRROR_NONE : MIRROR_X;
                //GuiSetState(mirrorMode == MIRROR_Y ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 56, (float)(panelTop + 252), 44, 18 }, "Y"))   mirrorMode = (mirrorMode == MIRROR_Y) ? MIRROR_NONE : MIRROR_Y;
                //GuiSetState(mirrorMode == MIRROR_BOTH ? STATE_PRESSED : STATE_NORMAL); if (GuiButton({ 104, (float)(panelTop + 252), 44, 18 }, "XY")) mirrorMode = (mirrorMode == MIRROR_BOTH) ? MIRROR_NONE : MIRROR_BOTH;
                //GuiSetState(STATE_NORMAL);

                DrawText(TextFormat("Pos: %d, %d", logicX, logicY), 8, panelTop + 278, 9, { 120,140,180,255 });
            }

            DrawRectangle(0, sh - 50, sw, 50, { 35,35,35,255 });
            DrawLine(0, sh - 50, sw, sh - 50, GRAY);
            float cx = (float)sw / 2;
            if (GuiButton({ cx - 110, (float)sh - 40, 100, 30 }, "EDITOR")) currentScene = SCENE_WHITE;
            if (GuiButton({ cx + 10,  (float)sh - 40, 100, 30 }, "MAP"))    currentScene = SCENE_BLUE;

            if (status.timer > 0) {
                float alpha = Clamp(status.timer / 0.5f, 0.0f, 1.0f);
                int tw2 = MeasureText(status.text, 12), bx = sw / 2 - tw2 / 2 - 8, by = sh - 90;
                DrawRectangle(bx, by, tw2 + 16, 22, Fade({ 20,20,20,220 }, alpha));
                DrawText(status.text, bx + 8, by + 5, 12, Fade(status.color, alpha));
            }


            if (menu.active) {
                float mx2 = menu.screenPosition.x + 15, my2 = menu.screenPosition.y + 15;
                if (menu.editMode == 1) {
                    DrawRectangleRec({ mx2, my2, 165, 230 }, { 230,230,230,255 });
                    Cell& tc = map.data[menu.targetX][menu.targetY];
                    GuiColorPicker({ mx2 + 10, my2 + 10, 140, 140 }, NULL, &tc.color);

                    if (GuiButton({ mx2 + 10, my2 + 155, 80, 22 }, "Reset")) { tc.color = DARKGRAY; }
                    if (GuiButton({ mx2 + 95, my2 + 155, 60, 22 }, "Done"))  menu.editMode = 0;
                 
                    DrawText("Hex:", mx2 + 10, my2 + 182, 9, GRAY);
                    static char menuHexBuf[16] = ""; static bool menuHexEdit = false;
                    Rectangle mhR = { mx2 + 35, my2 + 180, 80, 15 };
                    bool mhHov = CheckCollisionPointRec(mousePos, mhR);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) menuHexEdit = mhHov;
                    DrawRectangleLinesEx(mhR, 1, menuHexEdit ? Color{ 0,200,255,255 } : Color{ 180,180,180,255 });
                    DrawText(menuHexBuf, (int)mhR.x + 2, (int)mhR.y + 2, 9, BLACK);
                    if (menuHexEdit) {
                        int key; while ((key = GetCharPressed()) > 0) { int hl = (int)strlen(menuHexBuf); if (hl < 7) { menuHexBuf[hl] = (char)key; menuHexBuf[hl + 1] = '\0'; } }
                        if (IsKeyPressed(KEY_BACKSPACE)) { int hl = (int)strlen(menuHexBuf); if (hl > 0) menuHexBuf[--hl] = '\0'; }
                        Color parsed; if (TryParseHex(menuHexBuf, &parsed)) tc.color = parsed;
                    }
                    sprintf(menuHexBuf, "#%02X%02X%02X", tc.color.r, tc.color.g, tc.color.b);
                }
                else {
                    float mh2 = (menu.editMode == 0) ? 185.0f : (menu.editMode == 2 ? 140.0f : menu.editMode == 4 ? 80.0f : 80.0f);
                    DrawRectangleRec({ mx2, my2, 130, mh2 }, { 45,45,45,255 });
                    DrawRectangleLinesEx({ mx2, my2, 130, mh2 }, 1, LIGHTGRAY);
                    Cell& tc = map.data[menu.targetX][menu.targetY];
                    if (menu.editMode == 0) {
                        DrawText(TextFormat("[%d,%d]", menu.targetX - map.offsetX, menu.targetY - map.offsetY), mx2 + 10, my2 + 8, 10, SKYBLUE);
                        if (GuiButton({ mx2 + 10, my2 + 25,  110, 25 }, "Color"))     menu.editMode = 1;
                        if (GuiButton({ mx2 + 10, my2 + 55,  110, 25 }, "Type"))      menu.editMode = 2;
                        if (GuiButton({ mx2 + 10, my2 + 85,  110, 25 }, "Collision")) menu.editMode = 4;
                        if (GuiButton({ mx2 + 10, my2 + 115, 110, 25 }, "Texture"))   menu.editMode = 3;
                        if (GuiButton({ mx2 + 10, my2 + 155, 110, 20 }, "Close"))     menu.active = false;
                    }
                    else if (menu.editMode == 2) {
                        DrawText("Select Type", mx2 + 10, my2 + 8, 10, GRAY);
                        if (GuiButton({ mx2 + 10, my2 + 25,  110, 25 }, "BLOCK")) { tc.type = TYPE_BLOCK;        menu.editMode = 0; }
                        if (GuiButton({ mx2 + 10, my2 + 55,  110, 25 }, "DECOR")) { tc.type = TYPE_DECOR;        menu.editMode = 0; }
                        if (GuiButton({ mx2 + 10, my2 + 85,  110, 25 }, "TEX_ONLY")) { tc.type = TYPE_TEXTURE_ONLY; menu.editMode = 0; }
                        if (GuiButton({ mx2 + 10, my2 + 115, 110, 20 }, "Back"))       menu.editMode = 0;
                    }
                    else if (menu.editMode == 3) {
                        DrawText("Textures...", mx2 + 10, my2 + 10, 10, YELLOW);
                        if (GuiButton({ mx2 + 10, my2 + 45, 110, 25 }, "Back")) menu.editMode = 0;
                    }
                    else if (menu.editMode == 4) {
                        DrawText("Collision", mx2 + 10, my2 + 8, 10, { 255,80,80,255 });
                        bool isSolid = tc.solid;
                        DrawText(isSolid ? "SOLID (on)" : "NOT SOLID", mx2 + 10, my2 + 28, 10, isSolid ? Color{ 255,80,80,255 } : LIGHTGRAY);
                        if (GuiButton({ mx2 + 10, my2 + 44, 110, 22 }, isSolid ? "Disable" : "Enable")) { tc.solid = !tc.solid; }
                        if (GuiButton({ mx2 + 10, my2 + 70, 110, 18 }, "Back")) menu.editMode = 0;
                    }
                }
            }

            if (showNewConfirm) {
                int dw = 280, dh = 100, dx = sw / 2 - dw / 2, dy = sh / 2 - dh / 2;
                DrawRectangle(dx, dy, dw, dh, { 40,40,40,245 });
                DrawRectangleLinesEx({ (float)dx, (float)dy, (float)dw, (float)dh }, 1, LIGHTGRAY);
                DrawText("New map? Unsaved changes will be lost.", dx + 14, dy + 14, 10, LIGHTGRAY);
                if (GuiButton({ (float)dx + 20, (float)dy + 55, 110, 28 }, "#8# Yes, new map")) {
                    FreeDynamicMap(&map); map = InitDynamicMap(50, 50);
                    FreeSelectionGrid(&selGrid); selGrid = InitSelectionGrid(map.width, map.height);
                    ClearSelection(&selGrid, &sel); ClearUndoStack(&undoStack); PushUndo(&undoStack, &map);
                    pasteMode = false; shapeFirstClick = false; ClearPreview();
                    camera2D.target = { 0, 0 }; showNewConfirm = false; menu.active = false;
                    SetStatus(&status, "New map created", GREEN);
                }
                if (GuiButton({ (float)dx + 150, (float)dy + 55, 110, 28 }, "Cancel")) showNewConfirm = false;
            }

            DrawFPS(sw - 80, sh - 35);
            EndDrawing();
        }

        else {
            if (IsKeyPressed(KEY_BACKSPACE)) cursorLocked = !cursorLocked;
            if (cursorLocked) { if (!IsCursorHidden()) DisableCursor(); UpdateCamera(&camera3D, CAMERA_FIRST_PERSON); }
            else { if (IsCursorHidden()) EnableCursor(); }
            camera3D.position.y = 0.8f; camera3D.target.y = 0.8f;
            if (IsKeyPressed(KEY_ESCAPE)) { currentScene = SCENE_WHITE; cursorLocked = true; }

            if (IsKeyPressed(KEY_F1)) showSkyColorPicker = !showSkyColorPicker;

            BeginDrawing();
            ClearBackground(skyColor);
            BeginMode3D(camera3D);
            Vector3 fp = { (map.width / 2.0f) - map.offsetX, 0, (map.height / 2.0f) - map.offsetY };
            DrawPlane(fp, { (float)map.width * 2, (float)map.height * 2 }, { 30,30,30,255 });
            for (int y = 0; y < map.height; y++) for (int x = 0; x < map.width; x++)
                if (map.data[x][y].height > 0) {
                    Vector3 pos = { (float)(x - map.offsetX), map.data[x][y].height / 2.0f, (float)(y - map.offsetY) };
                    DrawCube(pos, 1.0f, map.data[x][y].height, 1.0f, map.data[x][y].color);
                    if (map.data[x][y].solid) DrawCubeWires(pos, 1.0f, map.data[x][y].height, 1.0f, Fade({ 255,80,80,255 }, 0.5f));
                    else DrawCubeWires(pos, 1.0f, map.data[x][y].height, 1.0f, Fade(BLACK, 0.3f));
                }
            EndMode3D();

            DrawRectangle(0, sh - 50, sw, 50, { 35,35,35,200 });
            float cx2 = (float)sw / 2;
            if (GuiButton({ cx2 - 110, (float)sh - 40, 100, 30 }, "EDITOR")) currentScene = SCENE_WHITE;
            if (GuiButton({ cx2 + 10,  (float)sh - 40, 100, 30 }, "MAP"))    currentScene = SCENE_BLUE;

            DrawText("F1 = Sky Color | Backspace = Toggle cursor | Esc = Back", 10, sh - 42, 9, { 140,140,160,255 });

            if (showSkyColorPicker) {
                DrawRectangle(10, 10, 175, 200, { 40,40,40,230 });
                DrawRectangleLinesEx({ 10, 10, 175, 200 }, 1, LIGHTGRAY);
                DrawText("Sky Color", 18, 16, 10, LIGHTGRAY);
                GuiColorPicker({ 18, 30, 155, 155 }, NULL, &skyColor);
                if (GuiButton({ 18, 190, 80, 16 }, "Close")) showSkyColorPicker = false;
            }

            DrawFPS(sw - 80, sh - 35);
            EndDrawing();
        }

        SetTargetFPS(60);
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
