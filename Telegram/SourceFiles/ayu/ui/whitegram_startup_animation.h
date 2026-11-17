#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

#include <QtGui/QImage>
#include <QtGui/QColor>

namespace Ui {

inline constexpr auto kWhitegramStartupStyleSunset = 3;
inline constexpr auto kWhitegramStartupStyleNebula = 4;
inline constexpr auto kWhitegramStartupStyleRadiance = 5;
inline constexpr auto kWhitegramStartupStyleOrbits = 6;
inline constexpr auto kWhitegramStartupStyleDroplet = 7;

// Styles in the order the settings list shows them.
[[nodiscard]] std::vector<int> WhitegramStartupStyles();
[[nodiscard]] int NormalizeWhitegramStartupStyle(int style);
[[nodiscard]] QString WhitegramStartupStyleName(int style, bool isRu);

// Draws one frame of a startup style, shared by the splash and the preview.
[[nodiscard]] float64 WhitegramStartupFrameInterval(
	not_null<QWidget*> widget,
	int fps);

class WhitegramStartupScene final {
public:
	WhitegramStartupScene();

	void paint(
		QPainter &p,
		QSizeF size,
		crl::time elapsed,
		int style,
		bool showIcon,
		float64 exit);

	void prewarm(QSize size, float64 ratio, int style, bool showIcon);

	[[nodiscard]] static crl::time HoldDuration(int style);
	[[nodiscard]] static crl::time ExitDuration();

private:
	struct Star {
		QPointF position;
		float phase = 0.;
	};
	struct Particle {
		float64 angle = 0.;
		float64 radius = 0.;
		float64 delay = 0.;
		float64 spin = 0.;
		float64 size = 0.;
		float64 hue = 0.;
	};

	[[nodiscard]] const QImage &silhouette(const QColor &tint);
	void composeLogo(
		QPainter &p,
		const QRectF &rect,
		const QColor &tint,
		int strength);
	[[nodiscard]] const QImage &glowSprite(const QColor &color);
	void drawGlow(
		QPainter &p,
		QPointF center,
		float64 radius,
		const QColor &color,
		float64 opacity);
	void drawWordmark(
		QPainter &p,
		QPointF center,
		float64 baseline,
		float64 minDim,
		float64 opacity,
		Fn<void(int index, int count, float64 offset, float64 &alpha, QPointF &shift)> letter);
	void ensureStars(QSizeF size, int style, int count, float64 heightPart);
	void ensureParticles();
	void drawLogo(
		QPainter &p,
		const QRectF &rect,
		const QColor &tint,
		int tintStrength,
		const QColor &bloom,
		float64 bloomStrength,
		float64 opacity,
		float64 shine);
	struct BackdropState;
	void drawBackdrop(QPainter &p, QSizeF size, int style, float64 t);
	void paintSunset(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale);
	void paintNebula(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale);
	void paintOrbits(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale);
	void paintDroplet(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 exitScale);
	void paintRadiance(
		QPainter &p,
		QSizeF size,
		float64 t,
		bool showIcon,
		float64 opacity,
		float64 exitScale);

	QImage _logo;
	std::vector<std::pair<quint32, QImage>> _silhouettes;
	std::vector<Star> _stars;
	QSizeF _starsSize;
	int _starsStyle = -1;
	std::vector<Particle> _particles;
	QImage _shineBuffer;
	QImage _mirrorBuffer;
	std::shared_ptr<BackdropState> _backdropState;
	bool _prewarming = false;
	std::vector<std::pair<quint32, QImage>> _glows;

};

class WhitegramStartupAnimation final : public RpWidget {
public:
	WhitegramStartupAnimation(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void startHiding();
	void scheduleNextFrame();

	WhitegramStartupScene _scene;
	QImage _underlay;
	Ui::Animations::Simple _exitAnimation;
	int _style = 0;
	bool _showIcon = true;
	bool _hiding = false;
	crl::time _startTime = 0;
	float64 _frameInterval = 0.;
	float64 _nextFrameAt = 0.;
	int _frames = 0;
	qint64 _paintNs = 0;
	qint64 _maxPaintNs = 0;
	crl::time _maxPaintAt = 0;
	int _slowFrames = 0;
	base::Timer _hideTimer;
	base::Timer _updateTimer;

};

// Plays the selected style in a loop inside a rounded card.
class WhitegramStartupPreview final : public RpWidget {
public:
	WhitegramStartupPreview(QWidget *parent);

	void restart();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	WhitegramStartupScene _scene;
	crl::time _startTime = 0;
	float64 _frameInterval = 0.;
	float64 _nextFrameAt = 0.;
	base::Timer _updateTimer;

};

} // namespace Ui
