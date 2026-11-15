// This file is part of Whitegram for Desktop.
#pragma once

#include "ayu/ui/utils/lyrics_engine.h"
#include "ayu/ui/utils/track_analysis.h"

namespace Ayu::Ui::Lyrics {

struct AlignReport {
	bool changed = false;
	bool autoSynced = false;
};

// Approximate fallback for plain text only. Published timing is never modified.
[[nodiscard]] AlignReport Align(Data &data, const Analysis::Result &analysis);
// Optional local speech model. Returns false without changing data on failure.
[[nodiscard]] bool AlignWithModel(Data &data, const QString &path, const QByteArray &audio);

} // namespace Ayu::Ui::Lyrics
