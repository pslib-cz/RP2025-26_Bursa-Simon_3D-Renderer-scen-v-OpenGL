#pragma once
#include "raylib.h"
#include <cstdint>

#define CELL_BASE_SIZE 40.0f
#define MAX_UNZOOM     0.1f
#define MAX_ZOOM       3.0f
#define UNDO_MAX       128
#define MAP_MAGIC      "MAP\0"
#define MAP_VERSION    1

typedef enum { SCENE_LAUNCH = 0, SCENE_WHITE, SCENE_BLUE } GameScene;
typedef enum { TYPE_BLOCK = 0, TYPE_DECOR, TYPE_TEXTURE_ONLY } CellType;
typedef enum { TOOL_NONE = 0, TOOL_BRUSH, TOOL_ERASER, TOOL_SELECT, TOOL_LINE, TOOL_RECT, TOOL_CIRCLE } ToolMode;
typedef enum { MIRROR_NONE = 0, MIRROR_X, MIRROR_Y, MIRROR_BOTH }MirrorMode;

typedef struct {
    float    height;
    Color    color;
    CellType type;
    bool     solid;
    int      textureIndex;
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
    Cell*   cells;
    int*    srcX;
    int*    srcY;
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
    int*  relX, *relY;
    int   count, boundsW, boundsH;
} Clipboard;

typedef struct {
    Cell** data;
    int    width, height, offsetX, offsetY;
} MapSnapshot;

typedef struct {
    char  text[128];
    float timer;
    Color color;
} StatusMsg;
