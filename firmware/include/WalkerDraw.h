#pragma once
#include "WalkerLogic.h"

// Draws (or erases, by redrawing in black) a walker pose with its feet on
// groundY. Main task only.
void drawWalker(int x, int groundY, const WalkerFrame& pose, bool erase);
