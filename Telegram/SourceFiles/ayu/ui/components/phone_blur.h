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

#include <QtGui/QImage>

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Ayu::UI {

// Blur cover for the phone number line in "my profile".
// A purely visual overlay on top of the phone label: it renders
// a blurred snapshot of the number and fades out when revealed.
// Reveal triggers (hover or click) are observed via an event filter
// on the label, so the label keeps its normal mouse behaviour.
class PhoneBlur final : public Ui::RpWidget {
public:
	PhoneBlur(
		QWidget *parent,
		not_null<Ui::FlatLabel*> phoneLabel);

private:
	void paintEvent(QPaintEvent *e) override;
	void setupLabelEvents();
	void setRevealed(bool revealed, anim::type animated);
	void syncWithSettings(anim::type animated);
	void updateGeometryToLabel();
	void refreshMode();
	[[nodiscard]] bool BlurEnabled() const;
	[[nodiscard]] QImage GrabLabelImage();

	const not_null<Ui::FlatLabel*> _phoneLabel;
	Ui::Animations::Simple _revealing;
	QImage _blurred;
	bool _revealed = false;
	bool _hoverMode = false;

};

} // namespace Ayu::UI
