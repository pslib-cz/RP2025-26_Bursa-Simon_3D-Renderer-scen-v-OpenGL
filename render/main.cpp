#define NOGDI
#define NOUSER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"

#include "types.h"
#include "map.h"
#include "editor.h"
#include "raycaster.h"
#include <windows.h>


int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1200, 800, "Raycaster Demo");
    SetWindowMinSize(800, 600);

    GameScene currentScene = SCENE_WHITE;

    DynamicMap map = InitDynamicMap(75, 75);

    EditorState editor;
    InitEditor(&editor, &map);

    RaycasterState raycaster;
    InitRaycaster(&raycaster);

    while (!WindowShouldClose()) {
        if (currentScene == SCENE_WHITE) {
            currentScene = UpdateDrawEditor(&editor, &map,
                GetRaycasterWorldPos(&raycaster),
                GetRaycasterAngle(&raycaster));
        }
        else {
            currentScene = UpdateDrawRaycaster(&raycaster, &map);
        }
    }

    FreeEditor(&editor);
    FreeDynamicMap(&map);
    CloseWindow();
    return 0;
}
