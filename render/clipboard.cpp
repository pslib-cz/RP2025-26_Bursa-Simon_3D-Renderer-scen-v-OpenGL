#include "clipboard.h"
#include <cstdlib>
#include <cstring>

void FreeClipboard(Clipboard* cb) {
    free(cb->cells); 
    cb->cells = nullptr;

    free(cb->relX);  
    cb->relX  = nullptr;

    free(cb->relY);  
    cb->relY  = nullptr;

    cb->count = 0;
}

void CopySelection(Clipboard* cb, const SelectionState* sel, const DynamicMap*)
{
    if (!cb || !sel || sel->count <= 0) return;
    FreeClipboard(cb);

    int minX = sel->srcX[0], minY = sel->srcY[0];
    int maxX = minX,         maxY = minY;
    for (int i = 1; i < sel->count; i++) {
        if (sel->srcX[i] < minX) minX = sel->srcX[i];
        if (sel->srcY[i] < minY) minY = sel->srcY[i];
        if (sel->srcX[i] > maxX) maxX = sel->srcX[i];
        if (sel->srcY[i] > maxY) maxY = sel->srcY[i];
    }

    cb->boundsW = maxX - minX + 1;
    cb->boundsH = maxY - minY + 1;
    cb->count   = sel->count;

    cb->cells = (Cell*)malloc(cb->count * sizeof(Cell));
    cb->relX  = (int*)malloc(cb->count * sizeof(int));
    cb->relY  = (int*)malloc(cb->count * sizeof(int));

    if (!cb->cells || !cb->relX || !cb->relY) {
        free(cb->cells); free(cb->relX); free(cb->relY);
        cb->cells = nullptr; cb->relX = nullptr; cb->relY = nullptr;
        cb->count = 0;
        return;
    }

    for (int i = 0; i < sel->count; i++) {
        cb->cells[i] = sel->cells[i];
        cb->relX[i]  = sel->srcX[i] - minX;
        cb->relY[i]  = sel->srcY[i] - minY;
    }
}

void PasteClipboard(Clipboard* cb, DynamicMap* map,
                    SelectionGrid* sg, SelectionState* sel,
                    int plx, int ply)
{
    if (cb->count == 0) return;

    for (int x = 0; x < sg->width; x++)
        memset(sg->data[x], 0, sg->height * sizeof(bool));
    sel->count    = 0;
    sel->isMoving = false;

    for (int i = 0; i < cb->count; i++) {
        int lx = plx + cb->relX[i], ly = ply + cb->relY[i];
        int ax = lx + map->offsetX,  ay = ly + map->offsetY;
        if (ax >= 0 && ax < map->width && ay >= 0 && ay < map->height) {
            map->data[ax][ay]  = cb->cells[i];
            sg->data[ax][ay]   = true;
        }
    }
}

void RotateClipboardCW(Clipboard* cb) {
    if (cb->count == 0) return;
    int newW = cb->boundsH, newH = cb->boundsW;
    for (int i = 0; i < cb->count; i++) {
        int ox = cb->relX[i], oy = cb->relY[i];
        cb->relX[i] = (cb->boundsH - 1) - oy;
        cb->relY[i] = ox;
    }
    cb->boundsW = newW;
    cb->boundsH = newH;
}

void MirrorClipboardX(Clipboard* cb) {
    if (cb->count == 0) return;
    for (int i = 0; i < cb->count; i++)
        cb->relX[i] = (cb->boundsW - 1) - cb->relX[i];
}

void MirrorClipboardY(Clipboard* cb) {
    if (cb->count == 0) return;
    for (int i = 0; i < cb->count; i++)
        cb->relY[i] = (cb->boundsH - 1) - cb->relY[i];
}
