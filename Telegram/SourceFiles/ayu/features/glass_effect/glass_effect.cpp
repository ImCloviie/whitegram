// This file is part of Whitegram Desktop, based on AyuGram for Desktop.
// Copyright @Radolyn, 2026
#include "ayu/features/glass_effect/glass_effect.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "base/algorithm.h"
#include "base/timer.h"
#include "core/application.h"
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/box_content.h"
#include "ui/layers/layer_widget.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_palette.h"
#include "ui/ui_utility.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "styles/palette.h"
#include "styles/style_layers.h"

#include <QtCore/QEvent>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QPointer>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#ifdef Q_OS_WIN
#include "platform/win/windows_dlls.h"
#endif // Q_OS_WIN

namespace AyuFeatures::GlassEffect {
namespace {

constexpr auto kCrossfadeDuration = crl::time(480);
constexpr auto kSliderApplyDelay = crl::time(30);
constexpr auto kMinAlpha = 3;
constexpr auto kLayerTintAlpha = 70;
constexpr auto kBlurProperty = "WhitegramGlassBlur";
constexpr auto kBackdropName = "WhitegramLayerBackdrop";

enum class Kind {
	Base,  // window, chat list, title, compose area: the slider value
	Inner, // rows drawn over an already filled panel: a light extra layer
	Hover, // hover and active rows: a bit denser than the base
	Layer, // boxes, side menu, profiles: glass over a strong blur
	Panel, // emoji panel: almost opaque over the chat, glass as a column
};

struct Entry {
	const char *name = nullptr;
	const style::color *color = nullptr;
	Kind kind = Kind::Base;
};

[[nodiscard]] const std::vector<Entry> &Entries() {
	static const auto result = std::vector<Entry>{
		{ "windowBg", &st::windowBg, Kind::Base },
		{ "dialogsBg", &st::dialogsBg, Kind::Base },
		{ "topBarBg", &st::topBarBg, Kind::Base },
		{ "titleBg", &st::titleBg, Kind::Base },
		{ "titleBgActive", &st::titleBgActive, Kind::Base },
		{ "sideBarBg", &st::sideBarBg, Kind::Base },
		{ "historyComposeAreaBg", &st::historyComposeAreaBg, Kind::Base },
		{ "historyPinnedBg", &st::historyPinnedBg, Kind::Base },
		{ "historyReplyBg", &st::historyReplyBg, Kind::Base },
		{ "mediaPlayerBg", &st::mediaPlayerBg, Kind::Base },
		{ "historyComposeButtonBg", &st::historyComposeButtonBg, Kind::Base },
		{ "titleButtonBg", &st::titleButtonBg, Kind::Base },
		{ "titleButtonBgActive", &st::titleButtonBgActive, Kind::Base },
		{ "filterInputInactiveBg", &st::filterInputInactiveBg, Kind::Hover },
		{ "filterInputActiveBg", &st::filterInputActiveBg, Kind::Hover },
		{
			"historyComposeButtonBgOver",
			&st::historyComposeButtonBgOver,
			Kind::Hover,
		},
		{ "contactsBg", &st::contactsBg, Kind::Inner },
		{ "contactsBgOver", &st::contactsBgOver, Kind::Hover },
		{ "searchedBarBg", &st::searchedBarBg, Kind::Hover },
		{ "boxDividerBg", &st::boxDividerBg, Kind::Hover },
		{ "windowBgOver", &st::windowBgOver, Kind::Hover },
		{ "dialogsBgOver", &st::dialogsBgOver, Kind::Hover },
		{ "sideBarBgActive", &st::sideBarBgActive, Kind::Hover },
		{ "boxBg", &st::boxBg, Kind::Layer },
		{ "mainMenuBg", &st::mainMenuBg, Kind::Layer },
		{ "emojiPanBg", &st::emojiPanBg, Kind::Panel },
		{ "emojiPanCategories", &st::emojiPanCategories, Kind::Panel },
	};
	return result;
}

[[nodiscard]] bool IsMainWindow(QObject *object) {
	if (!object || !object->isWidgetType()) {
		return false;
	}
	const auto widget = static_cast<QWidget*>(object);
	return widget->isWindow()
		&& (dynamic_cast<Window::MainWindow*>(widget) != nullptr);
}

[[nodiscard]] bool IsTranslucent(not_null<QWidget*> window) {
	return window->testAttribute(Qt::WA_TranslucentBackground);
}

[[nodiscard]] float64 KindAlpha(Kind kind, float64 base, bool sidePanel) {
	switch (kind) {
	case Kind::Base: return base;
	case Kind::Inner: return base * 0.35;
	case Kind::Hover: return base + (1. - base) * 0.35;
	case Kind::Layer: return base + (1. - base) * 0.4;
	case Kind::Panel: return sidePanel ? base : std::max(base, 0.97);
	}
	return base;
}

void SetBlurBehind(
		not_null<QWidget*> window,
		bool enabled,
		QColor tint,
		int strength) {
	if (!window->internalWinId()) {
		return;
	}
#ifdef Q_OS_WIN
	using namespace Platform;
	const auto handle = reinterpret_cast<HWND>(window->winId());

	auto behind = DWM_BLURBEHIND();
	behind.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
	behind.fEnable = TRUE;
	behind.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
	DwmEnableBlurBehindWindow(handle, &behind);
	if (behind.hRgnBlur) {
		DeleteObject(behind.hRgnBlur);
	}

	if (!Dlls::SetWindowCompositionAttribute) {
		return;
	}
	// Acrylic stutters while dragging on Windows 10, plain blur does not.
	static const auto kAcrylicSupported
		= (QOperatingSystemVersion::current().microVersion() >= 22000);
	// WHY: plain blur on Windows 11 lags behind a dragged window and may not
	// render, so it stays acrylic there; DWM has no radius, 0% turns it off.
	if (strength <= 0) {
		enabled = false;
	}
	const auto acrylic = kAcrylicSupported;
	const auto alpha = std::clamp(0x10 + strength * 0xD6 / 100, 0x10, 0xE6);
	const auto gradient = DWORD((alpha << 24)
		| (tint.blue() << 16)
		| (tint.green() << 8)
		| tint.red());
	const auto key = enabled ? qint64(gradient) : qint64(-1);
	if (window->property(kBlurProperty).toLongLong() == key) {
		return;
	}
	window->setProperty(kBlurProperty, key);

	enum AccentState : int {
		AccentDisabled = 0,
		AccentBlurBehind = 3,
		AccentAcrylicBlurBehind = 4,
	};
	struct AccentPolicy {
		int state = 0;
		int flags = 0;
		DWORD gradientColor = 0;
		int animationId = 0;
	};
	auto policy = AccentPolicy();
	if (enabled) {
		policy.state = acrylic ? AccentAcrylicBlurBehind : AccentBlurBehind;
		policy.flags = 2;
		policy.gradientColor = gradient;
	}
	auto data = Dlls::WINDOWCOMPOSITIONATTRIBDATA{
		Dlls::WINDOWCOMPOSITIONATTRIB::WCA_ACCENT_POLICY,
		&policy,
		sizeof(policy),
	};
	Dlls::SetWindowCompositionAttribute(handle, &data);
#endif // Q_OS_WIN
}

// Fades the pre-switch snapshot out over the restyled window.
class Crossfade final : public Ui::RpWidget {
public:
	Crossfade(QWidget *window, QPixmap snapshot)
	: RpWidget(window)
	, _snapshot(std::move(snapshot)) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setGeometry(window->rect());
		show();
		raise();
		_animation.start(
			[=] {
				update();
				if (!_animation.animating()) {
					deleteLater();
				}
			},
			1.,
			0.,
			kCrossfadeDuration,
			anim::easeOutCubic);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setOpacity(_animation.value(0.));
		p.drawPixmap(0, 0, _snapshot);
	}

private:
	QPixmap _snapshot;
	Ui::Animations::Simple _animation;

};

class Controller final : public QObject {
public:
	Controller() {
		qApp->installEventFilter(this);
		style::PaletteChanged(
		) | rpl::filter([=] {
			return !_applyingPalette;
		}) | rpl::on_next([=] {
			// A theme or night mode switch replaced the palette.
			_captured = false;
			applyNow();
		}, _lifetime);
	}

