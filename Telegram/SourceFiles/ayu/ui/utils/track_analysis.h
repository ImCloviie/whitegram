// This file is part of Whitegram for Desktop.
#pragma once

#include <QtCore/QByteArray>

#include <cstdint>
#include <vector>

namespace Core {
class FileLocation;
} // namespace Core

namespace Ayu::Ui::Analysis {

struct Section {
	std::int64_t startMs = 0;
	std::int64_t endMs = 0;
};

struct Result {
	bool ok = false;
	std::int64_t durationMs = 0;
	double frameMs = 0.;
	double bpm = 0.;
	double confidence = 0.;
	std::vector<std::uint8_t> pulse;
	std::vector<std::uint8_t> energy;
	std::vector<std::uint8_t> spectrum;
	int bands = 0;
	std::vector<std::int64_t> beats;
	std::vector<Section> chorus;
	std::vector<std::uint8_t> voice;
	std::vector<std::uint8_t> syllable;

	[[nodiscard]] float pulseAt(std::int64_t ms) const;
	[[nodiscard]] float energyAt(std::int64_t ms) const;
	[[nodiscard]] float spectrumAt(std::int64_t ms, int band) const;
	[[nodiscard]] bool inChorus(std::int64_t ms) const;
};

[[nodiscard]] Result Analyze(
	const Core::FileLocation &file,
	const QByteArray &data,
	const std::vector<Section> &lyricBlocks);

} // namespace Ayu::Ui::Analysis
