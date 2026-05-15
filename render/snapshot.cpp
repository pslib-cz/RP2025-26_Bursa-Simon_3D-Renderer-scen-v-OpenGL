#include "snapshot.h"
#include <cstdlib>
#include <cstring>

MapSnapshot SnapshotMap(const DynamicMap* m) {
    MapSnapshot s = {};
    if (!m || !m->data) return s;

    s.width   = m->width;
    s.height  = m->height;
    s.offsetX = m->offsetX;
    s.offsetY = m->offsetY;
    s.data    = (Cell**)malloc(m->width * sizeof(Cell*));
    if (!s.data) return s;

    for (int x = 0; x < m->width; x++) {
        s.data[x] = (Cell*)malloc(m->height * sizeof(Cell));
        if (s.data[x])
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

    m->width   = s->width;
    m->height  = s->height;
    m->offsetX = s->offsetX;
    m->offsetY = s->offsetY;
    m->data    = (Cell**)malloc(m->width * sizeof(Cell*));
    if (!m->data) return;

    for (int x = 0; x < m->width; x++) {
        m->data[x] = (Cell*)malloc(m->height * sizeof(Cell));
        if (m->data[x])
            memcpy(m->data[x], s->data[x], m->height * sizeof(Cell));
    }
}

void InitUndoStack(UndoStack* us) {
    memset(us->history, 0, sizeof(us->history));
    us->count   = 0;
    us->current = -1;
}

void ClearUndoStack(UndoStack* us) {
    for (int i = 0; i < us->count; i++) FreeSnapshot(&us->history[i]);
    us->count   = 0;
    us->current = -1;
}

void PushUndo(UndoStack* us, const DynamicMap* m) {
    for (int i = us->current + 1; i < us->count; i++)
        FreeSnapshot(&us->history[i]);
    us->count = us->current + 1;

    if (us->count >= UNDO_MAX) {
        FreeSnapshot(&us->history[0]);
        memmove(&us->history[0], &us->history[1],
                (UNDO_MAX - 1) * sizeof(MapSnapshot));
        us->count--;
        us->current--;
    }

    us->history[us->count] = SnapshotMap(m);
    us->current = us->count;
    us->count++;
}

bool UndoStep(UndoStack* us, DynamicMap* m) {
    if (us->current <= 0) return false;
    us->current--;
    RestoreSnapshot(m, &us->history[us->current]);
    return true;
}

bool RedoStep(UndoStack* us, DynamicMap* m) {
    if (us->current >= us->count - 1) return false;
    us->current++;
    RestoreSnapshot(m, &us->history[us->current]);
    return true;
}
