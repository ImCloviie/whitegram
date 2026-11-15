// This file is part of Whitegram Desktop, based on AyuGram for Desktop.
// Copyright @Radolyn, 2026
#include "ayu/features/glass_effect/glass_effect.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "base/timer.h"
#include "core/application.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_palette.h"
#include "ui/ui_utility.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "styles/palette.h"

#include <QtCore/QEvent>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QPointer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#ifdef Q_OS_WIN
#include "platform/win/windows_dlls.h"
#endif // Q_OS_WIN

namespace AyuFeatures::GlassEffect {
namespace {

constexpr auto kCrossfadeDuration = crl::time(480);
constexpr auto kSliderApplyDelay = crl::time(30);
constexpr auto kBlurProperty = "WhitegramGlassBlur";

enum class Kind {
	Base,  // window, chat list, title, compose area: the slider value
	Hover, // hover and active rows: a bit denser than the base
	Layer, // boxes, side menu, emoji panel: stay almost opaque to read
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
		{ "windowBgOver", &st::windowBgOver, Kind::Hover },
		{ "dialogsBgOver", &st::dialogsBgOver, Kind::Hover },
		{ "sideBarBgActive", &st::sideBarBgActive, Kind::Hover },
		{ "boxBg", &st::boxBg, Kind::Layer },
		{ "mainMenuBg", &st::mainMenuBg, Kind::Layer },
		{ "emojiPanBg", &st::emojiPanBg, Kind::Layer },
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

[[nodiscard]] float64 KindAlpha(Kind kind, float64 base) {
	switch (kind) {
	case Kind::Base: return base;
	case Kind::Hover: return base + (1. - base) * 0.35;
	case Kind::Layer: return std::max(base, 0.97);
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

	// WHY: DWM composes the alpha channel of a framed window only with
	// blur-behind enabled; an empty region turns on alpha without the old
	// Aero blur, the real blur comes from the accent policy below.
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
	const auto alpha = std::clamp(strength * 230 / 100, 0x10, 0xE6);
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
	// Acrylic stutters while dragging on Windows 10, plain blur does not.
	static const auto kAcrylic
		= (QOperatingSystemVersion::current().microVersion() >= 22000);
	auto policy = AccentPolicy();
	if (enabled) {
		policy.state = kAcrylic ? AccentAcrylicBlurBehind : AccentBlurBehind;
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

	void sync(bool toggle) {
		const auto enabled = AyuSettings::getInstance().glassEffect();
		if (enabled && !allWindowsTranslucent()) {
			// Opaque native windows can't become per-pixel translucent.
			promptRestart();
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

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		const auto type = event->type();
		if ((type == QEvent::Show || type == QEvent::WinIdChange)
			&& IsMainWindow(object)) {
			const auto window = static_cast<QWidget*>(object);
			if (type == QEvent::WinIdChange) {
				// The new native window has no accent policy yet.
				window->setProperty(kBlurProperty, QVariant());
			} else if (AyuSettings::getInstance().glassEffect()) {
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
				// A palette restored from a snapshot may hold our own value.
				if (!_applied || now != _written[i]) {
					_base[i] = now;
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
			if (enabled) {
				color.setAlphaF(color.alphaF()
					* KindAlpha(entries[i].kind, _alpha));
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

	std::vector<QColor> _base;
	std::vector<QColor> _written;
	float64 _alpha = 1.;
	bool _captured = false;
	bool _applied = false;
	bool _applyingPalette = false;
	base::Timer _sliderTimer = base::Timer([=] { applyNow(); });
	rpl::lifetime _lifetime;

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

void init() {
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
