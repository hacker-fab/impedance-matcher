#pragma once

#include <Arduino.h>

bool ui_init_display();
// When throttleDisplay is true (AUTO on home or metrics), redraw only on user input
// or DISPLAY_THROTTLE_AUTO_HOME_MS tick so I2C does not starve matching.
bool ui_should_redraw(bool userInput, bool throttleDisplay);
void ui_tick(int delta, bool pressed, bool doDraw);
