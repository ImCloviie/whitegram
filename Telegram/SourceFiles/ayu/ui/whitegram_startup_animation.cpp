#include "ayu/ui/whitegram_startup_animation.h"

#include "ayu/ui/ayu_logo.h"
#include "ayu/ayu_settings.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "crl/crl.h"
#include "logs.h"
#include "styles/style_basic.h"

#include <QtCore/QElapsedTimer>
#include <QtGui/QFontMetricsF>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QScreen>
#include <QtGui/QtEvents>

#include <atomic>
#include <cmath>
#include <thread>
#include <mutex>
#include <random>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif // Q_OS_WIN

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ui {
namespace {

constexpr auto kHoldMs = crl::time(5200);
constexpr auto kRadianceHoldMs = crl::time(4300);
constexpr auto kOrbitsHoldMs = crl::time(4400);
constexpr auto kDropletHoldMs = crl::time(4600);
constexpr auto kExitMs = crl::time(900);
constexpr auto kPreviewPauseMs = crl::time(500);
constexpr auto kPreviewVirtualWidth = 720.;
constexpr auto kPreviewFps = 120;
constexpr auto kParticleCount = 130;
constexpr auto kLayerScale = 0.5;
constexpr auto kLayerMaxWidth = 960.;
constexpr auto kGlowSize = 192;
constexpr auto kBackdropUpdateMs = crl::time(16);

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

[[nodiscard]] float64 EaseInOutCubic(float64 t) {
	t = Clamp01(t);
	return (t < 0.5)
		? 4. * t * t * t
		: 1. - std::pow(-2. * t + 2., 3.) / 2.;
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

[[nodiscard]] QColor WithAlpha(QColor color, float64 alpha) {
	color.setAlphaF(Clamp01(alpha));
	return color;
}

[[nodiscard]] std::mt19937 &Random() {
	static auto engine = std::mt19937(std::random_device()());
	return engine;
}

[[nodiscard]] float64 RandomUnit() {
	return std::uniform_real_distribution<float64>(0., 1.)(Random());
}

void FillVignette(
		QPainter &p,
		QSizeF size,
		QPointF center,
		float64 from,
		QColor edge) {
	auto vignette = QRadialGradient(
		center,
		std::max(size.width(), size.height()) * 0.8);
	vignette.setColorAt(from, Qt::transparent);
	vignette.setColorAt(1.0, edge);
	p.fillRect(QRectF(QPointF(), size), vignette);
}

void RenderRadianceBackdrop(QPainter &q, QSizeF size, float64 t) {
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto area = QRectF(QPointF(), size);
	Q_UNUSED(minDim);
	Q_UNUSED(area);
	Q_UNUSED(t);
	const auto center = QPointF(w * 0.5, h * 0.46);
	const auto lift = Smoothstep(0., 0.9, t);
	// Graphite backdrop that slowly lifts out of black.
	auto bg = QRadialGradient(center, std::max(w, h) * 0.75);
	bg.setColorAt(0.0, QColor(
		int(Lerp(4, 22, lift)),
		int(Lerp(4, 25, lift)),
		int(Lerp(6, 34, lift))));
	bg.setColorAt(0.6, QColor(9, 10, 15));
	bg.setColorAt(1.0, QColor(3, 3, 5));
	q.fillRect(QRectF(QPointF(), size), bg);

	// Aurora ribbons in white, ice and lavender.
	struct Ribbon {
		float64 base;
		float64 amp;
		float64 freq;
		float64 speed;
		float64 thickness;
		QColor color;
	};
	const Ribbon ribbons[] = {
		{ 0.30, 0.050, 2.1, 0.32, 0.10, QColor(235, 242, 255) },
		{ 0.44, 0.065, 1.6, -0.24, 0.13, QColor(170, 205, 255) },
		{ 0.60, 0.045, 2.6, 0.28, 0.09, QColor(200, 180, 255) },
	};
	const auto auroraAlpha = Smoothstep(0.1, 1.4, t) * 0.55;
	q.setPen(Qt::NoPen);
	for (const auto &ribbon : ribbons) {
		for (auto soft = 0; soft != 7; ++soft) {
			const auto spread = 0.55 + soft * 0.28;
			auto path = QPainterPath();
			const auto steps = 48;
			for (auto i = 0; i <= steps; ++i) {
				const auto x = w * i / float64(steps);
				const auto phase = (i / float64(steps)) * ribbon.freq * M_PI;
				const auto y = h * ribbon.base
					+ h * ribbon.amp * std::sin(phase + t * ribbon.speed * 2.)
					- h * ribbon.thickness * 0.5 * spread;
				if (!i) {
					path.moveTo(x, y);
				} else {
					path.lineTo(x, y);
				}
			}
			for (auto i = steps; i >= 0; --i) {
				const auto x = w * i / float64(steps);
				const auto phase = (i / float64(steps)) * ribbon.freq * M_PI;
				const auto breathe = 0.65
					+ 0.35 * std::sin(phase * 0.7 + t * 0.9);
				const auto y = h * ribbon.base
					+ h * ribbon.amp * std::sin(phase + t * ribbon.speed * 2.)
					+ h * ribbon.thickness * 0.5 * spread * breathe;
				path.lineTo(x, y);
			}
			path.closeSubpath();
			auto fill = QLinearGradient(0, 0, w, 0);
			const auto a = auroraAlpha * 0.085;
			fill.setColorAt(0.0, Qt::transparent);
			fill.setColorAt(0.3, WithAlpha(ribbon.color, a * 0.7));
			fill.setColorAt(0.5, WithAlpha(ribbon.color, a));
			fill.setColorAt(0.75, WithAlpha(ribbon.color, a * 0.6));
			fill.setColorAt(1.0, Qt::transparent);
			q.setBrush(fill);
			q.drawPath(path);
		}
	}
	// Flash when the light meets in the centre.
	const auto flash = Smoothstep(1.55, 1.8, t) * (1. - Smoothstep(1.8, 2.5, t));
	if (flash > 0.) {
		const auto radius = std::min(w, h) * 0.5;
		auto glow = QRadialGradient(center, radius);
		glow.setColorAt(0.0, QColor(225, 235, 255, int(180 * flash)));
		glow.setColorAt(0.25, QColor(225, 235, 255, int(95 * flash)));
		glow.setColorAt(0.6, QColor(205, 222, 255, int(26 * flash)));
		glow.setColorAt(1.0, Qt::transparent);
		q.setPen(Qt::NoPen);
		q.setBrush(glow);
		q.drawEllipse(center, radius, radius);
	}
	FillVignette(q, size, center, 0.6, QColor(0, 0, 0, 120));
}

void RenderSunsetBackdrop(QPainter &q, QSizeF size, float64 t) {
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto area = QRectF(QPointF(), size);
	Q_UNUSED(minDim);
	Q_UNUSED(area);
	Q_UNUSED(t);
	const auto sunX = w * 0.5;
	const auto sunY = h * (0.58 + 0.012 * std::sin(t * 0.55));
	const auto sunR = minDim * 0.145;
	const auto waterTop = h * 0.83;
	auto sky = QLinearGradient(0, 0, 0, h);
	sky.setColorAt(0.0, QColor(10, 8, 38));
	sky.setColorAt(0.25, QColor(68, 24, 96));
	sky.setColorAt(0.48, QColor(190, 55, 95));
	sky.setColorAt(0.68, QColor(255, 125, 65));
	sky.setColorAt(0.85, QColor(255, 185, 100));
	sky.setColorAt(1.0, QColor(255, 135, 85));
	q.fillRect(area, sky);

	q.save();
	q.translate(sunX, sunY);
	q.setPen(Qt::NoPen);
	for (auto ray = 0; ray < 14; ++ray) {
		const auto angle = ray * (M_PI / 7.0) + t * 0.06;
		const auto len = sunR * (3.6 + 0.4 * std::sin(t + ray));
		auto rayPath = QPainterPath();
		rayPath.moveTo(0, 0);
		rayPath.lineTo(
			std::cos(angle - 0.035) * len * 0.3,
			std::sin(angle - 0.035) * len * 0.3);
		rayPath.lineTo(std::cos(angle) * len, std::sin(angle) * len);
		rayPath.lineTo(
			std::cos(angle + 0.035) * len * 0.3,
			std::sin(angle + 0.035) * len * 0.3);
		rayPath.closeSubpath();
		q.setBrush(QColor(255, 205, 130, 16 + (ray % 3) * 5));
		q.drawPath(rayPath);
	}
	q.restore();

	auto sunGlow = QRadialGradient(QPointF(sunX, sunY), sunR * 5.4);
	sunGlow.setColorAt(0.0, QColor(255, 235, 170, 190));
	sunGlow.setColorAt(0.2, QColor(255, 160, 80, 110));
	sunGlow.setColorAt(0.5, QColor(255, 80, 100, 50));
	sunGlow.setColorAt(1.0, Qt::transparent);
	q.setBrush(sunGlow);
	q.setPen(Qt::NoPen);
	q.drawEllipse(QPointF(sunX, sunY), sunR * 5.4, sunR * 5.4);

	auto sunCore = QRadialGradient(QPointF(sunX, sunY), sunR);
	sunCore.setColorAt(0.0, QColor(255, 250, 225));
	sunCore.setColorAt(0.4, QColor(255, 210, 120));
	sunCore.setColorAt(1.0, QColor(255, 130, 60, 0));
	q.setBrush(sunCore);
	q.drawEllipse(QPointF(sunX, sunY), sunR, sunR);

	const auto drawCloud = [&](float64 cx, float64 cy, float64 s, int alpha) {
		q.setBrush(QColor(255, 175, 145, alpha));
		q.drawEllipse(QPointF(cx, cy), s * 1.1, s * 0.55);
		q.drawEllipse(QPointF(cx - s * 0.7, cy + s * 0.08), s * 0.75, s * 0.42);
		q.drawEllipse(QPointF(cx + s * 0.75, cy + s * 0.05), s * 0.8, s * 0.45);
	};
	drawCloud(w * (0.16 + 0.015 * std::sin(t * 0.22)), h * 0.28, w * 0.05, 50);
	drawCloud(w * (0.8 + 0.012 * std::cos(t * 0.18)), h * 0.22, w * 0.055, 40);

	for (auto band = 0; band < 5; ++band) {
		const auto y = h * (0.66 + band * 0.045);
		const auto bandH = h * (0.04 + band * 0.01);
		auto haze = QLinearGradient(0, y, 0, y + bandH);
		haze.setColorAt(0.0, QColor(255, 170, 100, 0));
		haze.setColorAt(0.45, QColor(255, 110, 85, 30 - band * 4));
		haze.setColorAt(1.0, QColor(90, 25, 55, 0));
		q.fillRect(QRectF(0, y, w, bandH), haze);
	}

	auto water = QLinearGradient(0, waterTop, 0, h);
	water.setColorAt(0.0, QColor(95, 32, 72, 170));
	water.setColorAt(0.4, QColor(42, 14, 48, 210));
	water.setColorAt(1.0, QColor(10, 5, 20, 245));
	q.fillRect(QRectF(0, waterTop, w, h - waterTop), water);
	auto reflection = QLinearGradient(sunX, waterTop, sunX, waterTop + sunR * 2.6);
	reflection.setColorAt(0.0, QColor(255, 195, 120, 95));
	reflection.setColorAt(0.55, QColor(255, 120, 80, 40));
	reflection.setColorAt(1.0, Qt::transparent);
	q.fillRect(
		QRectF(sunX - sunR * 0.75, waterTop, sunR * 1.5, sunR * 2.6),
		reflection);
	FillVignette(q, size, QPointF(w * 0.5, h * 0.5), 0.55, QColor(20, 5, 15, 110));
}

void RenderNebulaBackdrop(QPainter &q, QSizeF size, float64 t) {
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto area = QRectF(QPointF(), size);
	Q_UNUSED(minDim);
	Q_UNUSED(area);
	Q_UNUSED(t);
	const auto elapsed = t * 1000.;
	const auto phase1 = elapsed / 6000.0;
	const auto phase2 = elapsed / 8000.0;
	q.fillRect(area, QColor(3, 0, 8));
	const auto center1 = QPointF(
		w * (0.3 + 0.2 * std::sin(phase1 * M_PI)),
		h * (0.7 + 0.2 * std::cos(phase1 * M_PI)));
	auto nebula1 = QRadialGradient(center1, w * 0.8);
	nebula1.setColorAt(0.0, QColor(50, 10, 80, 45));
	nebula1.setColorAt(1.0, Qt::transparent);
	q.fillRect(area, nebula1);

	const auto center2 = QPointF(
		w * (0.7 + 0.2 * std::cos(phase2 * M_PI)),
		h * (0.3 + 0.2 * std::sin(phase2 * M_PI)));
	auto nebula2 = QRadialGradient(center2, w * 0.8);
	nebula2.setColorAt(0.0, QColor(10, 30, 80, 40));
	nebula2.setColorAt(1.0, Qt::transparent);
	q.fillRect(area, nebula2);

	q.setPen(Qt::NoPen);
	const auto p1 = QPointF(w * 0.85, h * 0.2);
	const auto p1R = minDim * 0.08;
	auto p1Grad = QRadialGradient(p1 - QPointF(p1R * 0.3, p1R * 0.3), p1R * 1.5);
	p1Grad.setColorAt(0.0, QColor(100, 70, 200));
	p1Grad.setColorAt(0.7, QColor(15, 5, 40));
	p1Grad.setColorAt(1.0, QColor(2, 0, 8));
	q.setBrush(p1Grad);
	q.drawEllipse(p1, p1R, p1R);
	q.save();
	q.translate(p1);
	q.rotate(-25);
	auto ringGrad = QLinearGradient(-p1R * 2.2, 0, p1R * 2.2, 0);
	ringGrad.setColorAt(0.0, Qt::transparent);
	ringGrad.setColorAt(0.2, QColor(150, 100, 200, 100));
	ringGrad.setColorAt(0.5, QColor(200, 150, 200, 150));
	ringGrad.setColorAt(0.8, QColor(150, 100, 200, 100));
	ringGrad.setColorAt(1.0, Qt::transparent);
	q.setPen(QPen(QBrush(ringGrad), std::max(1., minDim / 250)));
	q.setBrush(Qt::NoBrush);
	q.drawEllipse(QPointF(0, 0), p1R * 2.2, p1R * 0.3);
	q.restore();

	const auto p2 = QPointF(w * 0.15, h * 0.85);
	const auto p2R = minDim * 0.18;
	auto p2Grad = QRadialGradient(p2 + QPointF(p2R * 0.4, -p2R * 0.4), p2R * 1.8);
	p2Grad.setColorAt(0.0, QColor(200, 100, 50, 150));
	p2Grad.setColorAt(0.5, QColor(40, 10, 30, 100));
	p2Grad.setColorAt(1.0, QColor(2, 0, 8, 0));
	q.setPen(Qt::NoPen);
	q.setBrush(p2Grad);
	q.drawEllipse(p2, p2R, p2R);
}

void RenderOrbitsBackdrop(QPainter &q, QSizeF size, float64 t) {
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto area = QRectF(QPointF(), size);
	Q_UNUSED(minDim);
	Q_UNUSED(area);
	Q_UNUSED(t);
	const auto center = QPointF(w * 0.5, h * 0.44);
	auto bg = QRadialGradient(center, std::max(w, h) * 0.8);
	bg.setColorAt(0.0, QColor(26, 30, 56));
	bg.setColorAt(0.45, QColor(11, 13, 28));
	bg.setColorAt(1.0, QColor(3, 4, 10));
	q.fillRect(area, bg);
	auto violet = QRadialGradient(QPointF(w * 0.16, h * 0.18), minDim * 0.75);
	violet.setColorAt(0.0, QColor(110, 80, 220, 46));
	violet.setColorAt(1.0, Qt::transparent);
	q.fillRect(area, violet);
	auto cyan = QRadialGradient(QPointF(w * 0.86, h * 0.88), minDim * 0.85);
	cyan.setColorAt(0.0, QColor(40, 150, 230, 38));
	cyan.setColorAt(1.0, Qt::transparent);
	q.fillRect(area, cyan);
	FillVignette(q, size, center, 0.55, QColor(0, 0, 0, 140));
}

void RenderDropletBackdrop(QPainter &q, QSizeF size, float64 t) {
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto area = QRectF(QPointF(), size);
	Q_UNUSED(minDim);
	Q_UNUSED(area);
	Q_UNUSED(t);
	const auto horizon = h * 0.6;
	const auto water = QRectF(0, horizon, w, h - horizon);
	auto sky = QLinearGradient(0, 0, 0, horizon);
	sky.setColorAt(0.0, QColor(4, 7, 15));
	sky.setColorAt(0.7, QColor(10, 22, 36));
	sky.setColorAt(1.0, QColor(22, 44, 62));
	q.fillRect(QRectF(0, 0, w, horizon), sky);
	auto sea = QLinearGradient(0, horizon, 0, h);
	sea.setColorAt(0.0, QColor(12, 28, 42));
	sea.setColorAt(0.35, QColor(6, 14, 24));
	sea.setColorAt(1.0, QColor(2, 4, 9));
	q.fillRect(water, sea);
	auto haze = QRadialGradient(QPointF(w * 0.5, horizon), minDim * 0.9);
	haze.setColorAt(0.0, QColor(110, 180, 220, 52));
	haze.setColorAt(1.0, Qt::transparent);
	q.save();
	q.translate(w * 0.5, horizon);
	q.scale(1.6, 0.35);
	q.translate(-w * 0.5, -horizon);
	q.fillRect(QRectF(-w, horizon - minDim, w * 3., minDim * 2.), haze);
	q.restore();
	FillVignette(q, size, QPointF(w * 0.5, h * 0.5), 0.55, QColor(0, 0, 0, 150));
}

[[nodiscard]] bool BackdropIsStatic(int style) {
	return (style == kWhitegramStartupStyleOrbits)
		|| (style == kWhitegramStartupStyleDroplet);
}

// Runs on a worker thread, so it only touches its arguments.
[[nodiscard]] QImage RenderBackdrop(
		int style,
		QSizeF size,
		QSize device,
		float64 t) {
	// WHY: still backdrops are drawn once at full size; moving ones at half
	// size and scaled up, which is fast and looks the same for soft light.
	const auto still = BackdropIsStatic(style);
	const auto scale = still
		? 1.
		: std::min(kLayerScale, kLayerMaxWidth / std::max(1, device.width()) * 1.);
	const auto pixels = QSize(
		std::max(1, int(std::ceil(device.width() * scale))),
		std::max(1, int(std::ceil(device.height() * scale))));
	// WHY: soft light is many nearly transparent layers; 8-bit channels
	// turn that into visible bands, so paint in 16-bit and convert once.
	auto image = QImage(pixels, QImage::Format_RGBA64_Premultiplied);
	{
		auto q = QPainter(&image);
		PainterHighQualityEnabler hq(q);
		q.scale(pixels.width() / size.width(), pixels.height() / size.height());
		switch (style) {
		case kWhitegramStartupStyleSunset: RenderSunsetBackdrop(q, size, t); break;
		case kWhitegramStartupStyleNebula: RenderNebulaBackdrop(q, size, t); break;
		case kWhitegramStartupStyleOrbits: RenderOrbitsBackdrop(q, size, t); break;
		case kWhitegramStartupStyleDroplet: RenderDropletBackdrop(q, size, t); break;
		default: RenderRadianceBackdrop(q, size, t); break;
		}
	}
	image = image.convertToFormat(QImage::Format_RGB32);
	return (pixels == device)
		? image
		: image.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace

std::vector<int> WhitegramStartupStyles() {
	return {
		kWhitegramStartupStyleRadiance,
		kWhitegramStartupStyleOrbits,
		kWhitegramStartupStyleDroplet,
		kWhitegramStartupStyleSunset,
		kWhitegramStartupStyleNebula,
	};
}

int NormalizeWhitegramStartupStyle(int style) {
	const auto all = WhitegramStartupStyles();
	return (ranges::find(all, style) != end(all))
		? style
		: kWhitegramStartupStyleRadiance;
}

QString WhitegramStartupStyleName(int style, bool isRu) {
	switch (style) {
	case kWhitegramStartupStyleSunset:
		return isRu ? QString::fromUtf8("Закат") : u"Sunset"_q;
	case kWhitegramStartupStyleNebula:
		return isRu
			? QString::fromUtf8("Космическая туманность")
			: u"Space Nebula"_q;
	case kWhitegramStartupStyleOrbits:
		return isRu ? QString::fromUtf8("Орбиты") : u"Orbits"_q;
	case kWhitegramStartupStyleDroplet:
		return isRu ? QString::fromUtf8("Капля") : u"Droplet"_q;
	}
	return isRu ? QString::fromUtf8("Сияние") : u"Radiance"_q;
}

struct WhitegramStartupScene::BackdropState {
	std::mutex mutex;
	QImage ready;
	int readyStyle = -1;
	QSize readySize;
	crl::time requested = 0;
	bool busy = false;
};

WhitegramStartupScene::WhitegramStartupScene()
: _logo(AyuAssets::currentAppLogo())
, _font(st::semiboldFont->f)
, _backdropState(std::make_shared<BackdropState>()) {
}

void WhitegramStartupScene::drawBackdrop(
		QPainter &p,
		QSizeF size,
		int style,
		float64 t) {
	const auto transform = p.deviceTransform();
	const auto device = QSize(
		std::max(1, int(std::ceil(size.width() * std::abs(transform.m11())))),
		std::max(1, int(std::ceil(size.height() * std::abs(transform.m22())))));
	if (_prewarming) {
		p.fillRect(QRectF(QPointF(), size), QColor(0, 0, 0));
		return;
	}
	const auto state = _backdropState;
	auto image = QImage();
	auto request = false;
	{
		auto lock = std::unique_lock(state->mutex);
		const auto matches = (state->readyStyle == style)
			&& (state->readySize == device);
		if (matches) {
			image = state->ready;
		}
		const auto now = crl::now();
		const auto due = !matches
			|| (!BackdropIsStatic(style)
				&& now - state->requested >= kBackdropUpdateMs);
		if (!state->busy && due) {
			state->busy = true;
			state->requested = now;
			request = true;
		}
	}
	if (request) {
		// WHY: the backdrop is the heaviest layer, so a worker thread draws
		// it while this thread only copies the latest one to the screen.
		crl::async([=] {
			auto result = RenderBackdrop(style, size, device, t);
			auto lock = std::unique_lock(state->mutex);
			state->ready = std::move(result);
			state->readyStyle = style;
			state->readySize = device;
			state->busy = false;
		});
	}
	const auto area = QRectF(QPointF(), size);
	if (image.isNull()) {
		p.fillRect(area, QColor(0, 0, 0));
		return;
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	p.drawImage(area, image);
	p.restore();
}

crl::time WhitegramStartupScene::HoldDuration(int style) {
	switch (NormalizeWhitegramStartupStyle(style)) {
	case kWhitegramStartupStyleRadiance: return kRadianceHoldMs;
	case kWhitegramStartupStyleOrbits: return kOrbitsHoldMs;
	case kWhitegramStartupStyleDroplet: return kDropletHoldMs;
	}
	return kHoldMs;
}

crl::time WhitegramStartupScene::ExitDuration() {
	return kExitMs;
}

const QImage &WhitegramStartupScene::silhouette(const QColor &tint) {
	const auto key = quint32(tint.rgb() & 0xFFFFFFU);
	for (const auto &[cached, image] : _silhouettes) {
		if (cached == key) {
			return image;
		}
	}
	auto image = _logo.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	{
		auto lp = QPainter(&image);
		lp.setCompositionMode(QPainter::CompositionMode_SourceIn);
		lp.fillRect(image.rect(), tint);
	}
	_silhouettes.emplace_back(key, std::move(image));
	return _silhouettes.back().second;
}

void WhitegramStartupScene::composeLogo(
		QPainter &p,
		const QRectF &rect,
		const QColor &tint,
		int strength) {
	// WHY: a tinted copy per tint step stalled frames while the logo
	// faded to colour; one silhouette drawn on top looks the same.
	p.drawImage(rect, _logo);
	if (strength > 0) {
		const auto was = p.opacity();
		p.setOpacity(was * std::min(strength, 255) / 255.);
		p.drawImage(rect, silhouette(tint));
		p.setOpacity(was);
	}
}

const QImage &WhitegramStartupScene::glowSprite(const QColor &color) {
	const auto key = quint32(color.rgb() & 0xFFFFFFU);
	for (const auto &[cached, image] : _glows) {
		if (cached == key) {
			return image;
		}
	}
	auto image = QImage(
		QSize(kGlowSize, kGlowSize),
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		PainterHighQualityEnabler hq(p);
		const auto half = kGlowSize / 2.;
		auto grad = QRadialGradient(QPointF(half, half), half);
		grad.setColorAt(0.0, WithAlpha(color, 1.));
		grad.setColorAt(0.22, WithAlpha(color, 0.55));
		grad.setColorAt(0.5, WithAlpha(color, 0.17));
		grad.setColorAt(0.78, WithAlpha(color, 0.04));
		grad.setColorAt(1.0, WithAlpha(color, 0.));
		p.fillRect(image.rect(), grad);
	}
	_glows.emplace_back(key, std::move(image));
	return _glows.back().second;
}

void WhitegramStartupScene::drawGlow(
		QPainter &p,
		QPointF center,
		float64 radius,
		const QColor &color,
		float64 opacity) {
	if (opacity <= 0.003 || radius <= 0.5) {
		return;
	}
	const auto &sprite = glowSprite(color);
	const auto was = p.opacity();
	p.setOpacity(was * std::min(opacity, 1.));
	p.drawImage(
		QRectF(
			center.x() - radius,
			center.y() - radius,
			radius * 2.,
			radius * 2.),
		sprite);
	p.setOpacity(was);
}

void WhitegramStartupScene::drawWordmark(
		QPainter &p,
		QPointF center,
		float64 baseline,
		float64 minDim,
		float64 opacity,
		Fn<void(int, int, float64, float64&, QPointF&)> letter) {
	const auto word = u"Whitegram"_q;
	auto font = _font;
	font.setPixelSize(std::max(8, int(minDim * 0.058)));
	font.setLetterSpacing(QFont::PercentageSpacing, 112.);
	const auto metrics = QFontMetricsF(font);
	const auto total = metrics.horizontalAdvance(word);
	auto x = center.x() - total / 2.;
	p.setFont(font);
	for (auto i = 0; i != word.size(); ++i) {
		const auto glyph = QString(word[i]);
		const auto advance = metrics.horizontalAdvance(glyph);
		auto alpha = 0.;
		auto shift = QPointF();
		letter(i, word.size(), x + advance / 2. - center.x(), alpha, shift);
		if (alpha > 0.003) {
			p.setPen(WithAlpha(QColor(244, 247, 255), alpha * opacity));
			p.drawText(QPointF(x, baseline) + shift, glyph);
		}
		x += advance;
	}
}

void WhitegramStartupScene::ensureStars(
		QSizeF size,
		int style,
		int count,
		float64 heightPart) {
	if (_starsStyle == style && _starsSize == size && !_stars.empty()) {
		return;
	}
	_starsStyle = style;
	_starsSize = size;
	_stars.clear();
	_stars.reserve(count);
	for (auto i = 0; i != count; ++i) {
		_stars.push_back({
			.position = QPointF(
				RandomUnit() * size.width(),
				RandomUnit() * size.height() * heightPart),
			.phase = float(RandomUnit()),
		});
	}
}

void WhitegramStartupScene::ensureParticles() {
	if (!_particles.empty()) {
		return;
	}
	_particles.reserve(kParticleCount);
	for (auto i = 0; i != kParticleCount; ++i) {
		_particles.push_back({
			.angle = RandomUnit() * 2. * M_PI,
			.radius = 0.45 + 0.6 * RandomUnit(),
			.delay = RandomUnit(),
			.spin = (RandomUnit() < 0.5 ? -1. : 1.) * (0.6 + RandomUnit()),
			.size = 0.8 + 1.8 * RandomUnit(),
			.hue = RandomUnit(),
		});
	}
}

void WhitegramStartupScene::drawLogo(
		QPainter &p,
		const QRectF &rect,
		const QColor &tint,
		int tintStrength,
		const QColor &bloom,
		float64 bloomStrength,
		float64 opacity,
		float64 shine) {
	if (_logo.isNull() || opacity <= 0.001 || rect.isEmpty()) {
		return;
	}
	PainterHighQualityEnabler hq(p);
	const auto baseOpacity = p.opacity();
	drawGlow(
		p,
		rect.center(),
		rect.width() * (0.62 + 0.2 * bloomStrength),
		bloom,
		opacity * bloomStrength * 0.3);

	p.setOpacity(baseOpacity * opacity);
	if (shine > 0. && shine < 1.) {
		const auto transform = p.deviceTransform();
		const auto pixels = QSize(
			std::max(1, int(std::ceil(rect.width() * std::abs(transform.m11())))),
			std::max(1, int(std::ceil(rect.height() * std::abs(transform.m22())))));
		if (_shineBuffer.size() != pixels) {
			_shineBuffer = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
		}
		_shineBuffer.fill(Qt::transparent);
		{
			auto bp = QPainter(&_shineBuffer);
			PainterHighQualityEnabler bhq(bp);
			composeLogo(bp, QRectF(_shineBuffer.rect()), tint, tintStrength);
			bp.setCompositionMode(QPainter::CompositionMode_SourceAtop);
			const auto w = float64(pixels.width());
			const auto h = float64(pixels.height());
			const auto x = Lerp(-0.6 * w, 1.6 * w, shine);
			auto band = QLinearGradient(x - w * 0.25, 0, x + w * 0.25, h * 0.4);
			band.setColorAt(0.0, Qt::transparent);
			band.setColorAt(0.5, QColor(255, 255, 255, 170));
			band.setColorAt(1.0, Qt::transparent);
			bp.fillRect(_shineBuffer.rect(), band);
		}
		p.drawImage(rect, _shineBuffer);
	} else {
		composeLogo(p, rect, tint, tintStrength);
	}
	p.setOpacity(baseOpacity);
}

void WhitegramStartupScene::paintRadiance(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 opacity,
		float64 exitScale) {
	PainterHighQualityEnabler hq(p);
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto center = QPointF(w * 0.5, h * 0.46);

	drawBackdrop(p, size, kWhitegramStartupStyleRadiance, t);
	const auto lift = Smoothstep(0., 0.9, t);

	// Fine white dust.
	ensureStars(size, kWhitegramStartupStyleRadiance, 110, 1.);
	for (const auto &star : _stars) {
		const auto twinkle = 0.5 + 0.5 * std::sin(t * 1.7 + star.phase * 6.283);
		const auto s = 0.6 + 1.3 * twinkle;
		p.fillRect(
			QRectF(star.position.x() - s / 2, star.position.y() - s / 2, s, s),
			QColor(235, 240, 255, int((25 + 110 * twinkle) * lift)));
	}

	// Light particles spiral in and gather into the logo.
	ensureParticles();
	const auto gatherFrom = 0.15;
	const auto gatherTo = 1.75;
	const auto particlePosition = [&](const Particle &particle, float64 q) {
		const auto r = particle.radius * minDim * 0.62 * (1. - q);
		const auto angle = particle.angle + particle.spin * q * 2.4;
		return QPointF(
			center.x() + std::cos(angle) * r,
			center.y() + std::sin(angle) * r * 0.82);
	};
	for (const auto &particle : _particles) {
		const auto local = t - particle.delay * 0.35;
		const auto q = EaseInOutCubic(Smoothstep(gatherFrom, gatherTo, local));
		if (q >= 0.995) {
			continue;
		}
		const auto appear = Smoothstep(gatherFrom - 0.1, gatherFrom + 0.4, local);
		if (appear <= 0.) {
			continue;
		}
		const auto color = QColor::fromHslF(0.62 + 0.12 * particle.hue, 0.35, 0.9);
		const auto head = particlePosition(particle, q);
		const auto tail = particlePosition(particle, std::max(0., q - 0.05));
		p.setPen(QPen(
			WithAlpha(color, 0.3 * appear),
			particle.size * 0.8,
			Qt::SolidLine,
			Qt::RoundCap));
		p.drawLine(tail, head);
		drawGlow(p, head, particle.size * 2.6, QColor(255, 255, 255), appear);
	}

	// Flash and shock ring when the light meets in the centre.
	const auto ring = Smoothstep(1.65, 2.7, t);
	if (ring > 0. && ring < 1.) {
		const auto radius = Lerp(0.05, 0.7, EaseOutCubic(ring)) * minDim;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(
			WithAlpha(QColor(225, 235, 255), 0.5 * (1. - ring)),
			std::max(1., minDim * 0.004 * (1. - ring))));
		p.drawEllipse(center, radius, radius * 0.92);
	}

	// The logo resolves from a white silhouette into its real colours.
	const auto logoSize = minDim * 0.26 * exitScale
		* Lerp(0.6, 1.0, EaseOutBack(Smoothstep(1.55, 2.4, t)));
	if (showIcon) {
		drawLogo(
			p,
			QRectF(
				center.x() - logoSize * 0.5,
				center.y() - logoSize * 0.5,
				logoSize,
				logoSize),
			QColor(255, 255, 255),
			int(200 * (1. - Smoothstep(1.9, 2.8, t))),
			QColor(205, 222, 255),
			Lerp(1.6, 1.0, Smoothstep(1.8, 3.0, t)) + 0.12 * std::sin(t * 2.2),
			Smoothstep(1.55, 1.95, t) * opacity,
			Smoothstep(2.5, 3.3, t));
	}

	// Letter by letter wordmark.
	const auto fontSize = std::max(8., minDim * 0.058);
	const auto baseline = center.y()
		+ (showIcon ? minDim * 0.26 * 0.62 : 0.)
		+ fontSize * 0.95 * 1.15;
	drawWordmark(p, center, baseline, minDim, opacity, [&](
			int i,
			int,
			float64,
			float64 &alpha,
			QPointF &shift) {
		const auto appear = EaseOutCubic(
			Smoothstep(2.15 + i * 0.055, 2.6 + i * 0.055, t));
		alpha = appear;
		shift = QPointF(0., (1. - appear) * minDim * 0.02);
	});
}

void WhitegramStartupScene::paintSunset(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale) {
	PainterHighQualityEnabler hq(p);
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto sunBob = 0.012 * std::sin(t * 0.55);
	const auto sunX = w * 0.5;
	const auto sunY = h * (0.58 + sunBob);
	const auto waterTop = h * 0.83;

	drawBackdrop(p, size, kWhitegramStartupStyleSunset, t);

	ensureStars(size, kWhitegramStartupStyleSunset, 80, 0.42);
	for (const auto &star : _stars) {
		const auto twinkle = 0.35 + 0.65 * std::sin(
			t * 1.6 + star.phase * M_PI * 2);
		const auto fade = 1.0 - (star.position.y() / (h * 0.45));
		const auto s = 1.0 + 1.7 * twinkle;
		p.fillRect(
			QRectF(star.position.x() - s / 2, star.position.y() - s / 2, s, s),
			QColor(255, 230, 210, std::clamp(int((35 + 140 * twinkle) * fade), 0, 255)));
	}

	p.setPen(QPen(QColor(255, 205, 160, 30), std::max(1., minDim / 800.)));
	for (auto i = 0; i < 7; ++i) {
		const auto y = waterTop + minDim * 0.012 + i * (h - waterTop) / 9.0
			+ minDim * 0.003 * std::sin(t * 1.3 + i);
		const auto wobble = minDim * 0.012 * std::sin(t * 0.85 + i * 0.6);
		p.drawLine(QPointF(w * 0.1 + wobble, y), QPointF(w * 0.9 - wobble, y));
	}

	const auto ridgeBase = h * 0.75;
	const auto drawRidge = [&](float64 yShift, float64 amp, QColor color) {
		auto path = QPainterPath();
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
	p.setPen(QPen(QColor(22, 8, 28, 200), std::max(1., minDim / 500.)));
	for (auto b = 0; b < 5; ++b) {
		const auto baseX = w * (0.14 + b * 0.17)
			+ w * 0.008 * std::sin(t * 0.35 + b);
		const auto baseY = h * (0.5 + 0.025 * std::sin(t * 0.55 + b));
		const auto wing = minDim * (0.011 + 0.004 * std::sin(t * 2.8 + b));
		auto bird = QPainterPath();
		bird.moveTo(baseX, baseY);
		bird.quadTo(baseX - wing * 0.55, baseY - wing * 0.35, baseX - wing, baseY + 1.0);
		bird.moveTo(baseX, baseY);
		bird.quadTo(baseX + wing * 0.55, baseY - wing * 0.35, baseX + wing, baseY + 1.0);
		p.drawPath(bird);
	}

	if (showIcon) {
		const auto emerge = EaseOutCubic(Smoothstep(0.7, 2.6, t));
		const auto settle = EaseOutBack(Smoothstep(1.6, 3.2, t));
		const auto s = minDim * 0.28 * Lerp(0.12, 1.0, emerge) * exitScale;
		const auto logoCenter = QPointF(
			Lerp(sunX, w * 0.5, settle),
			Lerp(sunY, h * 0.42, settle));
		const auto warm = 1.0 - 0.55 * Smoothstep(2.0, 4.0, t);
		drawLogo(
			p,
			QRectF(logoCenter.x() - s * 0.5, logoCenter.y() - s * 0.5, s, s),
			QColor(255, 170, 90),
			int(210 * warm),
			QColor(255, 140, 70),
			Lerp(1.8, 1.05, Smoothstep(1.5, 3.5, t)) + 0.12 * std::sin(t * 2.0),
			Smoothstep(0.75, 2.1, t),
			0.);
	}
}

void WhitegramStartupScene::paintNebula(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale) {
	PainterHighQualityEnabler hq(p);
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto elapsed = t * 1000.;

	drawBackdrop(p, size, kWhitegramStartupStyleNebula, t);

	ensureStars(size, kWhitegramStartupStyleNebula, 150, 1.);
	p.setPen(Qt::NoPen);
	for (const auto &star : _stars) {
		const auto twinkle = 0.5 + 0.5 * std::sin(
			(elapsed / 500.0) + star.phase * M_PI * 2);
		auto driftX = std::fmod(
			star.position.x() - (elapsed / 50.0) * (0.5 + star.phase),
			w);
		if (driftX < 0) {
			driftX += w;
		}
		const auto s = 1.0 + 2.0 * twinkle;
		p.fillRect(
			QRectF(driftX - s / 2, star.position.y() - s / 2, s, s),
			QColor(
				200 + int(55 * std::sin(star.phase * M_PI)),
				200 + int(55 * std::cos(star.phase * M_PI)),
				255,
				50 + int(200 * twinkle)));
	}

	const auto tailVector = QPointF(minDim * 0.08, minDim * 0.04);
	for (auto c = 0; c < 3; ++c) {
		const auto cometPhase = std::fmod((elapsed / 3000.0) + (c * 0.33), 1.0);
		const auto cometPos = QPointF(w * cometPhase, h * (cometPhase * 0.5 + c * 0.2));
		for (auto k = 0; k != 4; ++k) {
			const auto from = cometPos - tailVector * (k / 4.);
			const auto to = cometPos - tailVector * ((k + 1) / 4.);
			p.setPen(QPen(QColor(255, 255, 255, 200 - k * 50), 2));
			p.drawLine(from, to);
		}
		drawGlow(p, cometPos, minDim * 0.012, QColor(255, 255, 255), 0.9);
	}

	if (showIcon) {
		const auto reveal = EaseOutBack(Smoothstep(0.4, 2.2, t));
		const auto s = minDim * 0.28 * Lerp(0.3, 1.0, reveal) * exitScale;
		const auto logoCenter = QPointF(w * 0.5, h * (0.5 - 0.03 * reveal));
		drawLogo(
			p,
			QRectF(logoCenter.x() - s * 0.5, logoCenter.y() - s * 0.5, s, s),
			QColor(255, 200, 120),
			int(100 * (1.0 - 0.4 * Smoothstep(1.5, 3.2, t))),
			QColor(160, 90, 255),
			1.2 + 0.15 * std::sin(t * 2.0),
			Smoothstep(0.3, 1.5, t),
			0.);
	}
}

void WhitegramStartupScene::paintOrbits(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale) {
	PainterHighQualityEnabler hq(p);
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto area = QRectF(QPointF(), size);
	const auto center = QPointF(w * 0.5, h * 0.44);

	drawBackdrop(p, size, kWhitegramStartupStyleOrbits, t);
	const auto lift = Smoothstep(0., 0.8, t);
	if (lift < 1.) {
		p.fillRect(area, QColor(0, 0, 0, int(std::round((1. - lift) * 255))));
	}

	ensureStars(size, kWhitegramStartupStyleOrbits, 90, 1.);
	for (const auto &star : _stars) {
		const auto twinkle = 0.5 + 0.5 * std::sin(t * 1.3 + star.phase * 6.283);
		const auto s = 0.7 + 1.1 * twinkle;
		p.fillRect(
			QRectF(star.position.x() - s / 2, star.position.y() - s / 2, s, s),
			QColor(220, 230, 255, int((20 + 90 * twinkle) * lift)));
	}

	struct Orbit {
		float64 radius;
		float64 ratio;
		float64 tilt;
		float64 speed;
		float64 start;
		QColor color;
		int bodies;
	};
	const Orbit orbits[] = {
		{ 0.22, 0.36, -16., 0.95, 0.3, QColor(190, 215, 255), 1 },
		{ 0.32, 0.33, 9., -0.62, 2.1, QColor(210, 195, 255), 2 },
		{ 0.43, 0.30, -5., 0.42, 4.2, QColor(160, 228, 255), 2 },
	};
	const auto sway = 3.5 * std::sin(t * 0.45);

	struct Body {
		QPointF position;
		float64 depth = 0.;
		float64 alpha = 0.;
		QColor color;
		std::array<QPointF, 6> trail;
	};
	auto bodies = std::vector<Body>();
	bodies.reserve(5);

	for (auto i = 0; i != 3; ++i) {
		const auto &orbit = orbits[i];
		const auto rx = orbit.radius * minDim;
		const auto ry = rx * orbit.ratio;
		auto transform = QTransform();
		transform.translate(center.x(), center.y());
		transform.rotate(orbit.tilt + sway * (i % 2 ? -1. : 1.));
		const auto reveal = EaseInOutCubic(
			Smoothstep(0.25 + i * 0.18, 1.35 + i * 0.18, t));
		if (reveal > 0.) {
			p.save();
			p.setTransform(transform, true);
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(
				WithAlpha(orbit.color, 0.3 * lift),
				std::max(1., minDim * 0.0018)));
			const auto startDeg = 90. + i * 70.;
			p.drawArc(
				QRectF(-rx, -ry, rx * 2., ry * 2.),
				int(startDeg * 16),
				-int(reveal * 360. * 16));
			p.restore();
			if (reveal < 1.) {
				const auto a = (startDeg - reveal * 360.) * M_PI / 180.;
				const auto tip = transform.map(
					QPointF(rx * std::cos(a), -ry * std::sin(a)));
				drawGlow(
					p,
					tip,
					minDim * 0.03,
					orbit.color,
					0.9 * std::sin(reveal * M_PI));
			}
		}
		const auto appear = Smoothstep(1.05 + i * 0.18, 1.7 + i * 0.18, t);
		if (appear <= 0.) {
			continue;
		}
		for (auto b = 0; b != orbit.bodies; ++b) {
			const auto angle = orbit.start + b * M_PI + orbit.speed * t;
			const auto point = [&](float64 a) {
				return transform.map(
					QPointF(rx * std::cos(a), ry * std::sin(a)));
			};
			auto body = Body{
				.position = point(angle),
				.depth = std::sin(angle),
				.alpha = appear,
				.color = orbit.color,
			};
			const auto direction = (orbit.speed > 0.) ? 1. : -1.;
			for (auto k = 0; k != int(body.trail.size()); ++k) {
				body.trail[k] = point(angle - direction * 0.05 * (k + 1));
			}
			bodies.push_back(body);
		}
	}
	const auto drawBody = [&](const Body &body) {
		const auto scale = 1. + 0.35 * body.depth;
		const auto radius = minDim * 0.0065 * scale;
		p.setPen(Qt::NoPen);
		for (auto k = 0; k != int(body.trail.size()); ++k) {
			const auto fade = 1. - (k + 1) / float64(body.trail.size() + 1);
			p.setBrush(WithAlpha(body.color, 0.45 * fade * body.alpha));
			p.drawEllipse(body.trail[k], radius * fade, radius * fade);
		}
		drawGlow(p, body.position, radius * 6., body.color, 0.55 * body.alpha);
		p.setBrush(WithAlpha(QColor(255, 255, 255), body.alpha));
		p.drawEllipse(body.position, radius, radius);
	};
	for (const auto &body : bodies) {
		if (body.depth < 0.) {
			drawBody(body);
		}
	}

	const auto core = Smoothstep(0.5, 1.3, t)
		* (1. - 0.65 * Smoothstep(1.9, 2.6, t));
	drawGlow(
		p,
		center,
		minDim * (0.14 + 0.1 * core),
		QColor(200, 215, 255),
		0.75 * core);

	const auto pulse = Smoothstep(1.35, 2.6, t);
	if (pulse > 0. && pulse < 1.) {
		const auto radius = Lerp(0.05, 0.6, EaseOutCubic(pulse)) * minDim;
		p.save();
		p.translate(center);
		p.rotate(-4. + sway);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(
			WithAlpha(QColor(225, 235, 255), 0.5 * (1. - pulse)),
			std::max(1., minDim * 0.004 * (1. - pulse))));
		p.drawEllipse(QPointF(), radius, radius * 0.34);
		p.restore();
	}

	const auto logoSize = minDim * 0.22;
	if (showIcon) {
		const auto s = logoSize * exitScale
			* Lerp(0.55, 1.0, EaseOutBack(Smoothstep(1.3, 2.2, t)));
		drawLogo(
			p,
			QRectF(center.x() - s * 0.5, center.y() - s * 0.5, s, s),
			QColor(255, 255, 255),
			int(230 * (1. - Smoothstep(1.7, 2.6, t))),
			QColor(170, 190, 255),
			Lerp(1.5, 1.0, Smoothstep(1.7, 3.0, t)) + 0.1 * std::sin(t * 2.),
			Smoothstep(1.3, 1.9, t),
			Smoothstep(2.6, 3.4, t));
	}

	for (const auto &body : bodies) {
		if (body.depth >= 0.) {
			drawBody(body);
		}
	}

	// Letters fly together from a wide tracking.
	const auto fontSize = std::max(8., minDim * 0.058);
	const auto baseline = center.y()
		+ (showIcon ? logoSize * 0.62 : 0.)
		+ fontSize * 1.1;
	const auto gather = EaseOutCubic(Smoothstep(2.05, 3.0, t));
	drawWordmark(p, center, baseline, minDim, 1., [&](
			int,
			int,
			float64 offset,
			float64 &alpha,
			QPointF &shift) {
		alpha = Smoothstep(2.05, 2.65, t);
		shift = QPointF(offset * (1. - gather) * 0.9, 0.);
	});
}

void WhitegramStartupScene::paintDroplet(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale) {
	PainterHighQualityEnabler hq(p);
	const auto w = size.width();
	const auto h = size.height();
	const auto minDim = std::min(w, h);
	const auto area = QRectF(QPointF(), size);
	const auto horizon = h * 0.6;
	const auto impact = QPointF(w * 0.5, h * 0.74);
	const auto target = QPointF(w * 0.5, h * 0.33);
	const auto water = QRectF(0, horizon, w, h - horizon);
	const auto base = p.opacity();

	drawBackdrop(p, size, kWhitegramStartupStyleDroplet, t);
	const auto lift = Smoothstep(0., 0.8, t);
	if (lift < 1.) {
		p.fillRect(area, QColor(0, 0, 0, int(std::round((1. - lift) * 255))));
	}

	ensureStars(size, kWhitegramStartupStyleDroplet, 70, 0.55);
	for (const auto &star : _stars) {
		const auto twinkle = 0.5 + 0.5 * std::sin(t * 1.5 + star.phase * 6.283);
		const auto s = 0.7 + 1.0 * twinkle;
		const auto fade = 1. - star.position.y() / horizon;
		p.fillRect(
			QRectF(star.position.x() - s / 2, star.position.y() - s / 2, s, s),
			QColor(215, 235, 255, int((20 + 100 * twinkle) * fade * lift)));
	}

	const auto lightColor = QColor(185, 228, 255);

	// A drop of light falls onto still water.
	const auto fall = Smoothstep(0.25, 0.95, t);
	if (fall > 0. && t < 0.97) {
		const auto eased = fall * fall;
		const auto y = Lerp(-minDim * 0.05, impact.y(), eased);
		const auto head = QPointF(impact.x(), y);
		const auto streak = minDim * (0.02 + 0.12 * eased);
		for (auto k = 0; k != 4; ++k) {
			p.setPen(QPen(
				WithAlpha(QColor(235, 245, 255), 0.6 * (1. - k / 4.)),
				std::max(1., minDim * 0.003),
				Qt::SolidLine,
				Qt::RoundCap));
			p.drawLine(
				head - QPointF(0, streak * k / 4.),
				head - QPointF(0, streak * (k + 1) / 4.));
		}
		drawGlow(p, head, minDim * 0.035, lightColor, 0.95);
		// Its reflection grows as it approaches the surface.
		const auto mirror = QPointF(impact.x(), 2. * horizon - y);
		if (mirror.y() > horizon) {
			drawGlow(p, mirror, minDim * 0.03, lightColor, 0.35 * eased);
		}
	}

	// Mirror of the logo on the water, cut by soft swell lines.
	const auto rise = EaseOutCubic(Smoothstep(1.15, 2.3, t));
	const auto logoCenter = QPointF(
		target.x(),
		Lerp(impact.y(), target.y(), rise));
	const auto logoSize = minDim * 0.2;
	const auto logoScale = Lerp(0.3, 1.0, EaseOutBack(Smoothstep(1.15, 2.4, t)))
		* exitScale;
	const auto logoOpacity = Smoothstep(1.1, 1.6, t);
	const auto tintStrength = int(220 * (1. - Smoothstep(1.8, 2.7, t)));
	if (showIcon && logoOpacity > 0. && logoCenter.y() < horizon) {
		const auto s = logoSize * logoScale;
		const auto mirrorY = 2. * horizon - logoCenter.y();
		const auto transform = p.deviceTransform();
		const auto pixels = QSize(
			std::max(1, int(std::ceil(s * std::abs(transform.m11())))),
			std::max(1, int(std::ceil(s * 0.85 * std::abs(transform.m22())))));
		if (_mirrorBuffer.size() != pixels) {
			_mirrorBuffer = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
		}
		_mirrorBuffer.fill(Qt::transparent);
		{
			auto mp = QPainter(&_mirrorBuffer);
			PainterHighQualityEnabler mhq(mp);
			const auto bw = float64(pixels.width());
			const auto bh = float64(pixels.height());
			mp.translate(0, bh);
			mp.scale(1., -1.);
			composeLogo(mp, QRectF(0, 0, bw, bh), QColor(255, 255, 255), tintStrength);
			mp.resetTransform();
			// Fades with depth and breaks into soft swell lines.
			mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			auto fadeOut = QLinearGradient(0, 0, 0, bh);
			fadeOut.setColorAt(0.0, QColor(0, 0, 0, 255));
			fadeOut.setColorAt(1.0, QColor(0, 0, 0, 0));
			mp.fillRect(QRectF(0, 0, bw, bh), fadeOut);
			mp.setCompositionMode(QPainter::CompositionMode_DestinationOut);
			const auto bands = 7;
			for (auto j = 0; j != bands; ++j) {
				const auto y = (j + 0.6) * bh / bands
					+ bh * 0.02 * std::sin(t * 2.4 + j * 1.7);
				const auto thickness = std::max(1., bh * (0.012 + 0.01 * j / bands));
				mp.fillRect(
					QRectF(0, y, bw, thickness),
					QColor(0, 0, 0, 150));
			}
		}
		const auto sway = minDim * 0.004 * std::sin(t * 1.6);
		p.save();
		p.setClipRect(water);
		p.setOpacity(base * 0.42 * logoOpacity);
		p.drawImage(
			QRectF(
				logoCenter.x() - s * 0.5 + sway,
				mirrorY - s * 0.85 * 0.5,
				s,
				s * 0.85),
			_mirrorBuffer);
		p.restore();
	}

	// Rings spread across the surface in perspective.
	p.save();
	p.setClipRect(water);
	p.setBrush(Qt::NoBrush);
	for (auto k = 0; k != 8; ++k) {
		const auto born = 0.95 + k * 0.5;
		const auto age = (t - born) / 2.5;
		if (age <= 0. || age >= 1.) {
			continue;
		}
		const auto r = minDim * Lerp(0.015, 0.95, EaseOutCubic(age));
		const auto alpha = std::pow(1. - age, 1.6) * (k ? 0.42 : 0.6);
		// WHY: hairline pens take Qt's fast stroker; thick ones made the
		// rings the slowest part of the frame, so thickness is stacked lines.
		auto pen = QPen(WithAlpha(lightColor, alpha), 1.);
		pen.setCosmetic(true);
		p.setPen(pen);
		const auto lines = (age < 0.35) ? 3 : 2;
		for (auto line = 0; line != lines; ++line) {
			const auto grow = line * 0.9;
			p.drawEllipse(impact, r + grow, r * 0.2 + grow);
		}
	}
	p.restore();

	// Splash flash, flattened onto the water plane.
	const auto flash = Smoothstep(0.9, 0.98, t) * (1. - Smoothstep(0.98, 1.7, t));
	if (flash > 0.) {
		p.save();
		p.translate(impact);
		p.scale(1., 0.32);
		drawGlow(p, QPointF(), minDim * 0.3, lightColor, flash);
		p.restore();
		drawGlow(p, impact, minDim * 0.06, QColor(255, 255, 255), flash);
	}

	// Droplets jump out of the splash.
	ensureParticles();
	const auto splash = t - 0.95;
	if (splash > 0. && splash < 0.8) {
		p.setPen(Qt::NoPen);
		for (auto i = 0; i != 14; ++i) {
			const auto &particle = _particles[i];
			const auto vx = std::cos(particle.angle) * minDim * 0.16;
			const auto vy = minDim * (0.35 + 0.3 * particle.delay);
			const auto g = minDim * 1.4;
			const auto x = impact.x() + vx * splash;
			const auto y = impact.y() - (vy * splash - g * splash * splash / 2.);
			if (y > impact.y() + 1.) {
				continue;
			}
			const auto alpha = 1. - splash / 0.8;
			const auto r = minDim * 0.0025 * particle.size;
			p.setBrush(WithAlpha(QColor(225, 242, 255), 0.85 * alpha));
			p.drawEllipse(QPointF(x, y), r, r);
		}
	}

	if (showIcon) {
		const auto s = logoSize * logoScale;
		drawLogo(
			p,
			QRectF(logoCenter.x() - s * 0.5, logoCenter.y() - s * 0.5, s, s),
			QColor(255, 255, 255),
			tintStrength,
			QColor(150, 215, 255),
			Lerp(1.6, 1.0, Smoothstep(1.8, 3.0, t)) + 0.1 * std::sin(t * 2.),
			logoOpacity,
			Smoothstep(2.7, 3.5, t));
	}

	// Letters surface from the middle outwards.
	const auto fontSize = std::max(8., minDim * 0.058);
	const auto baseline = target.y()
		+ (showIcon ? logoSize * 0.62 : 0.)
		+ fontSize * 1.1;
	drawWordmark(p, target, baseline, minDim, 1., [&](
			int i,
			int count,
			float64,
			float64 &alpha,
			QPointF &shift) {
		const auto distance = std::abs(i - (count - 1) / 2.);
		const auto appear = EaseOutCubic(Smoothstep(
			2.2 + distance * 0.07,
			2.75 + distance * 0.07,
			t));
		alpha = appear;
		shift = QPointF(0., (1. - appear) * minDim * 0.025);
	});
}

void WhitegramStartupScene::prewarm(
		QSize size,
		float64 ratio,
		int style,
		bool showIcon) {
	// WHY: glyphs, glows and the logo silhouette are built on first use,
	// which used to drop a frame mid-animation; build them before frame one.
	auto image = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	auto p = QPainter(&image);
	_prewarming = true;
	for (const auto ms : { 1700, 2950 }) {
		paint(p, QSizeF(size), ms, style, showIcon, 0.);
	}
	_prewarming = false;
}

void WhitegramStartupScene::paint(
		QPainter &p,
		QSizeF size,
		crl::time elapsed,
		int style,
		bool showIcon,
		float64 exit) {
	const auto t = elapsed / 1000.0;
	const auto fade = Clamp01(exit);
	const auto screenOpacity = 0.5 + 0.5 * std::cos(M_PI * fade);
	const auto hideScale = 1. + 0.12 * fade * fade * fade;
	p.setOpacity(screenOpacity);
	switch (NormalizeWhitegramStartupStyle(style)) {
	case kWhitegramStartupStyleSunset:
		paintSunset(p, size, t, showIcon, hideScale);
		break;
	case kWhitegramStartupStyleNebula:
		paintNebula(p, size, t, showIcon, hideScale);
		break;
	case kWhitegramStartupStyleOrbits:
		paintOrbits(p, size, t, showIcon, hideScale);
		break;
	case kWhitegramStartupStyleDroplet:
		paintDroplet(p, size, t, showIcon, hideScale);
		break;
	default:
		paintRadiance(p, size, t, showIcon, 1., hideScale);
		break;
	}
	p.setOpacity(1.);
}

// Keeps a steady cadence, but never queues frames up after a slow one.
[[nodiscard]] crl::time NextFrameDelay(
		float64 &nextAt,
		float64 interval,
		float64 now) {
	nextAt = std::max(nextAt + interval, now);
	return std::max(crl::time(0), crl::time(std::lround(nextAt - now)));
}

float64 WhitegramStartupFrameInterval(not_null<QWidget*> widget, int fps) {
	const auto screen = widget->screen();
	const auto refresh = screen ? screen->refreshRate() : 60.;
	const auto limit = (fps > 0) ? float64(fps) : 60.;
	// WHY: frames faster than the monitor are never shown and only steal
	// time from the frames that are, so pace to the refresh rate.
	const auto rate = (refresh >= 24.) ? std::min(limit, refresh) : limit;
	return 1000. / std::max(rate, 1.);
}

#ifdef Q_OS_WIN
// WHY: app startup stalls the main thread for seconds, so the splash draws on its own thread.
class WhitegramStartupAnimation::Presenter final {
public:
	Presenter(HWND owner, int style, bool showIcon, int fps)
	: _owner(owner)
	, _style(style)
	, _showIcon(showIcon)
	, _fps(fps)
	, _scene(std::make_unique<WhitegramStartupScene>()) {
		RegisterWindowClass();
		_thread = std::thread([=] { run(); });
	}

	~Presenter() {
		_stop = true;
		if (_thread.joinable()) {
			_thread.join();
		}
	}

	void setMargins(QMargins margins) {
		auto lock = std::unique_lock(_mutex);
		_margins = margins;
	}

	void skip() {
		_skip = true;
	}

	[[nodiscard]] bool done() const {
		return _done;
	}

	[[nodiscard]] bool failed() const {
		return _failed;
	}

private:
	static constexpr auto kClassName = L"WhitegramStartupSplash";

	static void RegisterWindowClass() {
		static auto registered = false;
		if (registered) {
			return;
		}
		registered = true;
		auto wc = WNDCLASSEXW();
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = kClassName;
		RegisterClassExW(&wc);
	}

	static constexpr auto kHighResolutionTimer = DWORD(0x00000002);

	// Keeps a steady cadence without queueing frames after a slow one.
	static void waitForCadence(
			HANDLE timer,
			const QElapsedTimer &clock,
			float64 &nextAt,
			float64 interval) {
		const auto now = clock.nsecsElapsed() / 1e6;
		nextAt = std::max(nextAt + interval, now);
		const auto wait = nextAt - now;
		if (wait <= 0.05) {
			return;
		}
		if (timer) {
			auto due = LARGE_INTEGER();
			due.QuadPart = -std::max(qint64(1), qint64(wait * 10000.));
			if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
				WaitForSingleObject(timer, 100);
				return;
			}
		}
		Sleep(DWORD(std::max(1., std::round(wait))));
	}

	[[nodiscard]] QRect targetRect() const {
		auto client = RECT();
		if (!IsWindowVisible(_owner)
			|| IsIconic(_owner)
			|| !GetClientRect(_owner, &client)) {
			return QRect();
		}
		auto origin = POINT{ 0, 0 };
		ClientToScreen(_owner, &origin);
		const auto margins = [&] {
			auto lock = std::unique_lock(_mutex);
			return _margins;
		}();
		return QRect(
			origin.x + margins.left(),
			origin.y + margins.top(),
			client.right - margins.left() - margins.right(),
			client.bottom - margins.top() - margins.bottom());
	}

	void run() {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		const auto hwnd = CreateWindowExW(
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			kClassName,
			L"",
			WS_POPUP,
			0,
			0,
			1,
			1,
			_owner,
			nullptr,
			GetModuleHandleW(nullptr),
			nullptr);
		if (!hwnd) {
			LOG(("Whitegram startup animation: no layered window, error %1"
				).arg(GetLastError()));
			_failed = true;
			_done = true;
			return;
		}
		const auto screen = GetDC(nullptr);
		const auto memory = CreateCompatibleDC(screen);
		auto bitmap = HBITMAP(nullptr);
		auto previous = HGDIOBJ(nullptr);
		auto image = QImage();
		auto clock = QElapsedTimer();
		auto shown = false;
		auto hiding = false;
		auto hideAt = qint64(0);
		auto frames = 0;
		auto slow = 0;
		auto paintNs = qint64(0);
		auto maxGapNs = qint64(0);
		auto lastFrameNs = qint64(0);
		auto presentNs = qint64(0);
		auto waitNs = qint64(0);
		const auto minInterval = (_fps > 0) ? (1000. / _fps) : 0.;
		const auto frameInterval = [&] {
			auto info = DWM_TIMING_INFO();
			info.cbSize = sizeof(info);
			auto frequency = LARGE_INTEGER();
			QueryPerformanceFrequency(&frequency);
			const auto refresh = (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &info))
				&& info.qpcRefreshPeriod
				&& frequency.QuadPart)
				? (info.qpcRefreshPeriod * 1000. / frequency.QuadPart)
				: (1000. / 60.);
			return std::max(refresh, minInterval);
		}();
		auto nextFrameAt = 0.;
		const auto timer = CreateWaitableTimerExW(
			nullptr,
			nullptr,
			kHighResolutionTimer,
			TIMER_ALL_ACCESS);
		const auto holdMs = WhitegramStartupScene::HoldDuration(_style);
		const auto exitMs = WhitegramStartupScene::ExitDuration();

		while (!_stop) {
			auto msg = MSG();
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
			const auto rect = targetRect();
			if (rect.width() <= 0 || rect.height() <= 0) {
				Sleep(10);
				continue;
			}
			if (image.size() != rect.size()) {
				if (bitmap) {
					SelectObject(memory, previous);
					DeleteObject(bitmap);
					bitmap = nullptr;
				}
				auto info = BITMAPINFO();
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = rect.width();
				info.bmiHeader.biHeight = -rect.height();
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;
				auto bits = static_cast<void*>(nullptr);
				bitmap = CreateDIBSection(
					screen,
					&info,
					DIB_RGB_COLORS,
					&bits,
					nullptr,
					0);
				if (!bitmap || !bits) {
					_failed = !shown;
					break;
				}
				previous = SelectObject(memory, bitmap);
				image = QImage(
					static_cast<uchar*>(bits),
					rect.width(),
					rect.height(),
					rect.width() * 4,
					QImage::Format_ARGB32_Premultiplied);
			}
			if (!clock.isValid()) {
				_scene->prewarm(rect.size(), 1., _style, _showIcon);
				clock.start();
			} else {
				const auto waitStart = clock.nsecsElapsed();
				waitForCadence(timer, clock, nextFrameAt, frameInterval);
				waitNs += clock.nsecsElapsed() - waitStart;
			}
			const auto frameStart = clock.nsecsElapsed();
			const auto elapsed = frameStart / 1000000;
			if (!hiding && (_skip || elapsed >= holdMs)) {
				hiding = true;
				hideAt = elapsed;
			}
			const auto exit = hiding
				? std::min(1., (elapsed - hideAt) / float64(exitMs))
				: 0.;
			if (exit > 0.) {
				image.fill(Qt::transparent);
			}
			{
				auto p = QPainter(&image);
				_scene->paint(
					p,
					QSizeF(rect.size()),
					elapsed,
					_style,
					_showIcon,
					exit);
			}
			GdiFlush();
			const auto presentStart = clock.nsecsElapsed();
			auto position = POINT{ rect.x(), rect.y() };
			auto size = SIZE{ rect.width(), rect.height() };
			auto source = POINT{ 0, 0 };
			auto blend = BLENDFUNCTION{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			UpdateLayeredWindow(
				hwnd,
				screen,
				&position,
				&size,
				memory,
				&source,
				0,
				&blend,
				ULW_ALPHA);
			if (!shown) {
				shown = true;
				SetWindowPos(
					hwnd,
					nullptr,
					0,
					0,
					0,
					0,
					(SWP_NOMOVE
						| SWP_NOSIZE
						| SWP_NOZORDER
						| SWP_NOACTIVATE
						| SWP_SHOWWINDOW));
			}
			++frames;
			presentNs += clock.nsecsElapsed() - presentStart;
			paintNs += presentStart - frameStart;
			if (lastFrameNs) {
				const auto gap = frameStart - lastFrameNs;
				maxGapNs = std::max(maxGapNs, gap);
				if (gap > 12000000) {
					++slow;
				}
			}
			lastFrameNs = frameStart;
			if (hiding && exit >= 1.) {
				break;
			}

		}
		const auto total = clock.isValid() ? clock.elapsed() : 0;
		LOG(("Whitegram startup animation: %1 frames in %2 ms (%3 fps), "
			"paint avg %4 ms, max gap %5 ms, gaps over 12 ms %6, "
			"size %7x%8, own thread, present %9 ms, wait %10 ms"
			).arg(frames
			).arg(total
			).arg(total ? (frames * 1000. / total) : 0., 0, 'f', 1
			).arg(paintNs / 1e6 / std::max(frames, 1), 0, 'f', 2
			).arg(maxGapNs / 1e6, 0, 'f', 1
			).arg(slow
			).arg(image.width()
			).arg(image.height()
			).arg(presentNs / 1e6 / std::max(frames, 1), 0, 'f', 2
			).arg(waitNs / 1e6 / std::max(frames, 1), 0, 'f', 2));
		image = QImage();
		ShowWindow(hwnd, SW_HIDE);
		DestroyWindow(hwnd);
		if (bitmap) {
			SelectObject(memory, previous);
			DeleteObject(bitmap);
		}
		DeleteDC(memory);
		ReleaseDC(nullptr, screen);
		if (timer) {
			CloseHandle(timer);
		}
		_done = true;
	}

	const HWND _owner = nullptr;
	const int _style = 0;
	const bool _showIcon = true;
	const int _fps = 0;
	const std::unique_ptr<WhitegramStartupScene> _scene;
	mutable std::mutex _mutex;
	QMargins _margins;
	std::atomic<bool> _stop = false;
	std::atomic<bool> _skip = false;
	std::atomic<bool> _done = false;
	std::atomic<bool> _failed = false;
	std::thread _thread;

};
#else // Q_OS_WIN
class WhitegramStartupAnimation::Presenter final {
public:
	void setMargins(QMargins) {
	}
	void skip() {
	}
	[[nodiscard]] bool done() const {
		return true;
	}
	[[nodiscard]] bool failed() const {
		return true;
	}
};
#endif // Q_OS_WIN

WhitegramStartupAnimation::WhitegramStartupAnimation(QWidget *parent)
: RpWidget(parent)
, _style(NormalizeWhitegramStartupStyle(
	AyuSettings::getInstance().startupAnimationType()))
, _showIcon(AyuSettings::getInstance().startupAnimationShowIcon())
, _hideTimer([=] { startHiding(); })
, _updateTimer([=] { update(); })
, _pollTimer([=] { pollPresenter(); }) {
	setFocusPolicy(Qt::StrongFocus);
	setCursor(style::cur_pointer);
#ifdef Q_OS_WIN
	if (const auto owner = HWND(window()->winId())) {
		_presenter = std::make_unique<Presenter>(
			owner,
			_style,
			_showIcon,
			AyuSettings::getInstance().startupAnimationFps());
		updatePresenterGeometry();
		_pollTimer.callEach(30);
	}
#endif // Q_OS_WIN
	if (!_presenter) {
		startOwnPainting();
	}
	show();
	raise();
	setFocus();
}

WhitegramStartupAnimation::~WhitegramStartupAnimation() = default;

void WhitegramStartupAnimation::startOwnPainting() {
	// WHY: otherwise Qt repaints the whole window under the splash every frame.
	setAttribute(Qt::WA_OpaquePaintEvent);
	update();
}

void WhitegramStartupAnimation::pollPresenter() {
	if (!_presenter || !_presenter->done()) {
		return;
	}
	const auto failed = _presenter->failed();
	_presenter = nullptr;
	_pollTimer.cancel();
	if (failed) {
		startOwnPainting();
	} else {
		hide();
		deleteLater();
	}
}

void WhitegramStartupAnimation::updatePresenterGeometry() {
	if (!_presenter) {
		return;
	}
	const auto ratio = devicePixelRatioF();
	const auto topLeft = mapTo(window(), QPoint());
	const auto outer = window()->size();
	_presenter->setMargins(QMargins(
		int(std::round(topLeft.x() * ratio)),
		int(std::round(topLeft.y() * ratio)),
		int(std::round((outer.width() - topLeft.x() - width()) * ratio)),
		int(std::round((outer.height() - topLeft.y() - height()) * ratio))));
}

void WhitegramStartupAnimation::resizeEvent(QResizeEvent *e) {
	updatePresenterGeometry();
}

void WhitegramStartupAnimation::moveEvent(QMoveEvent *e) {
	updatePresenterGeometry();
}

void WhitegramStartupAnimation::scheduleNextFrame() {
	if (!_frameInterval) {
		_frameInterval = WhitegramStartupFrameInterval(
			this,
			AyuSettings::getInstance().startupAnimationFps());
	}
	_updateTimer.callOnce(NextFrameDelay(
		_nextFrameAt,
		_frameInterval,
		float64(crl::now() - _startTime)));
}

void WhitegramStartupAnimation::startHiding() {
	if (_hiding) {
		return;
	}
	_hiding = true;
	_hideTimer.cancel();
	if (const auto parent = parentWidget()) {
		// WHY: the fade needs the app behind it; a snapshot keeps the splash
		// opaque so the window below is not repainted on every frame.
		hide();
		_underlay = GrabWidgetToImage(parent, rect(), QColor(0, 0, 0)
		).convertToFormat(QImage::Format_RGB32);
		show();
		raise();
	}
	_exitAnimation.start(
		[=] { update(); },
		0.,
		1.,
		WhitegramStartupScene::ExitDuration());
}

void WhitegramStartupAnimation::mousePressEvent(QMouseEvent *e) {
	if (_presenter) {
		_presenter->skip();
	} else {
		startHiding();
	}
}

void WhitegramStartupAnimation::keyPressEvent(QKeyEvent *e) {
	if (_presenter) {
		_presenter->skip();
	} else {
		startHiding();
	}
}

void WhitegramStartupAnimation::paintEvent(QPaintEvent *e) {
	if (_presenter) {
		return;
	}
	auto timer = QElapsedTimer();
	timer.start();
	auto p = QPainter(this);
	if (!_startTime) {
		_scene.prewarm(size(), devicePixelRatioF(), _style, _showIcon);
		timer.restart();
		// WHY: the window shows seconds after this widget is created, so
		// the timeline starts with the first frame the user can see.
		_startTime = crl::now();
		_hideTimer.callOnce(WhitegramStartupScene::HoldDuration(_style));
	}
	if (!_underlay.isNull()) {
		p.drawImage(rect(), _underlay);
	}
	const auto exit = _exitAnimation.value(_hiding ? 1. : 0.);
	_scene.paint(
		p,
		QSizeF(size()),
		crl::now() - _startTime,
		_style,
		_showIcon,
		exit);
	p.end();

	const auto ns = timer.nsecsElapsed();
	++_frames;
	_paintNs += ns;
	if (ns > _maxPaintNs) {
		_maxPaintNs = ns;
		_maxPaintAt = crl::now() - _startTime;
	}
	if (ns > qint64(_frameInterval * 1e6)) {
		++_slowFrames;
	}
	if (_hiding && !_exitAnimation.animating()) {
		const auto total = crl::now() - _startTime;
		LOG(("Whitegram startup animation: %1 frames in %2 ms (%3 fps), "
			"paint avg %4 ms, max %5 ms at %9 ms, slow %10, "
			"size %6x%7, interval %8 ms"
			).arg(_frames
			).arg(total
			).arg(total ? (_frames * 1000. / total) : 0., 0, 'f', 1
			).arg(_paintNs / 1e6 / std::max(_frames, 1), 0, 'f', 2
			).arg(_maxPaintNs / 1e6, 0, 'f', 2
			).arg(width()
			).arg(height()
			).arg(_frameInterval, 0, 'f', 2
			).arg(_maxPaintAt
			).arg(_slowFrames));
		_updateTimer.cancel();
		hide();
		deleteLater();
	} else if (!_hiding) {
		scheduleNextFrame();
	}
}

WhitegramStartupPreview::WhitegramStartupPreview(QWidget *parent)
: RpWidget(parent)
, _startTime(crl::now())
, _updateTimer([=] {
	if (isVisible()) {
		update();
	}
}) {
	setCursor(style::cur_pointer);
}

void WhitegramStartupPreview::restart() {
	_startTime = crl::now();
	_nextFrameAt = 0.;
	update();
}

int WhitegramStartupPreview::resizeGetHeight(int newWidth) {
	return newWidth * 9 / 16;
}

void WhitegramStartupPreview::mousePressEvent(QMouseEvent *e) {
	restart();
}

void WhitegramStartupPreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);
	const auto &settings = AyuSettings::getInstance();
	const auto style = NormalizeWhitegramStartupStyle(
		settings.startupAnimationType());
	const auto hold = WhitegramStartupScene::HoldDuration(style);
	const auto exitMs = WhitegramStartupScene::ExitDuration();
	const auto period = hold + exitMs + kPreviewPauseMs;
	const auto elapsed = (crl::now() - _startTime) % period;
	const auto exit = (elapsed > hold)
		? std::min(1., (elapsed - hold) / float64(exitMs))
		: 0.;

	const auto radius = height() * 0.06;
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(rect()), radius, radius);
	p.setClipPath(clip);
	p.fillRect(rect(), QColor(4, 4, 6));

	const auto scale = width() / kPreviewVirtualWidth;
	p.save();
	p.scale(scale, scale);
	_scene.paint(
		p,
		QSizeF(kPreviewVirtualWidth, height() / scale),
		elapsed,
		style,
		settings.startupAnimationShowIcon(),
		exit);
	p.restore();

	if (!_frameInterval) {
		_frameInterval = WhitegramStartupFrameInterval(this, kPreviewFps);
	}
	_updateTimer.callOnce(NextFrameDelay(
		_nextFrameAt,
		_frameInterval,
		float64(crl::now() - _startTime)));
}

} // namespace Ui
