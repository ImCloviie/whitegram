// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class QWidget;

namespace AyuFeatures::StreamerMode {

void apply(bool enabled);
void sync();
void hideWidgetWindow(QWidget *widget);
void showWidgetWindow(QWidget *widget);

[[nodiscard]] bool isStreamerModeActive();

void pushViewOnceActive();
void popViewOnceActive();

class ScopedViewOnceGuard final {
public:
	ScopedViewOnceGuard();
	~ScopedViewOnceGuard();

	ScopedViewOnceGuard(const ScopedViewOnceGuard &) = delete;
	ScopedViewOnceGuard &operator=(const ScopedViewOnceGuard &) = delete;
	ScopedViewOnceGuard(ScopedViewOnceGuard &&other) noexcept;
	ScopedViewOnceGuard &operator=(ScopedViewOnceGuard &&other) noexcept;

	void release();

private:
	bool _active = false;
};

[[nodiscard]] ScopedViewOnceGuard RequireViewOnceGuard();

} // namespace AyuFeatures::StreamerMode

