// Ties the pieces together at the end of every Hytale frame.
#pragma once

#include <windows.h>

#include <string>

#include "../core/settings.h"

namespace hvr::app {

void before_swap(HDC dc);
void after_swap();

Settings& settings();
void settings_changed();

// What the Sharpness setting is doing right now, for the menu.
std::string render_status();

bool menu_open();
void set_menu_open(bool open);

}  // namespace hvr::app
