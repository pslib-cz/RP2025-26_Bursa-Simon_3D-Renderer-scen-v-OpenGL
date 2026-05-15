#define _CRT_SECURE_NO_WARNINGS
#include "utils.h"
#include "raylib.h"
#include <cstring>
#include <cstdio>
#include <cstdint>



bool IsCellDefault(const Cell* c) {
    return c->height       == 0.0f
        && c->color.r      == DARKGRAY.r
        && c->color.g      == DARKGRAY.g
        && c->color.b      == DARKGRAY.b
        && c->color.a      == DARKGRAY.a
        && c->type         == TYPE_BLOCK
        && c->solid        == false
        && c->textureIndex == -1;
}

void SetStatus(StatusMsg* s, const char* txt, Color c) {
    strncpy(s->text, txt, 127);
    s->text[127] = '\0';
    s->timer     = 2.5f;
    s->color     = c;
}

bool TryParseHex(const char* s, Color* out) {
    const char* p = s;
    if (*p == '#') p++;
    if (strlen(p) != 6) return false;

    unsigned int r, g, b;
    if (sscanf(p, "%02x%02x%02x", &r, &g, &b) != 3) return false;

    out->r = (uint8_t)r;
    out->g = (uint8_t)g;
    out->b = (uint8_t)b;
    out->a = 255;
    return true;
}
