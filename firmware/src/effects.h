#pragma once

#include "config.h"

namespace effects {

// Sets up the strip from store.config and runs the start-up test.
void begin();
// Re-creates the strip after a change of pin, LED count or colour order.
void reconfigure();
// A short copper chase over the whole strip, so you can see whether pin and LED count fit.
void runTest();
// Shows a number for a few seconds, one group of green LEDs per digit (a 0 is one blue LED),
// e.g. 143 as 1 · 4 · 3. Used for the last part of the staff's address in the hotspot.
void showNumber(uint8_t n);
// Turns all LEDs off right away, e.g. before an update.
void blackout();
// Renders a frame when one is due; call from loop().
void loop();

}  // namespace effects