	[[nodiscard]] float64 backgroundOpacity() const {
		return _applied ? _alpha : 1.;
	}

	[[nodiscard]] bool layerBlurActive() const {
		return _applied && AyuSettings::getInstance().glassBlur();
	}

	void sync(bool toggle) {
		const auto enabled = AyuSettings::getInstance().glassEffect();
		if (enabled && !allWindowsTranslucent()) {
			// Opaque native windows can't become per-pixel translucent.
			if (toggle) {
				promptRestart();
			}
			return;
		}
		if (!toggle) {
			_sliderTimer.callOnce(kSliderApplyDelay);
			return;
		}
		auto snapshots = std::vector<std::pair<QPointer<QWidget>, QPixmap>>();
		forEachWindow([&](not_null<QWidget*> window) {
			if (window->isVisible()) {
				snapshots.emplace_back(
					QPointer<QWidget>(window.get()),
					Ui::GrabWidget(window));
			}
		});
		applyNow();
		for (auto &[window, snapshot] : snapshots) {
			if (window) {
				Ui::CreateChild<Crossfade>(
					window.data(),
					std::move(snapshot));
			}
		}
	}

	void applyNow() {
		applyPalette();
		updateWindows();
	}

	void registerSidePanel(not_null<QWidget*> panel) {
		_sidePanels.emplace_back(panel.get());
		QObject::connect(panel, &QObject::destroyed, this, [=] {
			_sidePanelTimer.callOnce(0);
		});
		_sidePanelTimer.callOnce(0);
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		const auto type = event->type();
		if (type == QEvent::Paint && _applied && object->isWidgetType()) {
			if (IsMainWindow(object)) {
				// Layered windows let clicks through fully transparent pixels.
				const auto window = static_cast<QWidget*>(object);
				auto p = QPainter(window);
				p.fillRect(
					static_cast<QPaintEvent*>(event)->rect(),
					QColor(0, 0, 0, 1));
			} else if (const auto layer = dynamic_cast<Ui::LayerWidget*>(
					static_cast<QWidget*>(object))) {
				// Every layer (box, profile, side menu) gets the same glass.
				auto p = QPainter(layer);
				p.setClipRegion(static_cast<QPaintEvent*>(event)->region());
				const auto radius = (layer->x() <= 0) ? 0 : st::boxRadius;
				PaintLayerBackdrop(p, layer, radius);
			}
			return false;
		}
		if (type == QEvent::Show
			&& _applied
			&& object->isWidgetType()
			&& dynamic_cast<Ui::BoxContent*>(static_cast<QWidget*>(object))) {
			// An opaque box would hide the layer glass drawn under it.
			static_cast<QWidget*>(object)->setAttribute(
				Qt::WA_OpaquePaintEvent,
				false);
		}
		if ((type == QEvent::Show || type == QEvent::Hide)
			&& isSidePanel(object)) {
			_sidePanelTimer.callOnce(0);
		}
		if ((type == QEvent::Show || type == QEvent::WinIdChange)
			&& IsMainWindow(object)) {
			const auto window = static_cast<QWidget*>(object);
			if (type == QEvent::WinIdChange) {
				// The new native window has no accent policy yet.
				window->setProperty(kBlurProperty, QVariant());
			} else if (AyuSettings::getInstance().glassEffect()
				&& !paletteHoldsWritten()) {
				// The startup theme may load without a palette notification.
				_captured = false;
				applyPalette();
			}
			applyTo(window);
		}
		return false;
	}

private:
	template <typename Callback>
	void forEachWindow(Callback &&callback) {
		for (const auto widget : QApplication::topLevelWidgets()) {
			if (IsMainWindow(widget)) {
				callback(not_null<QWidget*>(widget));
			}
		}
	}

