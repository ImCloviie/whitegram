#include "ayu/ui/whitegram_startup_animation.h"

#include "ayu/ui/ayu_logo.h"
#include "ayu/ayu_settings.h"
#include "ui/painter.h"
#include "ui/effects/animation_value.h"
#include <QtGui/QPainter>
#include <QtCore/QEvent>
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ui {

WhitegramStartupAnimation::WhitegramStartupAnimation(QWidget *parent)
: RpWidget(parent)
, _hideTimer([=] { 
	_hiding = true; 
	_opacityAnimation.start([=] { update(); }, 1., 0., 800, anim::sineInOut); 
	_scaleAnimation.start([=] { update(); }, 1.0, 1.5, 800, anim::easeInCubic); 
})
, _updateTimer([=] { update(); }) {
	_logo = AyuAssets::currentAppLogo();
	_startTime = crl::now();
	
	show();
	raise();
	
	_opacityAnimation.start([=] { update(); }, 1., 1., 0); // Start fully opaque to hide chats
	_scaleAnimation.start([=] { update(); }, 0.5, 1.0, 1200, anim::easeOutBack);
	
	_hideTimer.callOnce(3500);
	int fps = AyuSettings::getInstance().startupAnimationFps();
	_updateTimer.callEach(1000 / (fps > 0 ? fps : 60));
}

void WhitegramStartupAnimation::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
}

