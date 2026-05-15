#pragma once
#include "types.h"

MapSnapshot SnapshotMap(const DynamicMap* m);
void        FreeSnapshot(MapSnapshot* s);
void        RestoreSnapshot(DynamicMap* m, const MapSnapshot* s);

struct UndoStack {
    MapSnapshot history[UNDO_MAX];
    int count   = 0;
    int current = -1;
};

void InitUndoStack(UndoStack* us);
void ClearUndoStack(UndoStack* us);
void PushUndo(UndoStack* us, const DynamicMap* m);
bool UndoStep(UndoStack* us, DynamicMap* m);
bool RedoStep(UndoStack* us, DynamicMap* m);