	[[nodiscard]] bool allWindowsTranslucent() {
		auto result = true;
		forEachWindow([&](not_null<QWidget*> window) {
			if (!IsTranslucent(window)) {
				result = false;
			}
		});
		return result;
	}

	void promptRestart() {
		const auto window = Core::App().activePrimaryWindow();
		const auto controller = window
			? window->sessionController()
			: nullptr;
		if (controller) {
			Settings::ShowRestartPrompt(controller);
		}
	}

	void applyTo(not_null<QWidget*> window) {
		if (!IsTranslucent(window)) {
			return;
		}
		const auto &settings = AyuSettings::getInstance();
		const auto tint = _base.empty() ? st::windowBg->c : _base.front();
		SetBlurBehind(
			window,
			settings.glassEffect() && settings.glassBlur(),
			tint,
			settings.glassBlurStrength());
	}

	void updateWindows() {
		forEachWindow([&](not_null<QWidget*> window) {
			applyTo(window);
			window->update();
		});
	}

	void applyPalette() {
		const auto &settings = AyuSettings::getInstance();
		const auto enabled = settings.glassEffect()
			&& allWindowsTranslucent();
		const auto &entries = Entries();
		const auto count = int(entries.size());
		if (!_captured) {
			_base.resize(count);
			_written.resize(count);
			for (auto i = 0; i != count; ++i) {
				const auto now = (*entries[i].color)->c;
				// WHY: a palette may still hold our own translucent value;
				// taking it as the base would multiply the alpha again.
				if (!_applied || now.rgba() != _written[i].rgba()) {
					_base[i] = now;
					if (entries[i].kind == Kind::Base) {
						// Heals a base that was saved already translucent.
						_base[i].setAlpha(255);
					}
				}
			}
			_captured = true;
		}
		if (!enabled && !_applied) {
			return;
		}
		_alpha = settings.glassOpacity() / 100.;
		_applyingPalette = true;
		for (auto i = 0; i != count; ++i) {
			auto color = _base[i];
			if (enabled && color.alpha() > 0) {
				// At least kMinAlpha: Windows passes clicks through alpha 0.
				const auto scaled = int(base::SafeRound(color.alpha()
					* KindAlpha(entries[i].kind, _alpha, _sidePanelShown)));
				color = QColor(
					color.red(),
					color.green(),
					color.blue(),
					std::clamp(scaled, kMinAlpha, 255));
			}
			_written[i] = color;
			style::main_palette::setColor(
				QLatin1String(entries[i].name),
				uchar(color.red()),
				uchar(color.green()),
				uchar(color.blue()),
				uchar(color.alpha()));
		}
		_applied = enabled;
		style::NotifyPaletteChanged();
		_applyingPalette = false;
	}

