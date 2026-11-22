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

// The "Profile background" page opened from the profile editing page.
void BuildProfileBackgroundSection(Builder::SectionBuilder &builder);

// The row on the profile editing page that leads to that page.
void AddProfileBackgroundEntry(Builder::SectionBuilder &builder);

} // namespace Settings
