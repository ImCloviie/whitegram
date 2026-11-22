// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"

namespace Settings {

[[nodiscard]] bool IsWhitegramRussian();

[[nodiscard]] QString ExperimentalOptionName(
	const QString &id,
	const QString &fallback);
[[nodiscard]] QString ExperimentalOptionDescription(
	const QString &id,
	const QString &fallback);
[[nodiscard]] QString ExperimentalCategoryTitle(const QString &title);

} // namespace Settings
