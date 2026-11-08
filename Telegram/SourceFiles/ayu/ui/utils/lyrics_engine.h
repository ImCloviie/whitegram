// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#pragma once

#include <QtCore/QString>
#include <cstdint>
#include <utility>
#include <vector>

namespace Ayu::Ui::Lyrics {

struct Word {
	std::int64_t startMs = 0;
	std::int64_t endMs = 0;
	QString text; // includes its trailing whitespace
	bool keyword = false;
	int accent = 0;
};

struct Line {
	std::int64_t ms = 0; // start time in milliseconds, -1 when untimed
	std::int64_t endMs = 0;
	QString text;
	std::vector<Word> words;
	bool repeated = false;
};

struct Data {
	bool hasData = false;
	bool synced = false;
	bool wordSynced = false;
	QString artist;
	QString title;
	std::vector<Line> lines;
	QString plain;
	QString source;
	QString keyword;
	bool autoSynced = false;
	int revision = 0;
};

[[nodiscard]] std::vector<Line> ParseLrc(const QString &lrc);

[[nodiscard]] Data FetchSync(
	const QString &artist,
	const QString &title,
	int durationSec = 0,
	int timeoutMs = 6000);

[[nodiscard]] Data FetchCachedSync(
	const QString &artist,
	const QString &title,
	int durationSec = 0);

[[nodiscard]] int ActiveLineIndex(const Data &data, std::int64_t positionMs);

[[nodiscard]] double LineProgress(
	const Data &data,
	int index,
	std::int64_t positionMs);

[[nodiscard]] double WordProgress(const Word &word, std::int64_t positionMs);

[[nodiscard]] std::vector<std::pair<std::int64_t, std::int64_t>> RepeatedBlocks(
	const Data &data);

[[nodiscard]] QString NormalizedWord(const QString &word);

[[nodiscard]] int Syllables(const QString &word);

} // namespace Ayu::Ui::Lyrics
