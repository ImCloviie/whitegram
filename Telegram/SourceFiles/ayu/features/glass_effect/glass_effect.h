// This file is part of Whitegram Desktop, based on AyuGram for Desktop.
// Copyright @Radolyn, 2026
#pragma once

class QPainter;
class QRect;
class QWidget;

namespace AyuFeatures::GlassEffect {

// Background opacity bounds in percent; text and icons stay opaque.
inline constexpr auto kMinOpacity = 5;
inline constexpr auto kMaxOpacity = 95;

// Read once per main window: only windows created translucent can show it.
[[nodiscard]] bool CreateTranslucent();

// Opacity for the chat wallpaper: 1 while the effect is off.
[[nodiscard]] double BackgroundOpacity();

// Paints the blurred window content under a layer (side menu, settings).
void PaintLayerBackdrop(QPainter &p, QWidget *layer, int radius);

// A widget on the window glass that uses the emoji panel colors, like the
// sticker column: while it is visible those colors follow the glass too.
void RegisterSidePanel(QWidget *panel);

// Same glass for a popup over the chat (emoji panel), inside `area`.
void PaintPopupBackdrop(QPainter &p, QWidget *popup, QRect area, int radius);

// Applies the saved state; called once after settings are loaded.
void init();

// Applies current settings; `toggle` plays the on / off crossfade.
void sync(bool toggle);

} // namespace AyuFeatures::GlassEffect
