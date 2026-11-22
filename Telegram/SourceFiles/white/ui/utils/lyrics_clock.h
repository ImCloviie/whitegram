// This file is part of Whitegram for Desktop.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace White::Ui::Lyrics {

// Interpolate only a fresh audio-clock sample. Never run freely during a stall,
// pause, or across a seek; both lyric surfaces use this same media clock.
[[nodiscard]] inline std::int64_t PlaybackPosition(
		std::int64_t reported,
		std::int64_t sample,
		std::int64_t sampledAt,
		std::int64_t now,
		double speed,
		bool advancing) {
	if (!advancing || sample < 0 || sampledAt < 0 || now < sampledAt
		|| now - sampledAt > 150 || std::abs(sample - reported) > 250
		|| !std::isfinite(speed) || speed <= 0.) {
		return std::max<std::int64_t>(0, reported);
	}
	return sample + std::int64_t((now - sampledAt) * speed);
}

} // namespace Ayu::Ui::Lyrics
