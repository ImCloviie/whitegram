// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "ayu/features/streamer_mode/streamer_mode.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/streamer_mode/platform/platform_streamer_mode.h"
#include "core/application.h"
#include "window/window_controller.h"

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace AyuFeatures::StreamerMode {

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

} // namespace

bool isStreamerModeActive() {
	return AyuSettings::getInstance().streamerMode()
		|| (_viewOnceActiveCount > 0);
}

void apply(bool enabled) {
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
