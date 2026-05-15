#pragma once
#include "types.h"
#include "selection.h"

DynamicMap InitDynamicMap(int w, int h);
void FreeDynamicMap(DynamicMap* m);
void ExpandMap(DynamicMap* m, int newW, int newH, int shiftX, int shiftY);
void EnsureMapContainsLogicRect(DynamicMap* map, SelectionGrid* sg, int logicLeft, int logicTop, int logicRight, int logicBottom);
void DrawLine2DMap(DynamicMap* map, int x0, int y0, int x1, int y1, Cell cell, bool erase);

bool SaveMap(const char* path, const DynamicMap* m);
DynamicMap LoadMap(const char* path, bool* ok);