void WhitegramStartupAnimation::paintEvent(QPaintEvent *e) {
	Painter p(this);
	int animType = AyuSettings::getInstance().startupAnimationType();
	
	auto now = crl::now() - _startTime;
	
	float64 opacity = _opacityAnimation.value(_hiding ? 0. : 1.);
	float64 scale = _scaleAnimation.value(_hiding ? (animType == 1 ? 1.0 : 1.5) : 1.0);
	
	p.setOpacity(opacity);
	
	if (animType == 0) {
		float64 timePhase = now / 3000.0; 
		QPointF center(width() * (0.5 + 0.2 * std::sin(timePhase * M_PI)), 
					   height() * (0.5 + 0.2 * std::cos(timePhase * M_PI)));
		QRadialGradient bgGradient(center, width());
		bgGradient.setColorAt(0.0, QColor(35, 15, 65));
		bgGradient.setColorAt(1.0, QColor(8, 8, 15));
		p.fillRect(rect(), bgGradient);
		
		if (!_logo.isNull()) {
			float64 pulse = 0.5 + 0.5 * std::sin(now / 400.0);
			int glowSize = 350 * scale + (pulse * 30);
			QRect glowRect((width() - glowSize) / 2, (height() - glowSize) / 2, glowSize, glowSize);
			QRadialGradient glow(glowRect.center(), glowSize / 2);
			glow.setColorAt(0.0, QColor(150, 100, 255, 120));
			glow.setColorAt(0.5, QColor(150, 100, 255, 40));
			glow.setColorAt(1.0, Qt::transparent);
			p.fillRect(glowRect, glow);
		}
	} else if (animType == 1) {
		p.fillRect(rect(), QColor(10, 10, 15));
	} else if (animType == 2) {
		p.fillRect(rect(), QColor(5, 10, 15));
		p.setPen(QPen(QColor(0, 255, 200, 30), 1));
		for (int i = 0; i < width(); i += 40) p.drawLine(i, 0, i, height());
		for (int i = 0; i < height(); i += 40) p.drawLine(0, i, width(), i);
	} else if (animType == 3) {
		QLinearGradient bgGradient(0, 0, width(), height());
		bgGradient.setColorAt(0.0, QColor(255, 150, 100));
		bgGradient.setColorAt(1.0, QColor(255, 100, 150));
		p.fillRect(rect(), bgGradient);
	} else if (animType == 4) {
		float64 phase1 = now / 6000.0;
		float64 phase2 = now / 8000.0;
		
		p.fillRect(rect(), QColor(3, 0, 8));
		
		QPointF center1(width() * (0.3 + 0.2 * std::sin(phase1 * M_PI)), height() * (0.7 + 0.2 * std::cos(phase1 * M_PI)));
		QRadialGradient nebula1(center1, width() * 0.8);
		nebula1.setColorAt(0.0, QColor(50, 10, 80, 40));
		nebula1.setColorAt(1.0, Qt::transparent);
		p.fillRect(rect(), nebula1);
		
		QPointF center2(width() * (0.7 + 0.2 * std::cos(phase2 * M_PI)), height() * (0.3 + 0.2 * std::sin(phase2 * M_PI)));
		QRadialGradient nebula2(center2, width() * 0.8);
		nebula2.setColorAt(0.0, QColor(10, 30, 80, 35));
		nebula2.setColorAt(1.0, Qt::transparent);
		p.fillRect(rect(), nebula2);
		
		if (_stars.empty() && width() > 0 && height() > 0) {
			std::srand(crl::now());
			for (int i = 0; i < 150; ++i) {
				_stars.push_back(QPointF(std::rand() % std::max(width(), 1), std::rand() % std::max(height(), 1)));
				_starPhases.push_back((std::rand() % 1000) / 1000.0);
			}
		}
		
		p.setPen(Qt::NoPen);
		for (size_t i = 0; i < _stars.size(); ++i) {
			float64 twinkle = 0.5 + 0.5 * std::sin((now / 500.0) + _starPhases[i] * M_PI * 2);
			float64 driftX = std::fmod(_stars[i].x() - (now / 50.0) * (0.5 + _starPhases[i]), width());
			if (driftX < 0) driftX += width();
			QPointF pos(driftX, _stars[i].y());
			
			int r = 200 + 55 * std::sin(_starPhases[i] * M_PI);
			int g = 200 + 55 * std::cos(_starPhases[i] * M_PI);
			int b = 255;
			
			float64 size = 1.0 + 2.0 * twinkle;
			p.fillRect(QRectF(pos.x() - size/2, pos.y() - size/2, size, size), QColor(r, g, b, 50 + 200 * twinkle));
		}
		
		for (int c = 0; c < 3; ++c) {
			float64 cometPhase = std::fmod((now / 3000.0) + (c * 0.33), 1.0); 
			QPointF cometPos(width() * cometPhase, height() * (cometPhase * 0.5 + c * 0.2));
			p.fillRect(QRectF(cometPos.x() - 1, cometPos.y() - 1, 2, 2), QColor(255, 255, 255, 255));
			QLinearGradient tail(cometPos, cometPos - QPointF(60, 30));
			tail.setColorAt(0.0, QColor(255, 255, 255, 200));
			tail.setColorAt(1.0, Qt::transparent);
			p.setPen(QPen(QBrush(tail), 2));
			p.drawLine(cometPos, cometPos - QPointF(60, 30));
		}
		
		// Draw Planets
		p.setPen(Qt::NoPen);
		int minDim = std::min(width(), height());
		
		// Planet 1: Top Right with Orbital Ring
		QPointF planet1Center(width() * 0.85, height() * 0.2);
		int p1Radius = minDim * 0.08;
		QRadialGradient p1Grad(planet1Center - QPointF(p1Radius*0.3, p1Radius*0.3), p1Radius * 1.5);
		p1Grad.setColorAt(0.0, QColor(100, 70, 200));
		p1Grad.setColorAt(0.7, QColor(15, 5, 40));
		p1Grad.setColorAt(1.0, QColor(2, 0, 8));
		p.setBrush(p1Grad);
		p.drawEllipse(planet1Center, p1Radius, p1Radius);
		
		// Orbital Ring
		p.save();
		p.translate(planet1Center);
		p.rotate(-25);
		QLinearGradient ringGrad(-p1Radius*2.2, 0, p1Radius*2.2, 0);
		ringGrad.setColorAt(0.0, Qt::transparent);
		ringGrad.setColorAt(0.2, QColor(150, 100, 200, 100));
		ringGrad.setColorAt(0.5, QColor(200, 150, 200, 150));
		ringGrad.setColorAt(0.8, QColor(150, 100, 200, 100));
		ringGrad.setColorAt(1.0, Qt::transparent);
		p.setPen(QPen(QBrush(ringGrad), std::max(1, minDim / 250)));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QPointF(0, 0), p1Radius * 2.2, p1Radius * 0.3);
		p.restore();
		
		// Planet 2: Bottom Left Gas Giant
		QPointF planet2Center(width() * 0.15, height() * 0.85);
		int p2Radius = minDim * 0.18;
		QRadialGradient p2Grad(planet2Center + QPointF(p2Radius*0.4, -p2Radius*0.4), p2Radius * 1.8);
		p2Grad.setColorAt(0.0, QColor(200, 100, 50, 150));
		p2Grad.setColorAt(0.5, QColor(40, 10, 30, 100));
		p2Grad.setColorAt(1.0, QColor(2, 0, 8, 0));
		p.setPen(Qt::NoPen);
		p.setBrush(p2Grad);
		p.drawEllipse(planet2Center, p2Radius, p2Radius);

		if (!_logo.isNull()) {
			float64 pulse = 0.5 + 0.5 * std::sin(now / 600.0);
			int glowSize = 380 * scale + (pulse * 40);
			QRect glowRect((width() - glowSize) / 2, (height() - glowSize) / 2, glowSize, glowSize);
			QRadialGradient aura(glowRect.center(), glowSize / 2);
			aura.setColorAt(0.0, QColor(255, 200, 100, 80)); 
			aura.setColorAt(0.4, QColor(100, 50, 255, 40)); 
			aura.setColorAt(1.0, Qt::transparent);
			p.fillRect(glowRect, aura);
		}
	}
	
	if (!_logo.isNull()) {
		int size = 256 * scale;
		QRect targetRect((width() - size) / 2, (height() - size) / 2, size, size);
		p.drawImage(targetRect, _logo);
	}
	
	if (_hiding && !_opacityAnimation.animating()) {
		_updateTimer.cancel();
		hide();
		deleteLater();
	}
}

} // namespace Ui
