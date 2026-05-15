#pragma once
#include "raylib.h"

#define MAX_TEXTURES 64

struct LoadedTexture {
    Texture2D tex;
    char path[256];
    bool valid;
};

extern LoadedTexture gTextures[MAX_TEXTURES];
extern int gTextureCount;

int LoadOrFindTexture(const char* path);
