#pragma once
#include "status.h"

#include <imgui.h>

struct FrogFonts {
    ImFont* label;
    ImFont* big;
};

// Draws one frog on its lily pad. `origin` is the top-centre of a cell that is
// 160x160 logical pixels at scale 1; `seed` staggers blinks between frogs.
void drawFrog(ImDrawList* dl, ImVec2 origin, float scale, ImU32 skin, AgentState state,
              float t, float seed, const FrogFonts& fonts);
