#define _CRT_SECURE_NO_WARNINGS
#include "map.h"
#include "utils.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>


DynamicMap InitDynamicMap(int w, int h) {
    DynamicMap m;
    m.width = w;
    m.height = h;
    m.offsetX = w / 2;
    m.offsetY = h / 2;
    m.data = (Cell**)malloc(w * sizeof(Cell*));

    for (int i = 0; i < w; i++) {
        m.data[i] = (Cell*)malloc(h * sizeof(Cell));
        for (int j = 0; j < h; j++) {
            m.data[i][j] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };
        }
    }
    return m;
}

void FreeDynamicMap(DynamicMap* m) {
    for (int i = 0; i < m->width; i++) {
        free(m->data[i]);
    };

    free(m->data);

    m->data = nullptr;
    m->width = 0;
    m->height = 0;
}

void ExpandMap(DynamicMap* m, int newW, int newH, int shiftX, int shiftY) {
    Cell** nd = (Cell**)malloc(newW * sizeof(Cell*));

    for (int i = 0; i < newW; i++) {
        nd[i] = (Cell*)malloc(newH * sizeof(Cell));
        for (int j = 0; j < newH; j++) {
            int ox = i - shiftX, oy = j - shiftY;
            nd[i][j] = (ox >= 0 && ox < m->width && oy >= 0 && oy < m->height)
                ? m->data[ox][oy]
                : Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };
        }
    }
    FreeDynamicMap(m);
    m->data    = nd;
    m->width   = newW;
    m->height  = newH;
    m->offsetX += shiftX;
    m->offsetY += shiftY;
}

void EnsureMapContainsLogicRect(DynamicMap* map, SelectionGrid* selectionGrid,
                                int logicLeft, int logicTop,
                                int logicRight, int logicBottom)
{
    const int EXPAND_PADDING = 5;

    int extraShiftX = 0, extraShiftY = 0;
    int newWidth  = map->width;
    int newHeight = map->height;

    int arrayLeft   = logicLeft   + map->offsetX;
    int arrayTop    = logicTop    + map->offsetY;
    int arrayRight  = logicRight  + map->offsetX;
    int arrayBottom = logicBottom + map->offsetY;

    if (arrayLeft < EXPAND_PADDING) {
        extraShiftX = EXPAND_PADDING - arrayLeft;
        newWidth   += extraShiftX;
    }
    if (arrayTop < EXPAND_PADDING) {
        extraShiftY = EXPAND_PADDING - arrayTop;
        newHeight  += extraShiftY;
    }
    if (arrayRight + EXPAND_PADDING >= newWidth)
        newWidth  = arrayRight + EXPAND_PADDING + 1 + extraShiftX;
    if (arrayBottom + EXPAND_PADDING >= newHeight)
        newHeight = arrayBottom + EXPAND_PADDING + 1 + extraShiftY;

    if (newWidth  != map->width  || newHeight != map->height ||
        extraShiftX != 0 || extraShiftY != 0)
    {
        ExpandMap(map, newWidth, newHeight, extraShiftX, extraShiftY);
        ExpandSelectionGrid(selectionGrid, newWidth, newHeight, extraShiftX, extraShiftY);
    }
}

void DrawLine2DMap(DynamicMap* map,
                   int x0, int y0, int x1, int y1,
                   Cell cell, bool erase)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        int ax = x0 + map->offsetX;
        int ay = y0 + map->offsetY;

        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            map->data[ax][ay] = erase
                ? Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 }
                : cell;
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

