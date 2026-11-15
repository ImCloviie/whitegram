// This file is part of Whitegram Desktop, based on AyuGram for Desktop.
// Copyright @Radolyn, 2026
#pragma once

namespace AyuFeatures::GlassEffect {

// Background opacity bounds in percent; text and icons stay opaque.
inline constexpr auto kMinOpacity = 5;
inline constexpr auto kMaxOpacity = 95;

// Read once per main window: only windows created translucent can show it.
[[nodiscard]] bool CreateTranslucent();

// Opacity for the chat wallpaper: 1 while the effect is off.
[[nodiscard]] double BackgroundOpacity();

// Applies the saved state; called once after settings are loaded.
void init();

// Applies current settings; `toggle` plays the on / off crossfade.
void sync(bool toggle);

} // namespace AyuFeatures::GlassEffect
