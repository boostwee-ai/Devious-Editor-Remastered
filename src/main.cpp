#include <Geode/Geode.hpp>
#include "network/NetworkTypes.hpp"
using namespace geode::prelude;

// All hooks are registered via $modify in their respective translation units.
// This file just provides the required Geode mod entry point.

$on_mod(Loaded) {
    log::info("Devious Editor Remastered loaded — LAN collaborative editing enabled.");
    log::info("Platform: {}", PLATFORM_TAG);
}