bool SaveMap(const char* path, const DynamicMap* m) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    int cellCount = 0;
    for (int x = 0; x < m->width; x++)
        for (int y = 0; y < m->height; y++)
            if (!IsCellDefault(&m->data[x][y])) cellCount++;

    fwrite(MAP_MAGIC,   1,           4, f);
    int ver = MAP_VERSION;
    fwrite(&ver,        sizeof(int), 1, f);
    fwrite(&m->offsetX, sizeof(int), 1, f);
    fwrite(&m->offsetY, sizeof(int), 1, f);
    fwrite(&cellCount,  sizeof(int), 1, f);

    for (int x = 0; x < m->width; x++) {
        for (int y = 0; y < m->height; y++) {
            const Cell* c = &m->data[x][y];
            if (IsCellDefault(c)) continue;
            int lx = x - m->offsetX, ly = y - m->offsetY;
            fwrite(&lx,       sizeof(int),   1, f);
            fwrite(&ly,       sizeof(int),   1, f);
            fwrite(&c->height,sizeof(float), 1, f);
            uint8_t rgba[4] = { c->color.r, c->color.g, c->color.b, c->color.a };
            fwrite(rgba,      1,             4, f);
            int t = (int)c->type; fwrite(&t, sizeof(int), 1, f);
            fwrite(&c->solid,        sizeof(bool), 1, f);
            fwrite(&c->textureIndex, sizeof(int),  1, f);
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

    int ox, oy, cellCount;
    fread(&ox,        sizeof(int), 1, f);
    fread(&oy,        sizeof(int), 1, f);
    fread(&cellCount, sizeof(int), 1, f);
    if (cellCount < 0 || cellCount > 10000000) { fclose(f); return m; }

    struct SavedCell { int lx, ly; Cell cell; };
    SavedCell* tmp = (SavedCell*)malloc(cellCount * sizeof(SavedCell));
    if (!tmp && cellCount > 0) { fclose(f); return m; }

    int minLX = 0, minLY = 0, maxLX = 0, maxLY = 0;
    bool first = true;

    for (int i = 0; i < cellCount; i++) {
        fread(&tmp[i].lx,          sizeof(int),   1, f);
        fread(&tmp[i].ly,          sizeof(int),   1, f);
        fread(&tmp[i].cell.height, sizeof(float), 1, f);
        uint8_t rgba[4]; fread(rgba, 1, 4, f);
        tmp[i].cell.color = { rgba[0], rgba[1], rgba[2], rgba[3] };
        int t; fread(&t, sizeof(int), 1, f);
        tmp[i].cell.type = (CellType)t;
        if (fread(&tmp[i].cell.solid,        sizeof(bool), 1, f) != 1) tmp[i].cell.solid        = false;
        if (fread(&tmp[i].cell.textureIndex, sizeof(int),  1, f) != 1) tmp[i].cell.textureIndex = -1;

        if (first) { minLX = maxLX = tmp[i].lx; minLY = maxLY = tmp[i].ly; first = false; }
        else {
            if (tmp[i].lx < minLX) minLX = tmp[i].lx;
            if (tmp[i].lx > maxLX) maxLX = tmp[i].lx;
            if (tmp[i].ly < minLY) minLY = tmp[i].ly;
            if (tmp[i].ly > maxLY) maxLY = tmp[i].ly;
        }
    }
    fclose(f);

    const int PAD = 25;
    int newW, newH, newOX, newOY;

    if (cellCount == 0) {
        newW = 50; newH = 50; newOX = 25; newOY = 25;
    } else {
        newOX = -minLX + PAD; newOY = -minLY + PAD;
        newW  = (maxLX - minLX + 1) + PAD * 2;
        newH  = (maxLY - minLY + 1) + PAD * 2;
        if (newW < 50) { newOX += (50 - newW) / 2; newW = 50; }
        if (newH < 50) { newOY += (50 - newH) / 2; newH = 50; }
    }

    m.width = newW; m.height = newH; m.offsetX = newOX; m.offsetY = newOY;
    m.data  = (Cell**)malloc(newW * sizeof(Cell*));
    if (!m.data) { free(tmp); return m; }

    for (int x = 0; x < newW; x++) {
        m.data[x] = (Cell*)malloc(newH * sizeof(Cell));
        for (int y = 0; y < newH; y++)
            m.data[x][y] = Cell{ 0.0f, DARKGRAY, TYPE_BLOCK, false, -1 };
    }
    for (int i = 0; i < cellCount; i++) {
        int ax = tmp[i].lx + newOX, ay = tmp[i].ly + newOY;
        if (ax >= 0 && ax < newW && ay >= 0 && ay < newH)
            m.data[ax][ay] = tmp[i].cell;
    }
    free(tmp);
    *ok = true;
    return m;
}
