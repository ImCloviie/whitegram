// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "white/features/streamer_mode/platform/win/streamer_mode_win.h"

#include "lang/lang_instance.h"
#include "ui/toast/toast.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>

#include <windows.h>

namespace WhiteFeatures::StreamerMode::Platform {
namespace {

constexpr auto kRetryDelay = 500;

[[nodiscard]] bool Exclude(HWND handle, DWORD &excludeError, DWORD &monitorError) {
	if (SetWindowDisplayAffinity(handle, WDA_EXCLUDEFROMCAPTURE)) {
		return true;
	}
	excludeError = GetLastError();
	if (SetWindowDisplayAffinity(handle, WDA_MONITOR)) {
		LOG(("Streamer Mode: exclude failed (%1), window shown black instead"
			).arg(excludeError));
		return true;
	}
	monitorError = GetLastError();
	return false;
}

void LogFailure(HWND handle, DWORD excludeError, DWORD monitorError) {
	auto info = MONITORINFOEXW();
	info.cbSize = sizeof(info);
	const auto monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
	const auto device = GetMonitorInfoW(monitor, &info)
		? QString::fromWCharArray(info.szDevice)
		: QString();
	LOG(("Streamer Mode Error: display affinity failed, exclude %1, "
		"monitor %2, exstyle 0x%3, visible %4, display %5"
		).arg(excludeError
		).arg(monitorError
		).arg(quint64(GetWindowLongPtrW(handle, GWL_EXSTYLE)), 0, 16
		).arg(IsWindowVisible(handle) ? 1 : 0
		).arg(device));
}

void WarnOnce() {
	static auto warned = false;
	if (warned) {
		return;
	}
	warned = true;
	const auto ru = Lang::GetInstance().id().startsWith(u"ru"_q)
		|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
	Ui::Toast::Show(ru
		? QString::fromUtf8(
			"Windows не дала скрыть окно от записи экрана. "
			"Режим стримера на этом компьютере не защищает окно.")
		: QString(
			"Windows refused to hide the window from screen capture. "
			"Streamer mode does not protect the window on this PC."));
}

} // namespace

void SetWindowCaptureExcluded(
		not_null<QWidget*> widget,
		bool excluded) {
	const auto handle = reinterpret_cast<HWND>(widget->winId());
	if (!excluded) {
		SetWindowDisplayAffinity(handle, WDA_NONE);
		return;
	}
	auto excludeError = DWORD();
	auto monitorError = DWORD();
	if (Exclude(handle, excludeError, monitorError)) {
		return;
	}
	LogFailure(handle, excludeError, monitorError);

	// The window may not be ready right after it is shown: retry once.
	const auto weak = QPointer<QWidget>(widget.get());
	QTimer::singleShot(kRetryDelay, widget.get(), [=] {
		if (!weak) {
			return;
		}
		const auto again = reinterpret_cast<HWND>(weak->winId());
		auto e1 = DWORD();
		auto e2 = DWORD();
		if (Exclude(again, e1, e2)) {
			LOG(("Streamer Mode: display affinity applied on retry"));
			return;
		}
		LogFailure(again, e1, e2);
		WarnOnce();
	});
}

} // namespace WhiteFeatures::StreamerMode::Platform
