#pragma once
#include "raylib.h"
#include "editor.h"
#include "map.h"

void HandleZoom(EditorState* ed, Vector2 mousePos, float wheel);
void HandleUndoRedo(EditorState* ed, DynamicMap* map);
void HandleCopyPaste(EditorState* ed, DynamicMap* map, int logicX, int logicY);
void HandleMirror(EditorState* ed, DynamicMap* map);
void HandleRotate(EditorState* ed, DynamicMap* map);
void HandleToolKeys(EditorState* ed);
void HandleSelectTool(EditorState* ed, DynamicMap* map, Vector2 worldMouse, int logicX, int logicY);
void HandleShapeTools(EditorState* ed, DynamicMap* map, int logicX, int logicY);
void HandleCanvasTools(EditorState* ed, DynamicMap* map, bool mouseOnUI, int logicX, int logicY);
void HandleContextMenu(EditorState* ed, DynamicMap* map, Vector2 worldMouse, int logicX, int logicY);
void HandleMapExpansion(EditorState* ed, DynamicMap* map, int sw, int sh, float wheel);
void HandleFilenameInput(EditorState* ed, Vector2 mousePos);

