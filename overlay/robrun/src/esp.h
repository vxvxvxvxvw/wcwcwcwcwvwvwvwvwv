#pragma once
#include "shared.h"
#include "imgui.h"

// Toggles ESP — modifiés par main.cpp via le menu ImGui
extern bool g_esp_boxes;
extern bool g_esp_snapline;
extern bool g_esp_names;
extern bool g_esp_dist;
extern bool g_esp_role;
extern bool g_esp_armor;
extern bool g_esp_state;

void draw_esp(ImDrawList* dl, const RobRunSnapshot& snap, float sw, float sh);
