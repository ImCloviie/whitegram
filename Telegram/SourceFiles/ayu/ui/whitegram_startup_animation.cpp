#include "ayu/ui/whitegram_startup_animation.h"

#include "ayu/ui/ayu_logo.h"
#include "ayu/ayu_settings.h"
#include "ui/painter.h"
#include "ui/effects/animation_value.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ui {
namespace {

constexpr auto kSplashMs = crl::time(5200);
constexpr auto kFadeOutMs = crl::time(900);

[[nodiscard]] float64 Clamp01(float64 v) {
	return std::max(0., std::min(1., v));
}

[[nodiscard]] float64 Smoothstep(float64 edge0, float64 edge1, float64 x) {
	const auto t = Clamp01((x - edge0) / (edge1 - edge0));
	return t * t * (3. - 2. * t);
}

[[nodiscard]] float64 EaseOutCubic(float64 t) {
	t = Clamp01(t);
	return 1. - std::pow(1. - t, 3.);
}

[[nodiscard]] float64 EaseOutBack(float64 t) {
	t = Clamp01(t);
	constexpr auto c1 = 1.70158;
	constexpr auto c3 = c1 + 1.;
	return 1. + c3 * std::pow(t - 1., 3.) + c1 * std::pow(t - 1., 2.);
}

[[nodiscard]] float64 Lerp(float64 a, float64 b, float64 t) {
	return a + (b - a) * t;
}

} // namespace

WhitegramStartupAnimation::WhitegramStartupAnimation(QWidget *parent)
: RpWidget(parent)
, _hideTimer([=] {
	_hiding = true;
	_opacityAnimation.start(
		[=] { update(); },
		1.,
		0.,
		kFadeOutMs,
		anim::sineInOut);
	_scaleAnimation.start(
		[=] { update(); },
		1.0,
		1.12,
		kFadeOutMs,
		anim::easeInCubic);
})
, _updateTimer([=] { update(); }) {
	_logo = AyuAssets::currentAppLogo();
	_startTime = crl::now();

	show();
	raise();

	_opacityAnimation.start([=] { update(); }, 1., 1., 0);
	_scaleAnimation.start([=] { update(); }, 1.0, 1.0, 0);

	_hideTimer.callOnce(kSplashMs);
	const auto fps = AyuSettings::getInstance().startupAnimationFps();
	_updateTimer.callEach(1000 / (fps > 0 ? fps : 60));
}

void WhitegramStartupAnimation::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	_stars.clear();
	_starPhases.clear();
}

QImage WhitegramStartupAnimation::tintedLogo(
		const QColor &tint,
		int strength) const {
	if (_logo.isNull() || strength <= 0) {
		return _logo;
	}
	auto image = _logo.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	QPainter lp(&image);
	lp.setCompositionMode(QPainter::CompositionMode_SourceAtop);
	auto color = tint;
	color.setAlpha(std::clamp(strength, 0, 255));
	lp.fillRect(image.rect(), color);
	return image;
}

void WhitegramStartupAnimation::drawCinematicLogo(
		QPainter &p,
		const QRectF &rect,
		const QColor &tint,
		int tintStrength,
		const QColor &bloom,
		float64 bloomStrength,
		float64 logoOpacity) const {
	if (_logo.isNull() || logoOpacity <= 0.001 || rect.isEmpty()) {
		return;
	}
	PainterHighQualityEnabler hq(p);
	const auto image = tintedLogo(tint, tintStrength);
	const auto center = rect.center();

	for (auto layer = 4; layer >= 1; --layer) {
		const auto grow = 1.0 + layer * 0.085 * bloomStrength;
		const auto layerRect = QRectF(
			center.x() - rect.width() * grow * 0.5,
			center.y() - rect.height() * grow * 0.5,
			rect.width() * grow,
			rect.height() * grow);
		auto glow = bloom;
		glow.setAlpha(int(logoOpacity * bloomStrength * (28. / layer)));
		p.setPen(Qt::NoPen);
		QRadialGradient grad(center, layerRect.width() * 0.55);
		grad.setColorAt(0.0, glow);
		grad.setColorAt(0.55, QColor(glow.red(), glow.green(), glow.blue(), glow.alpha() / 3));
		grad.setColorAt(1.0, Qt::transparent);
		p.setBrush(grad);
		p.drawEllipse(layerRect);
	}

	p.setOpacity(logoOpacity);
	p.drawImage(rect, image);
	p.setOpacity(1.);
}

