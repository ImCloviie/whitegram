// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace AyuUiSettings {

inline constexpr int kMaxAvatarCorners = 23;

void setMonoFont(QString newFont);
QString getMonoFont();

void setWideMultiplier(double val);

bool isWideMultiplied();
int getWideMultiplied(int width, double mult);

void setMaterialSwitches(bool val);
bool isMaterialSwitches();

void setAvatarCorners(int val);
int getAvatarCorners();

enum class TypingAnimation {
	Off = 0,
	Fade = 1,
	Rise = 2,
	Drop = 3,
	Pop = 4,
	Glow = 5,
};
inline constexpr int kTypingAnimationMax = 5;

void setTypingAnimation(TypingAnimation val);
TypingAnimation getTypingAnimation();

struct TypingAnimationFrame {
	double opacity = 1.;
	double offsetY = 0.; // In line heights, positive is down.
	double scale = 1.;
	double accent = 0.; // 1. is the accent color, 0. is the text color.
};

[[nodiscard]] int TypingAnimationDuration(TypingAnimation mode);
[[nodiscard]] TypingAnimationFrame ComputeTypingAnimationFrame(
	TypingAnimation mode,
	double progress);

}
