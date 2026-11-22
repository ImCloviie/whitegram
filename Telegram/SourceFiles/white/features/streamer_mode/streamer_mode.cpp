// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "white/features/streamer_mode/streamer_mode.h"

#include "white/white_settings.h"
#include "white/features/streamer_mode/platform/platform_streamer_mode.h"
#include "core/application.h"
#include "window/window_controller.h"

#include <QtCore/QEvent>
#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace WhiteFeatures::StreamerMode {

namespace {

constexpr auto kHiddenProperty = "AyuStreamerModeHidden";
auto _viewOnceActiveCount = 0;

[[nodiscard]] bool IsWindowCaptureExcluded(not_null<QWidget*> widget) {
	return widget->property(kHiddenProperty).toBool();
}

void SetWindowCaptureExcluded(QWidget *widget, bool excluded) {
	const auto window = widget->window();
	Platform::SetWindowCaptureExcluded(window, excluded);
	window->setProperty(kHiddenProperty, excluded);
}

// WHY: the exclusion lives on the HWND, which Qt may recreate (OpenGL, DPI),
// and popups appear after the toggle, so every shown or re-created
// top-level window gets it again.
class CaptureGuard final : public QObject {
protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		const auto type = event->type();
		if (type != QEvent::Show && type != QEvent::WinIdChange) {
			return false;
		}
		if (!object->isWidgetType() || !isStreamerModeActive()) {
			return false;
		}
		const auto widget = static_cast<QWidget*>(object);
		if (widget->isWindow() && widget->internalWinId()) {
			SetWindowCaptureExcluded(widget, true);
		}
		return false;
	}

};

void EnsureCaptureGuard() {
	static auto guard = (CaptureGuard*)nullptr;
	if (!guard && qApp) {
		guard = new CaptureGuard();
		guard->setParent(qApp);
		qApp->installEventFilter(guard);
	}
}

} // namespace

bool isStreamerModeActive() {
	const auto &settings = WhiteSettings::getInstance();
	return settings.streamerMode()
		|| (settings.hideTelegramOnViewOnce() && (_viewOnceActiveCount > 0));
}

void apply(bool enabled) {
	EnsureCaptureGuard();
	Core::App().enumerateWindows([=](not_null<Window::Controller*> window) {
		SetWindowCaptureExcluded(window->widget(), enabled);
	});
	for (const auto widget : QApplication::topLevelWidgets()) {
		if (!widget->windowHandle()) {
			continue;
		}
		if (enabled) {
			if (widget->isVisible()
				&& !IsWindowCaptureExcluded(widget)) {
				SetWindowCaptureExcluded(widget, true);
			}
		} else if (IsWindowCaptureExcluded(widget)) {
			SetWindowCaptureExcluded(widget, false);
		}
	}
}

void sync() {
	apply(isStreamerModeActive());
}

void hideWidgetWindow(QWidget *widget) {
	EnsureCaptureGuard();
	SetWindowCaptureExcluded(widget, true);
}

void showWidgetWindow(QWidget *widget) {
	SetWindowCaptureExcluded(widget, false);
}

void pushViewOnceActive() {
	++_viewOnceActiveCount;
	sync();
}

void popViewOnceActive() {
	if (_viewOnceActiveCount > 0) {
		--_viewOnceActiveCount;
		sync();
	}
}

ScopedViewOnceGuard::ScopedViewOnceGuard() {
	pushViewOnceActive();
	_active = true;
}

ScopedViewOnceGuard::~ScopedViewOnceGuard() {
	release();
}

ScopedViewOnceGuard::ScopedViewOnceGuard(ScopedViewOnceGuard &&other) noexcept
: _active(other._active) {
	other._active = false;
}

ScopedViewOnceGuard &ScopedViewOnceGuard::operator=(ScopedViewOnceGuard &&other) noexcept {
	if (this != &other) {
		release();
		_active = other._active;
		other._active = false;
	}
	return *this;
}

void ScopedViewOnceGuard::release() {
	if (_active) {
		_active = false;
		popViewOnceActive();
	}
}

ScopedViewOnceGuard RequireViewOnceGuard() {
	return ScopedViewOnceGuard();
}

} // namespace AyuFeatures::StreamerMode
