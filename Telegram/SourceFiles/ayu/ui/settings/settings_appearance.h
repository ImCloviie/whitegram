// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Settings {

// Adds the former AyuGram "Appearance" settings to a Whitegram page.
void BuildAyuAppearanceContent(Builder::SectionBuilder &builder);

} // namespace Settings
