#pragma once
#include "types.h"

void FreeClipboard(Clipboard* cb);
void CopySelection(Clipboard* cb, const SelectionState* sel, const DynamicMap* map);
void PasteClipboard(Clipboard* cb, DynamicMap* map, SelectionGrid* sg, SelectionState* sel, int plx, int ply);
void RotateClipboardCW(Clipboard* cb);
void MirrorClipboardX(Clipboard* cb);
void MirrorClipboardY(Clipboard* cb);
