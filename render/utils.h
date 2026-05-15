#pragma once
#include "types.h"

bool IsCellDefault(const Cell* c);
void SetStatus(StatusMsg* s, const char* txt, Color c);
bool TryParseHex(const char* s, Color* out);
