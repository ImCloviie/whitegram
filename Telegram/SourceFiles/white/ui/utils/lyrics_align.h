// This file is part of Whitegram for Desktop.
#pragma once

#include "white/ui/utils/lyrics_engine.h"
#include "white/ui/utils/track_analysis.h"

namespace White::Ui::Lyrics {

struct AlignReport {
	bool changed = false;
	bool autoSynced = false;
	std::int64_t offsetMs = 0;
};

// Published timing made for another cut of the song: shifts it to where the voice starts.
[[nodiscard]] AlignReport AlignOffset(Data &data, const Analysis::Result &analysis);

// Approximate fallback for plain text only. Published timing is never modified.
[[nodiscard]] AlignReport Align(Data &data, const Analysis::Result &analysis);
// Optional local speech model. Returns false without changing data on failure.
[[nodiscard]] bool AlignWithModel(Data &data, const QString &path, const QByteArray &audio);

} // namespace Ayu::Ui::Lyrics
