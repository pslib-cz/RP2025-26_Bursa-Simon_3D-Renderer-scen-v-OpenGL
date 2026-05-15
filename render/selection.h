#pragma once
#include "types.h"

SelectionGrid InitSelectionGrid(int w, int h);
void FreeSelectionGrid(SelectionGrid* sg);
void ExpandSelectionGrid(SelectionGrid* sg, int newW, int newH, int shiftX, int shiftY);

void ClearSelection(SelectionGrid* sg, SelectionState* sel);
void AddToCapacity(SelectionState* sel, int needed);
void RebuildSelectionList(SelectionGrid* sg, DynamicMap* map, SelectionState* sel);
void CommitMove(DynamicMap* map, SelectionGrid* sg, SelectionState* sel, int dx, int dy);
void RotateSelectionCW(DynamicMap* map, SelectionGrid* sg, SelectionState* sel);
