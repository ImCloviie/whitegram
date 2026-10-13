// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

#include <QtGui/QColor>

#include <vector>

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Ayu::UI {

// Starfield cover for the phone number line in "my profile".
// A purely visual overlay on top of the phone label: instead of a
// classic blur it paints an animated field of drifting, twinkling
// stars on a dark backdrop, tightly hugging the number's bounds.
// Reveal triggers (hover or click) are observed via an event filter
// on the label, so the label keeps its normal mouse behaviour.
class PhoneBlur final : public ::Ui::RpWidget {
public:
	PhoneBlur(
		QWidget *parent,
		not_null<::Ui::FlatLabel*> phoneLabel);

private:
	struct Particle {
		float64 x;
		float64 y;
		float64 drift;
		float64 phase;
		float64 frequency;
		float64 size;
		QColor tint;
	};

	void paintEvent(QPaintEvent *e) override;
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void setupLabelEvents();
	void setRevealed(bool revealed, anim::type animated);
	void syncWithSettings(anim::type animated);
	void updateGeometryToLabel();
	void refreshMode();
	void ensureParticles();
	void updateAnimationState();
	[[nodiscard]] bool BlurEnabled() const;

	const not_null<::Ui::FlatLabel*> _phoneLabel;
	::Ui::Animations::Simple _revealing;
	::Ui::Animations::Basic _twinkle;
	std::vector<Particle> _particles;
	bool _revealed = false;
	bool _hoverMode = false;

};

} // namespace Ayu::UI
