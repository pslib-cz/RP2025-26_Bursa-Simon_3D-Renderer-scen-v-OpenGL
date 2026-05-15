#include "selection.h"
#include <cstdlib>
#include <cstring>

SelectionGrid InitSelectionGrid(int w, int h) {
    SelectionGrid g;
    g.width  = w;
    g.height = h;
    g.data   = (bool**)malloc(w * sizeof(bool*));
    for (int i = 0; i < w; i++)
        g.data[i] = (bool*)calloc(h, sizeof(bool));
    return g;
}

void FreeSelectionGrid(SelectionGrid* sg) {
    for (int i = 0; i < sg->width; i++) free(sg->data[i]);
    free(sg->data);
    sg->data   = nullptr;
    sg->width  = 0;
    sg->height = 0;
}

void ExpandSelectionGrid(SelectionGrid* sg, int newW, int newH,
                         int shiftX, int shiftY)
{
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
    sg->data   = nd;
    sg->width  = newW;
    sg->height = newH;
}

void ClearSelection(SelectionGrid* sg, SelectionState* sel) {
    for (int i = 0; i < sg->width; i++)
        memset(sg->data[i], 0, sg->height * sizeof(bool));
    sel->count     = 0;
    sel->isMoving  = false;
}

void AddToCapacity(SelectionState* sel, int needed) {
    if (needed <= sel->capacity) return;
    int nc = (needed + 63) & ~63;

    Cell* nc_cells = (Cell*)realloc(sel->cells, nc * sizeof(Cell));
    int*  nc_srcX  = (int*)realloc(sel->srcX,  nc * sizeof(int));
    int*  nc_srcY  = (int*)realloc(sel->srcY,  nc * sizeof(int));

    if (nc_cells) sel->cells = nc_cells;
    if (nc_srcX)  sel->srcX  = nc_srcX;
    if (nc_srcY)  sel->srcY  = nc_srcY;
    sel->capacity = nc;
}

void RebuildSelectionList(SelectionGrid* sg, DynamicMap* map,
                          SelectionState* sel)
{
    sel->count = 0;
    for (int x = 0; x < sg->width; x++)
        for (int y = 0; y < sg->height; y++)
            if (sg->data[x][y]) sel->count++;

    AddToCapacity(sel, sel->count);

    int idx = 0;
    for (int x = 0; x < sg->width; x++) {
        for (int y = 0; y < sg->height; y++) {
            if (sg->data[x][y]) {
                sel->srcX[idx]  = x;
                sel->srcY[idx]  = y;
                sel->cells[idx] = map->data[x][y];
                idx++;
            }
        }
    }
}

void CommitMove(DynamicMap* map, SelectionGrid* sg, SelectionState* sel,
                int dx, int dy)
{
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
                map->data[x][y] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };

    for (int x = 0; x < sg->width; x++)
        memset(sg->data[x], 0, sg->height);
    for (int i = 0; i < sel->count; i++) {
        int nx = sel->srcX[i] + dx, ny = sel->srcY[i] + dy;
        if (nx >= 0 && nx < sg->width && ny >= 0 && ny < sg->height)
            sg->data[nx][ny] = true;
    }

    RebuildSelectionList(sg, map, sel);
}

void RotateSelectionCW(DynamicMap* map, SelectionGrid* sg, SelectionState* sel) {
    if (sel->count == 0) return;

    int minX = sel->srcX[0], minY = sel->srcY[0];
    int maxX = minX,         maxY = minY;
    for (int i = 1; i < sel->count; i++) {
        if (sel->srcX[i] < minX) minX = sel->srcX[i];
        if (sel->srcY[i] < minY) minY = sel->srcY[i];
        if (sel->srcX[i] > maxX) maxX = sel->srcX[i];
        if (sel->srcY[i] > maxY) maxY = sel->srcY[i];
    }
    int bH = maxY - minY + 1;

    Cell* tmp   = (Cell*)malloc(sel->count * sizeof(Cell));
    int*  nxArr = (int*)malloc(sel->count * sizeof(int));
    int*  nyArr = (int*)malloc(sel->count * sizeof(int));

    for (int i = 0; i < sel->count; i++) {
        tmp[i]   = sel->cells[i];
        nxArr[i] = minX + ((bH - 1) - (sel->srcY[i] - minY));
        nyArr[i] = minY +  (sel->srcX[i] - minX);
    }

    for (int i = 0; i < sel->count; i++)
        map->data[sel->srcX[i]][sel->srcY[i]] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };

    for (int x = 0; x < sg->width; x++)
        memset(sg->data[x], 0, sg->height * sizeof(bool));

    for (int i = 0; i < sel->count; i++) {
        int ax = nxArr[i], ay = nyArr[i];
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            map->data[ax][ay] = tmp[i];
            sg->data[ax][ay]  = true;
            sel->srcX[i]      = ax;
            sel->srcY[i]      = ay;
        }
    }
    free(tmp); free(nxArr); free(nyArr);
}



















