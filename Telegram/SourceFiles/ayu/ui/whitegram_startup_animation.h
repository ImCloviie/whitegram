#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

#include <QtGui/QImage>
#include <QtGui/QColor>

namespace Ui {

class WhitegramStartupAnimation : public RpWidget {
public:
	WhitegramStartupAnimation(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	[[nodiscard]] QImage tintedLogo(const QColor &tint, int strength) const;
	void drawCinematicLogo(
		QPainter &p,
		const QRectF &rect,
		const QColor &tint,
		int tintStrength,
		const QColor &bloom,
		float64 bloomStrength,
		float64 logoOpacity) const;

	QImage _logo;
	Ui::Animations::Simple _opacityAnimation;
	Ui::Animations::Simple _scaleAnimation;
	bool _hiding = false;
	crl::time _startTime = 0;
	base::Timer _hideTimer;
	base::Timer _updateTimer;
	std::vector<QPointF> _stars;
	std::vector<float> _starPhases;
};

} // namespace Ui
