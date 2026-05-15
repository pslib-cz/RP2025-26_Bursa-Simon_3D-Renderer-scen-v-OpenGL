#include "preview.h"
#include <cmath>
#include <cstdlib>

PreviewCell gPreview[MAX_PREVIEW];
int gPreviewCount = 0;

void ClearPreview()            { gPreviewCount = 0; }
void AddPreview(int lx, int ly) {
    if (gPreviewCount < MAX_PREVIEW) {
        gPreview[gPreviewCount].lx = lx;
        gPreview[gPreviewCount].ly = ly;
        gPreviewCount++;
    }
}

void BuildLinePreview(int x0, int y0, int x1, int y1) {
    ClearPreview();
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        AddPreview(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void BuildRectPreview(int x0, int y0, int x1, int y1) {
    ClearPreview();
    int lx0 = (x0 < x1) ? x0 : x1, lx1 = (x0 < x1) ? x1 : x0;
    int ly0 = (y0 < y1) ? y0 : y1, ly1 = (y0 < y1) ? y1 : y0;

    for (int x = lx0; x <= lx1; x++) {
        AddPreview(x, ly0);
        if (ly1 != ly0) AddPreview(x, ly1);
    }
    for (int y = ly0 + 1; y < ly1; y++) {
        AddPreview(lx0, y);
        if (lx1 != lx0) AddPreview(lx1, y);
    }
}

void BuildCirclePreview(int centerX, int centerY, int radiusX, int radiusY) {
    ClearPreview();

    long long a2 = (long long)radiusX * radiusX;
    long long b2 = (long long)radiusY * radiusY;

    int x = 0, y = radiusY;

    auto plot = [&](int ox, int oy) {
        AddPreview(centerX + ox, centerY + oy);
        AddPreview(centerX - ox, centerY + oy);
        AddPreview(centerX + ox, centerY - oy);
        AddPreview(centerX - ox, centerY - oy);
    };

    double d1 = b2 - (a2 * radiusY) + (0.25 * a2);
    long long dx = 2 * b2 * x, dy = 2 * a2 * y;

    while (dx < dy) {
        plot(x, y);
        if (d1 < 0) { x++; dx += 2*b2; d1 += dx + b2; }
        else        { x++; y--; dx += 2*b2; dy -= 2*a2; d1 += dx - dy + b2; }
    }

    double d2 = b2 * (x + 0.5) * (x + 0.5) +
                a2 * (y - 1)   * (y - 1) -
                (double)(a2 * b2);

    while (y >= 0) {
        plot(x, y);
        if (d2 > 0) { y--;     dy -= 2*a2; d2 += a2 - dy; }
        else        { y--; x++; dx += 2*b2; dy -= 2*a2; d2 += dx - dy + a2; }
    }
}

void CommitPreview(DynamicMap* map, Cell cell) {
    for (int i = 0; i < gPreviewCount; i++) {
        int ax = gPreview[i].lx + map->offsetX;
        int ay = gPreview[i].ly + map->offsetY;
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height)
            map->data[ax][ay] = cell;
    }
}
