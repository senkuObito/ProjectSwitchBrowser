// imgui_impl_nx.h
#pragma once
#include "imgui.h"

extern "C" {
bool imgui_backend_init();
void imgui_backend_exit();
void imgui_backend_new_frame();
void imgui_backend_render_draw_data(ImDrawData *draw_data);
}
