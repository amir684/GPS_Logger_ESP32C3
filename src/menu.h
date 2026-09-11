#pragma once

#include "input.h"

class U8G2;

// Hierarchical settings menu generated from the settings table.
// Up/down move or change a value, push selects/confirms, hold goes back or cancels.
namespace Menu {
void open();
void close();
bool isOpen();
void handleKey(Key key);
void draw(U8G2 &lcd);  // draws the whole screen
}  // namespace Menu
