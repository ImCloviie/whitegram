// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "rpl/producer.h"

namespace AyuFeatures::ConnectionBypass {

void init();
void reevaluate(bool forceRestart = false, bool userInitiated = false);
void shutdown();

[[nodiscard]] bool modeSwitchLocked();
[[nodiscard]] rpl::producer<bool> modeSwitchLockedValue();

} // namespace AyuFeatures::ConnectionBypass
