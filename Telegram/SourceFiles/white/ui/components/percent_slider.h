// This file is part of Whitegram Desktop, based on AyuGram for Desktop.
// Copyright @Radolyn, 2026
#pragma once

#include "base/object_ptr.h"
#include "ui/rp_widget.h"

namespace WhiteUi {

struct PercentSliderArgs {
	rpl::producer<QString> title;
	int min = 0;
	int max = 100;
	int current = 0;
	Fn<void(int)> changed;
};

// A titled 1% slider: the number counts smoothly, a click on it types a value.
[[nodiscard]] object_ptr<Ui::RpWidget> CreatePercentSlider(
	QWidget *parent,
	PercentSliderArgs &&args);

} // namespace AyuUi
