#pragma once
#include "types.h"

#define MAX_PREVIEW 4096

struct PreviewCell { int lx, ly; };

extern PreviewCell gPreview[MAX_PREVIEW];
extern int         gPreviewCount;

void ClearPreview();
void AddPreview(int lx, int ly);

void BuildLinePreview  (int x0, int y0, int x1, int y1);
void BuildRectPreview  (int x0, int y0, int x1, int y1);
void BuildCirclePreview(int centerX, int centerY, int radiusX, int radiusY);

void CommitPreview(DynamicMap* map, Cell cell);
