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

// Former AyuGram "ghost" page, now shown on Whitegram pages.
void BuildAyuGhostEssentials(Builder::SectionBuilder &builder);
void BuildAyuSpyContent(Builder::SectionBuilder &builder);
void BuildAyuOtherToggles(Builder::SectionBuilder &builder);

} // namespace Settings
