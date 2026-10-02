// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#pragma once

#include <QtCore/QString>
#include <cstdint>
#include <vector>

namespace Ayu::Ui::Lyrics {

struct Line {
	std::int64_t ms = 0; // start time in milliseconds
	QString text;
};

struct Data {
	bool hasData = false; // anything found (synced or plain)
	bool synced = false; // has timestamps
	QString artist;
	QString title;
	std::vector<Line> lines;
	QString plain; // fallback when no timestamps
	QString source; // e.g. "lrclib"
};

// Parse [mm:ss.xx] LRC content into lines. Supports multiple tags per line.
[[nodiscard]] std::vector<Line> ParseLrc(const QString &lrc);

// Synchronous network fetch (blocking, call from worker thread).
// Uses https://lrclib.net (free, no key, synced + plain lyrics).
[[nodiscard]] Data FetchSync(
	const QString &artist,
	const QString &title,
	int durationSec = 0,
	int timeoutMs = 6000);

// Cached variant: memory cache first, then network.
[[nodiscard]] Data FetchCachedSync(
	const QString &artist,
	const QString &title,
	int durationSec = 0);

// Active line index for positionMs. Returns -1 when nothing to show yet.
[[nodiscard]] int ActiveLineIndex(const Data &data, std::int64_t positionMs);

// Progress 0..1 inside the active line (to next line start). -1 if unknown.
[[nodiscard]] double LineProgress(
	const Data &data,
	int index,
	std::int64_t positionMs);

} // namespace Ayu::Ui::Lyrics
