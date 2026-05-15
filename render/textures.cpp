#define _CRT_SECURE_NO_WARNINGS
#include "textures.h"
#include <cstring>


LoadedTexture gTextures[MAX_TEXTURES] = {};
int           gTextureCount           = 0;

int LoadOrFindTexture(const char* path) {
    for (int i = 0; i < gTextureCount; i++)
        if (gTextures[i].valid && strcmp(gTextures[i].path, path) == 0)
            return i;

    if (gTextureCount >= MAX_TEXTURES) return -1;

    Texture2D t = LoadTexture(path);
    if (t.id == 0) return -1;

    gTextures[gTextureCount] = { t, {}, true };
    strncpy(gTextures[gTextureCount].path, path, 255);
    return gTextureCount++;
}