void WhitegramStartupAnimation::paintEvent(QPaintEvent *e) {
	Painter p(this);
	const auto &settings = AyuSettings::getInstance();
	const auto animType = settings.startupAnimationType();
	const auto showIcon = settings.startupAnimationShowIcon();
	const auto now = crl::now() - _startTime;
	const auto t = now / 1000.0;
	const auto w = double(width());
	const auto h = double(height());
	const auto minDim = std::min(w, h);

	auto screenOpacity = _opacityAnimation.value(_hiding ? 0. : 1.);
	auto hideScale = _scaleAnimation.value(_hiding ? 1.12 : 1.0);
	p.setOpacity(screenOpacity);

	auto logoOpacity = 0.;
	auto logoScale = 1.;
	auto logoCenter = QPointF(w * 0.5, h * 0.5);
	auto logoTint = QColor(255, 255, 255);
	auto tintStrength = 0;
	auto bloom = QColor(255, 255, 255);
	auto bloomStrength = 0.8;

	if (animType == 0) {
		PainterHighQualityEnabler hq(p);
		const auto drift = 0.12 * std::sin(t * 0.35);
		QPointF focus(w * (0.5 + drift), h * (0.48 - drift * 0.3));
		QRadialGradient bg(focus, minDim * 1.1);
		bg.setColorAt(0.0, QColor(55, 25, 95));
		bg.setColorAt(0.45, QColor(22, 12, 48));
		bg.setColorAt(1.0, QColor(6, 6, 14));
		p.fillRect(rect(), bg);

		QRadialGradient veil(QPointF(w * 0.5, h * 0.2), minDim);
		veil.setColorAt(0.0, QColor(120, 70, 220, 50));
		veil.setColorAt(1.0, Qt::transparent);
		p.fillRect(rect(), veil);

		if (_stars.empty() && width() > 0) {
			std::srand(unsigned(crl::now()));
			for (auto i = 0; i < 90; ++i) {
				_stars.push_back(QPointF(
					std::rand() % std::max(width(), 1),
					std::rand() % std::max(height(), 1)));
				_starPhases.push_back((std::rand() % 1000) / 1000.0f);
			}
		}
		p.setPen(Qt::NoPen);
		for (size_t i = 0; i < _stars.size(); ++i) {
			const auto twinkle = 0.4 + 0.6 * std::sin(
				t * 2.0 + _starPhases[i] * M_PI * 2);
			const auto size = 1.0 + 1.8 * twinkle;
			p.fillRect(
				QRectF(
					_stars[i].x() - size / 2,
					_stars[i].y() - size / 2,
					size,
					size),
				QColor(210, 190, 255, int(50 + 160 * twinkle)));
		}

		const auto reveal = EaseOutBack(Smoothstep(0.35, 2.1, t));
		logoOpacity = Smoothstep(0.25, 1.4, t);
		logoScale = Lerp(0.35, 1.0, reveal) * hideScale;
		logoCenter = QPointF(w * 0.5, h * (0.54 - 0.04 * EaseOutCubic(reveal)));
		logoTint = QColor(190, 150, 255);
		tintStrength = int(90 * (1.0 - 0.45 * Smoothstep(1.2, 2.8, t)));
		bloom = QColor(150, 100, 255);
		bloomStrength = 1.15 + 0.2 * std::sin(t * 2.2);

	} else if (animType == 1) {
		p.fillRect(rect(), QColor(8, 8, 12));
		QRadialGradient soft(QPointF(w * 0.5, h * 0.5), minDim * 0.55);
		soft.setColorAt(0.0, QColor(30, 30, 40, 90));
		soft.setColorAt(1.0, Qt::transparent);
		p.fillRect(rect(), soft);

		const auto reveal = EaseOutCubic(Smoothstep(0.2, 1.8, t));
		logoOpacity = reveal;
		logoScale = Lerp(0.7, 1.0, reveal) * hideScale;
		logoCenter = QPointF(w * 0.5, h * 0.5);
		logoTint = QColor(230, 235, 255);
		tintStrength = 40;
		bloom = QColor(220, 230, 255);
		bloomStrength = 0.7;

	} else if (animType == 2) {
		p.fillRect(rect(), QColor(4, 10, 16));
		p.setPen(QPen(QColor(0, 255, 210, 28), 1));
		const auto shift = std::fmod(t * 18.0, 40.0);
		for (auto i = -40; i < width() + 40; i += 40) {
			p.drawLine(int(i + shift), 0, int(i + shift), height());
		}
		for (auto i = -40; i < height() + 40; i += 40) {
			p.drawLine(0, int(i - shift * 0.5), width(), int(i - shift * 0.5));
		}
		QRadialGradient pulse(QPointF(w * 0.5, h * 0.5), minDim * 0.7);
		pulse.setColorAt(0.0, QColor(0, 255, 200, int(25 + 20 * std::sin(t * 3))));
		pulse.setColorAt(1.0, Qt::transparent);
		p.fillRect(rect(), pulse);

		const auto reveal = EaseOutBack(Smoothstep(0.3, 2.0, t));
		logoOpacity = Smoothstep(0.2, 1.3, t);
		logoScale = Lerp(0.4, 1.0, reveal) * hideScale;
		logoCenter = QPointF(w * 0.5, h * 0.5);
		logoTint = QColor(0, 255, 210);
		tintStrength = int(110 * (1.0 - 0.5 * Smoothstep(1.4, 3.0, t)));
		bloom = QColor(255, 40, 180);
		bloomStrength = 1.0 + 0.25 * std::sin(t * 4.0);

	} else if (animType == 3) {
		PainterHighQualityEnabler hq(p);
		const auto sunBob = 0.012 * std::sin(t * 0.55);
		const auto sunX = w * 0.5;
		const auto sunY = h * (0.58 + sunBob);
		const auto sunR = minDim * 0.145;

		QLinearGradient sky(0, 0, 0, h);
		sky.setColorAt(0.0, QColor(10, 8, 38));
		sky.setColorAt(0.25, QColor(68, 24, 96));
		sky.setColorAt(0.48, QColor(190, 55, 95));
		sky.setColorAt(0.68, QColor(255, 125, 65));
		sky.setColorAt(0.85, QColor(255, 185, 100));
		sky.setColorAt(1.0, QColor(255, 135, 85));
		p.fillRect(rect(), sky);

		if (_stars.empty() && width() > 0) {
			std::srand(unsigned(crl::now()));
			for (auto i = 0; i < 80; ++i) {
				_stars.push_back(QPointF(
					std::rand() % std::max(width(), 1),
					std::rand() % std::max(int(h * 0.42), 1)));
				_starPhases.push_back((std::rand() % 1000) / 1000.0f);
			}
		}
		p.setPen(Qt::NoPen);
		for (size_t i = 0; i < _stars.size(); ++i) {
			const auto twinkle = 0.35 + 0.65 * std::sin(
				t * 1.6 + _starPhases[i] * M_PI * 2);
			const auto fade = 1.0 - (_stars[i].y() / (h * 0.45));
			const auto size = 1.0 + 1.7 * twinkle;
			p.fillRect(
				QRectF(
					_stars[i].x() - size / 2,
					_stars[i].y() - size / 2,
					size,
					size),
				QColor(255, 230, 210, int((35 + 140 * twinkle) * fade)));
		}

		p.save();
		p.translate(sunX, sunY);
		for (auto ray = 0; ray < 14; ++ray) {
			const auto angle = ray * (M_PI / 7.0) + t * 0.06;
			const auto len = sunR * (3.6 + 0.4 * std::sin(t + ray));
			QPainterPath rayPath;
			rayPath.moveTo(0, 0);
			rayPath.lineTo(
				std::cos(angle - 0.035) * len * 0.3,
				std::sin(angle - 0.035) * len * 0.3);
			rayPath.lineTo(std::cos(angle) * len, std::sin(angle) * len);
			rayPath.lineTo(
				std::cos(angle + 0.035) * len * 0.3,
				std::sin(angle + 0.035) * len * 0.3);
			rayPath.closeSubpath();
			p.setBrush(QColor(255, 205, 130, 16 + (ray % 3) * 5));
			p.setPen(Qt::NoPen);
			p.drawPath(rayPath);
		}
		p.restore();

		QRadialGradient sunGlow(QPointF(sunX, sunY), sunR * 5.4);
		sunGlow.setColorAt(0.0, QColor(255, 235, 170, 190));
		sunGlow.setColorAt(0.2, QColor(255, 160, 80, 110));
		sunGlow.setColorAt(0.5, QColor(255, 80, 100, 50));
		sunGlow.setColorAt(1.0, Qt::transparent);
		p.setBrush(sunGlow);
		p.setPen(Qt::NoPen);
		p.drawEllipse(QPointF(sunX, sunY), sunR * 5.4, sunR * 5.4);

		QRadialGradient sunCore(QPointF(sunX, sunY), sunR);
		sunCore.setColorAt(0.0, QColor(255, 250, 225));
		sunCore.setColorAt(0.4, QColor(255, 210, 120));
		sunCore.setColorAt(1.0, QColor(255, 130, 60, 0));
		p.setBrush(sunCore);
		p.drawEllipse(QPointF(sunX, sunY), sunR, sunR);

		auto drawCloud = [&](float64 cx, float64 cy, float64 s, int alpha) {
			p.setBrush(QColor(255, 175, 145, alpha));
			p.drawEllipse(QPointF(cx, cy), s * 1.1, s * 0.55);
			p.drawEllipse(QPointF(cx - s * 0.7, cy + s * 0.08), s * 0.75, s * 0.42);
			p.drawEllipse(QPointF(cx + s * 0.75, cy + s * 0.05), s * 0.8, s * 0.45);
		};
		drawCloud(w * (0.16 + 0.015 * std::sin(t * 0.22)), h * 0.28, w * 0.05, 50);
		drawCloud(w * (0.8 + 0.012 * std::cos(t * 0.18)), h * 0.22, w * 0.055, 40);

		for (auto band = 0; band < 5; ++band) {
			const auto y = h * (0.66 + band * 0.045);
			const auto bandH = h * (0.04 + band * 0.01);
			QLinearGradient haze(0, y, 0, y + bandH);
			haze.setColorAt(0.0, QColor(255, 170, 100, 0));
			haze.setColorAt(0.45, QColor(255, 110, 85, 30 - band * 4));
			haze.setColorAt(1.0, QColor(90, 25, 55, 0));
			p.fillRect(QRectF(0, y, w, bandH), haze);
		}

		const auto waterTop = h * 0.83;
		QLinearGradient water(0, waterTop, 0, h);
		water.setColorAt(0.0, QColor(95, 32, 72, 170));
		water.setColorAt(0.4, QColor(42, 14, 48, 210));
		water.setColorAt(1.0, QColor(10, 5, 20, 245));
		p.fillRect(QRectF(0, waterTop, w, h - waterTop), water);
		QLinearGradient reflection(sunX, waterTop, sunX, waterTop + sunR * 2.6);
		reflection.setColorAt(0.0, QColor(255, 195, 120, 95));
		reflection.setColorAt(0.55, QColor(255, 120, 80, 40));
		reflection.setColorAt(1.0, Qt::transparent);
		p.fillRect(
			QRectF(sunX - sunR * 0.75, waterTop, sunR * 1.5, sunR * 2.6),
			reflection);
		p.setPen(QPen(QColor(255, 205, 160, 30), 1.1));
		for (auto i = 0; i < 7; ++i) {
			const auto y = waterTop + 10 + i * (h - waterTop) / 9.0
				+ 2.0 * std::sin(t * 1.3 + i);
			const auto wobble = 10.0 * std::sin(t * 0.85 + i * 0.6);
			p.drawLine(
				QPointF(w * 0.1 + wobble, y),
				QPointF(w * 0.9 - wobble, y));
		}

		const auto ridgeBase = h * 0.75;
		auto drawRidge = [&](float64 yShift, float64 amp, QColor color) {
			QPainterPath path;
			path.moveTo(0, h);
			path.lineTo(0, ridgeBase + yShift);
			for (auto i = 0; i <= 34; ++i) {
				const auto x = w * (double(i) / 34);
				const auto n = std::sin(i * 0.48 + t * 0.1 + yShift)
					+ 0.4 * std::sin(i * 1.2 - t * 0.07);
				path.lineTo(x, ridgeBase + yShift + amp * n);
			}
			path.lineTo(w, h);
			path.closeSubpath();
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawPath(path);
		};
		drawRidge(0, h * 0.03, QColor(60, 20, 50, 195));
		drawRidge(h * 0.032, h * 0.024, QColor(28, 10, 32, 235));

		p.setBrush(Qt::NoBrush);
		for (auto b = 0; b < 5; ++b) {
			const auto baseX = w * (0.14 + b * 0.17)
				+ w * 0.008 * std::sin(t * 0.35 + b);
			const auto baseY = h * (0.5 + 0.025 * std::sin(t * 0.55 + b));
			const auto wing = 9.0 + 3.5 * std::sin(t * 2.8 + b);
			QPainterPath bird;
			bird.moveTo(baseX, baseY);
			bird.quadTo(
				baseX - wing * 0.55,
				baseY - wing * 0.35,
				baseX - wing,
				baseY + 1.0);
			bird.moveTo(baseX, baseY);
			bird.quadTo(
				baseX + wing * 0.55,
				baseY - wing * 0.35,
				baseX + wing,
				baseY + 1.0);
			p.setPen(QPen(QColor(22, 8, 28, 200), 1.7));
			p.drawPath(bird);
		}

		const auto emerge = EaseOutCubic(Smoothstep(0.7, 2.6, t));
		const auto settle = EaseOutBack(Smoothstep(1.6, 3.2, t));
		logoOpacity = Smoothstep(0.75, 2.1, t);
		logoScale = Lerp(0.12, 1.0, emerge) * hideScale;
		logoCenter = QPointF(
			Lerp(sunX, w * 0.5, settle),
			Lerp(sunY, h * 0.42, settle));
		const auto warm = 1.0 - 0.55 * Smoothstep(2.0, 4.0, t);
		logoTint = QColor(255, 170, 90);
		tintStrength = int(210 * warm);
		bloom = QColor(255, 140, 70);
		bloomStrength = Lerp(1.8, 1.05, Smoothstep(1.5, 3.5, t))
			+ 0.12 * std::sin(t * 2.0);

		QRadialGradient vignette(QPointF(w * 0.5, h * 0.5), minDim * 0.85);
		vignette.setColorAt(0.55, Qt::transparent);
		vignette.setColorAt(1.0, QColor(20, 5, 15, 110));
		p.fillRect(rect(), vignette);

	} else if (animType == 4) {
		const auto phase1 = now / 6000.0;
		const auto phase2 = now / 8000.0;
		p.fillRect(rect(), QColor(3, 0, 8));

		QPointF center1(
			w * (0.3 + 0.2 * std::sin(phase1 * M_PI)),
			h * (0.7 + 0.2 * std::cos(phase1 * M_PI)));
		QRadialGradient nebula1(center1, w * 0.8);
		nebula1.setColorAt(0.0, QColor(50, 10, 80, 45));
		nebula1.setColorAt(1.0, Qt::transparent);
		p.fillRect(rect(), nebula1);

		QPointF center2(
			w * (0.7 + 0.2 * std::cos(phase2 * M_PI)),
			h * (0.3 + 0.2 * std::sin(phase2 * M_PI)));
		QRadialGradient nebula2(center2, w * 0.8);
		nebula2.setColorAt(0.0, QColor(10, 30, 80, 40));
		nebula2.setColorAt(1.0, Qt::transparent);
		p.fillRect(rect(), nebula2);

		if (_stars.empty() && width() > 0) {
			std::srand(unsigned(crl::now()));
			for (auto i = 0; i < 150; ++i) {
				_stars.push_back(QPointF(
					std::rand() % std::max(width(), 1),
					std::rand() % std::max(height(), 1)));
				_starPhases.push_back((std::rand() % 1000) / 1000.0f);
			}
		}
		p.setPen(Qt::NoPen);
		for (size_t i = 0; i < _stars.size(); ++i) {
			const auto twinkle = 0.5 + 0.5 * std::sin(
				(now / 500.0) + _starPhases[i] * M_PI * 2);
			auto driftX = std::fmod(
				_stars[i].x() - (now / 50.0) * (0.5 + _starPhases[i]),
				w);
			if (driftX < 0) {
				driftX += w;
			}
			const auto size = 1.0 + 2.0 * twinkle;
			p.fillRect(
				QRectF(driftX - size / 2, _stars[i].y() - size / 2, size, size),
				QColor(
					200 + int(55 * std::sin(_starPhases[i] * M_PI)),
					200 + int(55 * std::cos(_starPhases[i] * M_PI)),
					255,
					50 + int(200 * twinkle)));
		}

		for (auto c = 0; c < 3; ++c) {
			const auto cometPhase = std::fmod((now / 3000.0) + (c * 0.33), 1.0);
			const auto cometPos = QPointF(
				w * cometPhase,
				h * (cometPhase * 0.5 + c * 0.2));
			p.fillRect(
				QRectF(cometPos.x() - 1, cometPos.y() - 1, 2, 2),
				QColor(255, 255, 255));
			QLinearGradient tail(cometPos, cometPos - QPointF(60, 30));
			tail.setColorAt(0.0, QColor(255, 255, 255, 200));
			tail.setColorAt(1.0, Qt::transparent);
			p.setPen(QPen(QBrush(tail), 2));
			p.drawLine(cometPos, cometPos - QPointF(60, 30));
		}

		p.setPen(Qt::NoPen);
		const auto p1 = QPointF(w * 0.85, h * 0.2);
		const auto p1R = minDim * 0.08;
		QRadialGradient p1Grad(p1 - QPointF(p1R * 0.3, p1R * 0.3), p1R * 1.5);
		p1Grad.setColorAt(0.0, QColor(100, 70, 200));
		p1Grad.setColorAt(0.7, QColor(15, 5, 40));
		p1Grad.setColorAt(1.0, QColor(2, 0, 8));
		p.setBrush(p1Grad);
		p.drawEllipse(p1, p1R, p1R);
		p.save();
		p.translate(p1);
		p.rotate(-25);
		QLinearGradient ringGrad(-p1R * 2.2, 0, p1R * 2.2, 0);
		ringGrad.setColorAt(0.0, Qt::transparent);
		ringGrad.setColorAt(0.2, QColor(150, 100, 200, 100));
		ringGrad.setColorAt(0.5, QColor(200, 150, 200, 150));
		ringGrad.setColorAt(0.8, QColor(150, 100, 200, 100));
		ringGrad.setColorAt(1.0, Qt::transparent);
		p.setPen(QPen(QBrush(ringGrad), std::max(1., minDim / 250)));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QPointF(0, 0), p1R * 2.2, p1R * 0.3);
		p.restore();

		const auto p2 = QPointF(w * 0.15, h * 0.85);
		const auto p2R = minDim * 0.18;
		QRadialGradient p2Grad(p2 + QPointF(p2R * 0.4, -p2R * 0.4), p2R * 1.8);
		p2Grad.setColorAt(0.0, QColor(200, 100, 50, 150));
		p2Grad.setColorAt(0.5, QColor(40, 10, 30, 100));
		p2Grad.setColorAt(1.0, QColor(2, 0, 8, 0));
		p.setPen(Qt::NoPen);
		p.setBrush(p2Grad);
		p.drawEllipse(p2, p2R, p2R);

		const auto reveal = EaseOutBack(Smoothstep(0.4, 2.2, t));
		logoOpacity = Smoothstep(0.3, 1.5, t);
		logoScale = Lerp(0.3, 1.0, reveal) * hideScale;
		logoCenter = QPointF(w * 0.5, h * (0.5 - 0.03 * reveal));
		logoTint = QColor(255, 200, 120);
		tintStrength = int(100 * (1.0 - 0.4 * Smoothstep(1.5, 3.2, t)));
		bloom = QColor(160, 90, 255);
		bloomStrength = 1.2 + 0.15 * std::sin(t * 2.0);
	}

	if (showIcon && logoOpacity > 0.001) {
		const auto size = minDim * 0.28 * logoScale;
		const auto logoRect = QRectF(
			logoCenter.x() - size * 0.5,
			logoCenter.y() - size * 0.5,
			size,
			size);
		drawCinematicLogo(
			p,
			logoRect,
			logoTint,
			tintStrength,
			bloom,
			bloomStrength,
			logoOpacity * screenOpacity);
	}

	if (_hiding && !_opacityAnimation.animating()) {
		_updateTimer.cancel();
		hide();
		deleteLater();
	}
}

} // namespace Ui