	[[nodiscard]] bool isSidePanel(QObject *object) const {
		for (const auto &panel : _sidePanels) {
			if (panel.data() == object) {
				return true;
			}
		}
		return false;
	}

	void refreshSidePanels() {
		_sidePanels.erase(
			ranges::remove_if(_sidePanels, [](const auto &panel) {
				return !panel;
			}),
			end(_sidePanels));
		const auto shown = ranges::any_of(_sidePanels, [](const auto &panel) {
			return panel && panel->isVisible();
		});
		if (_sidePanelShown == shown) {
			return;
		}
		// A sticker column lies on the window glass like the chat list.
		_sidePanelShown = shown;
		if (_applied) {
			applyPalette();
		}
	}

	[[nodiscard]] bool paletteHoldsWritten() const {
		const auto &entries = Entries();
		if (!_applied || _written.size() != entries.size()) {
			return false;
		}
		for (auto i = 0; i != int(entries.size()); ++i) {
			if ((*entries[i].color)->c.rgba() != _written[i].rgba()) {
				return false;
			}
		}
		return true;
	}

	std::vector<QColor> _base;
	std::vector<QColor> _written;
	float64 _alpha = 1.;
	bool _captured = false;
	bool _applied = false;
	bool _applyingPalette = false;
	base::Timer _sliderTimer = base::Timer([=] { applyNow(); });
	std::vector<QPointer<QWidget>> _sidePanels;
	bool _sidePanelShown = false;
	base::Timer _sidePanelTimer = base::Timer([=] { refreshSidePanels(); });
	rpl::lifetime _lifetime;

};

// Keeps a downscaled blurred snapshot of the window content under a layer.
class LayerBackdrop final : public QObject {
public:
	explicit LayerBackdrop(not_null<QWidget*> layer)
	: QObject(layer.get())
	, _layer(layer) {
		setObjectName(QLatin1String(kBackdropName));
		layer->installEventFilter(this);
		schedule();
	}

