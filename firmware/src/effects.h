#pragma once

#include "config.h"

namespace effects {

// Sets up the strip from store.config and runs the start-up test.
void begin();
// Re-creates the strip after a change of pin, LED count or colour order.
void reconfigure();
// A short copper chase from the foot to the tip, so you can see whether pin, LED count and
// fold fit.
void runTest();
// Turns all LEDs off right away, e.g. before an update.
void blackout();
// Renders a frame when one is due; call from loop().
void loop();

}  // namespace effects
