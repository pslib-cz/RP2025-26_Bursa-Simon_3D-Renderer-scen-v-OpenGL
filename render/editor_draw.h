#pragma once

#include "raylib.h"
#include "editor.h"
#include "map.h"

void DrawMapCells(EditorState* ed, DynamicMap* map, int sw, int sh);
void DrawMovingSelection(EditorState* ed, DynamicMap* map);
void DrawDragSelect(EditorState* ed);
void DrawPastePreview(EditorState* ed, int logicX, int logicY);
void DrawShapePreview(EditorState* ed);
void DrawCursorHighlight(EditorState* ed, bool mouseOnUI, int logicX, int logicY);
void DrawPlayerMarker(EditorState* ed, Vector2 rcWorldPos, float rcAngle, int sw, int sh);
GameScene DrawTopBar(EditorState* ed, DynamicMap* map, int sw, int sh);
void DrawSidebar(EditorState* ed, DynamicMap* map, Vector2 mousePos, int sh, int logicX, int logicY);
void DrawColorPanel(EditorState* ed, Vector2 mousePos, int sw, int sh, int logicX, int logicY);
GameScene DrawBottomBar(EditorState* ed, int sw, int sh);
void DrawContextMenu(EditorState* ed, DynamicMap* map, Vector2 mousePos);
void DrawNewMapConfirm(EditorState* ed, DynamicMap* map, int sw, int sh);
void InitMapRenderTexture(EditorState* ed, int sw, int sh);
void MarkMapDirty(EditorState* ed);
void DrawMapCellAt(EditorState* ed, DynamicMap* map, int logicX, int logicY);