	void paint(QPainter &p, int radius) {
		const auto host = this->host();
		if (_blurred.isNull() || !host) {
			return;
		}
		const auto origin = _layer->mapTo(host, QPoint());
		p.save();
		if (radius > 0) {
			auto path = QPainterPath();
			path.addRoundedRect(QRectF(_layer->rect()), radius, radius);
			p.setClipPath(path, Qt::IntersectClip);
		}
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(QRect(-origin, _hostSize), _blurred);
		// Translucent rows of the layer lie on this, so text stays readable.
		auto tint = st::boxBg->c;
		tint.setAlpha(kLayerTintAlpha);
		p.fillRect(_layer->rect(), tint);
		p.restore();
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		const auto type = event->type();
		if (type == QEvent::Show || type == QEvent::Resize) {
			schedule();
		}
		return false;
	}

private:
	static constexpr auto kDownscale = 4;
	static constexpr auto kMinBlurRadius = 9;
	static constexpr auto kGrabDelay = crl::time(150);

	[[nodiscard]] QWidget *host() const {
		const auto stack = _layer->parentWidget();
		return stack ? stack->parentWidget() : nullptr;
	}

	void schedule() {
		// A layer resizes every frame while it animates: grab once it rests.
		_grabTimer.callOnce(kGrabDelay);
	}

	void grab() {
		const auto stack = _layer->parentWidget();
		const auto host = this->host();
		if (!host || host->size().isEmpty()) {
			return;
		}
		const auto ratio = style::DevicePixelRatio();
		auto full = QImage(
			host->size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		full.setDevicePixelRatio(ratio);
		full.fill(Qt::transparent);
		{
			auto p = QPainter(&full);
			for (const auto child : host->children()) {
				const auto widget = qobject_cast<QWidget*>(child);
				if (!widget
					|| widget == stack
					|| widget->isWindow()
					|| widget->isHidden()) {
					continue;
				}
				widget->render(
					&p,
					widget->pos(),
					QRegion(),
					QWidget::DrawChildren);
			}
		}
		auto small = full.scaled(
			full.size() / (ratio * kDownscale),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		small.setDevicePixelRatio(1.);
		const auto strength = AyuSettings::getInstance().glassBlurStrength();
		_blurred = Images::BlurLargeImage(
			std::move(small),
			kMinBlurRadius + strength / 6);
		_hostSize = host->size();
	}

	const not_null<QWidget*> _layer;
	QImage _blurred;
	QSize _hostSize;
	base::Timer _grabTimer = base::Timer([=] {
		grab();
		_layer->update();
	});

};

Controller *Instance() {
	static auto instance = (Controller*)nullptr;
	if (!instance && qApp) {
		instance = new Controller();
		instance->setParent(qApp);
	}
	return instance;
}

} // namespace

bool CreateTranslucent() {
	return AyuSettings::getInstance().glassEffect();
}

double BackgroundOpacity() {
	const auto instance = Instance();
	return instance ? instance->backgroundOpacity() : 1.;
}

void PaintLayerBackdrop(QPainter &p, QWidget *layer, int radius) {
	const auto instance = Instance();
	if (!layer || !instance || instance->backgroundOpacity() >= 1.) {
		return;
	} else if (!instance->layerBlurActive()) {
		auto tint = st::boxBg->c;
		tint.setAlpha(kLayerTintAlpha);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(tint);
		p.drawRoundedRect(layer->rect(), radius, radius);
		return;
	}
	const auto found = layer->findChild<QObject*>(
		QLatin1String(kBackdropName),
		Qt::FindDirectChildrenOnly);
	const auto backdrop = found
		? static_cast<LayerBackdrop*>(found)
		: new LayerBackdrop(layer);
	backdrop->paint(p, radius);
}

void RegisterSidePanel(QWidget *panel) {
	if (!panel) {
		return;
	} else if (const auto instance = Instance()) {
		instance->registerSidePanel(panel);
	}
}

void init() {
	if (CreateTranslucent()) {
		// WHY: Qt scrolls by shifting already painted pixels, which keeps
		// stale translucent rows and garbage; Qt reads this once on first use.
		qputenv("QT_NO_FAST_SCROLL", "1");
		qputenv("QT_NO_FAST_MOVE", "1");
	}
	if (const auto instance = Instance()) {
		instance->applyNow();
	}
}

void sync(bool toggle) {
	if (const auto instance = Instance()) {
		instance->sync(toggle);
	}
}

} // namespace AyuFeatures::GlassEffect
