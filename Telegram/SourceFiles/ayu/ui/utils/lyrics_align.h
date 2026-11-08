// This file is part of Whitegram for Desktop.
#pragma once

#include "ayu/ui/utils/lyrics_engine.h"
#include "ayu/ui/utils/track_analysis.h"

namespace Ayu::Ui::Lyrics {

struct AlignReport {
	bool changed = false;
	bool autoSynced = false;
	std::int64_t offsetMs = 0;
	int refinedLines = 0;
};

// Fits lyric timing to the voice in the audio (offset, word placement, timings for plain text).
[[nodiscard]] AlignReport Align(Data &data, const Analysis::Result &analysis);

} // namespace Ayu::Ui::Lyrics
