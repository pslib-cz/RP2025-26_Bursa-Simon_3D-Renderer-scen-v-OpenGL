#pragma once
#include "types.h"
#include "snapshot.h"
#include "selection.h"
#include "clipboard.h"

struct EditorState {
    ToolMode toolMode;
    MirrorMode mirrorMode;
    ContextMenu menu;
    StatusMsg status;
    SelectionGrid selGrid;
    SelectionState sel;
    Clipboard clipboard;
    UndoStack undoStack;
    Camera2D camera2D;
    Color brushColor;
    bool brushSolid;
    bool shapeFirstClick;
    int shapeStartLX;
    int shapeStartLY;
    bool pasteMode;
    int pasteLogicX;
    int pasteLogicY;
    bool showNewConfirm;
    bool isDragging;
    bool strokeStarted;
    bool editingFilename;
    char saveFile[256];
    char hexInputBuf[16];
    bool hexEditing;
    char rBuf[8];
    char gBuf2[8];
    char bBuf[8];
    bool rEdit;
    bool gEdit;
    bool bEdit;
    RenderTexture2D mapRenderTex;
    bool            mapDirty;
    Vector2         lastCamTarget;  
    float           lastCamZoom;   
    int             lastOffsetX;
    int             lastOffsetY;
};

void InitEditor(EditorState* ed, DynamicMap* map);
void FreeEditor(EditorState* ed);
GameScene UpdateDrawEditor(EditorState* ed, DynamicMap* map, Vector2 rcWorldPos, float rcAngle);