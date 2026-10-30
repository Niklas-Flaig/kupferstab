#pragma once

#include <Arduino.h>

// Updates from the GitHub releases of Niklas-Flaig/kupferstab, see docs/protocol.md.
// The staff checks after joining the hotspot, every 30 minutes and on request; it installs
// only on request. Everything it takes is signed with the release key (signing_key.h).
namespace selfupdate {

void loop();
void requestCheck();
void requestInstall();
// The newer version on GitHub, or empty when there is none (or nothing checked yet).
const String& available();

}  // namespace selfupdate
