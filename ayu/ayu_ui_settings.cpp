// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu_ui_settings.h"

#include <utility>

namespace AyuUiSettings {

QString monoFont;
double wideMultiplier = 1.0;
bool materialSwitches;
int avatarCorners = kMaxAvatarCorners;
TypingAnimation typingAnimation = TypingAnimation::Off;

void setMonoFont(QString newFont) {
	monoFont = std::move(newFont);
}

QString getMonoFont() {
	return monoFont;
}

void setWideMultiplier(double val) {
	wideMultiplier = val;
}

bool isWideMultiplied() {
	return abs(wideMultiplier - 1.0) > 0.01;
}

int getWideMultiplied(int width, double mult) {
	if (!isWideMultiplied()) {
		return width;
	}
	const auto res = width * (wideMultiplier * mult);
	return std::max(width, static_cast<int>(std::round(res)));
}

void setMaterialSwitches(bool val) {
	materialSwitches = val;
}

bool isMaterialSwitches() {
	return materialSwitches;
}

void setAvatarCorners(int val) {
	avatarCorners = val;
}

int getAvatarCorners() {
	return avatarCorners;
}

void setTypingAnimation(TypingAnimation val) {
	typingAnimation = val;
}

TypingAnimation getTypingAnimation() {
	return typingAnimation;
}

namespace {

[[nodiscard]] double EaseOutCubic(double t) {
	const auto inv = 1. - t;
	return 1. - inv * inv * inv;
}

[[nodiscard]] double EaseOutBack(double t) {
	constexpr auto c1 = 1.70158;
	constexpr auto c3 = c1 + 1.;
	const auto x = t - 1.;
	return 1. + c3 * x * x * x + c1 * x * x;
}

} // namespace

int TypingAnimationDuration(TypingAnimation mode) {
	switch (mode) {
	case TypingAnimation::Fade: return 200;
	case TypingAnimation::Rise: return 280;
	case TypingAnimation::Drop: return 280;
	case TypingAnimation::Pop: return 320;
	case TypingAnimation::Glow: return 520;
	case TypingAnimation::Off: break;
	}
	return 0;
}

TypingAnimationFrame ComputeTypingAnimationFrame(
		TypingAnimation mode,
		double progress) {
	const auto t = std::clamp(progress, 0., 1.);
	const auto eased = EaseOutCubic(t);
	auto result = TypingAnimationFrame();
	switch (mode) {
	case TypingAnimation::Fade:
		result.opacity = eased;
		break;
	case TypingAnimation::Rise:
		result.opacity = std::min(1., t * 1.6);
		result.offsetY = (1. - eased) * 0.5;
		break;
	case TypingAnimation::Drop:
		result.opacity = std::min(1., t * 1.6);
		result.offsetY = -(1. - eased) * 0.5;
		break;
	case TypingAnimation::Pop:
		result.opacity = std::min(1., t * 2.5);
		result.scale = 0.35 + 0.65 * EaseOutBack(t);
		break;
	case TypingAnimation::Glow:
		result.opacity = std::min(1., t * 4.);
		result.scale = 1. + 0.12 * (1. - EaseOutCubic(std::min(1., t * 1.5)));
		result.accent = 1. - eased;
		break;
	case TypingAnimation::Off:
		break;
	}
	return result;
}

}
