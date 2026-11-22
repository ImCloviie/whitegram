// This file is part of Whitegram for Desktop.
#include "ayu/ui/utils/lyrics_align.h"

#include <QtCore/QStringList>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <vector>
#include <mutex>

namespace Ayu::Ui::Lyrics {
namespace {

constexpr auto kMinWordMs = std::int64_t(70);
constexpr auto kFastSyllableMs = 110.;


struct Curves {
	const std::vector<std::uint8_t> *voice = nullptr;
	const std::vector<std::uint8_t> *syllable = nullptr;
	double frameMs = 0.;
	int count = 0;

	[[nodiscard]] int frameOf(std::int64_t ms) const {
		return std::clamp(int(std::lround(ms / frameMs)), 0, count - 1);
	}
	[[nodiscard]] std::int64_t msOf(int frame) const {
		return std::int64_t(std::lround(frame * frameMs));
	}
	[[nodiscard]] float voiceAt(int frame) const {
		return (*voice)[std::clamp(frame, 0, count - 1)] / 255.f;
	}
	[[nodiscard]] float syllableAt(int frame) const {
		return (*syllable)[std::clamp(frame, 0, count - 1)] / 255.f;
	}
};

QStringList WordTexts(const Line &line) {
	auto result = QStringList();
	if (!line.words.empty()) {
		for (const auto &word : line.words) {
			result.push_back(word.text);
		}
		return result;
	}
	const auto parts = line.text.split(QChar(u' '), Qt::SkipEmptyParts);
	for (auto i = 0; i != parts.size(); ++i) {
		result.push_back(parts[i] + ((i + 1 < parts.size()) ? u" "_q : QString()));
	}
	return result;
}

double WordWeight(const QString &text) {
	return 0.35 + Syllables(text.trimmed());
}

double LineWeight(const Line &line) {
	auto total = 0.;
	for (const auto &text : WordTexts(line)) {
		total += WordWeight(text);
	}
	return std::max(1., total);
}

float Percentile(std::vector<float> values, double fraction) {
	if (values.empty()) {
		return 0.f;
	}
	const auto index = std::clamp(int(fraction * (values.size() - 1)), 0, int(values.size()) - 1);
	std::nth_element(values.begin(), values.begin() + index, values.end());
	return values[index];
}

void PlaceWords(Line &line, std::int64_t limit, const Curves &c) {
	const auto texts = WordTexts(line);
	if (texts.isEmpty() || line.ms < 0) {
		return;
	}
	const auto start = line.ms;
	if (limit <= start) return;
	auto weights = std::vector<double>();
	auto total = 0.;
	for (const auto &text : texts) {
		weights.push_back(WordWeight(text));
		total += weights.back();
	}
	const auto from = c.frameOf(start);
	const auto till = std::max(from + 1, c.frameOf(limit));
	auto peak = 0.f;
	for (auto f = from; f < till; ++f) {
		peak = std::max(peak, c.voiceAt(f));
	}
	const auto threshold = std::max(0.12f, 0.45f * peak);
	auto lastVoiced = from;
	for (auto f = from; f < till; ++f) {
		if (c.voiceAt(f) >= threshold) {
			lastVoiced = f;
		}
	}
	const auto fastest = start + std::int64_t(total * kFastSyllableMs);
	auto end = c.msOf(lastVoiced) + 160;
	end = std::clamp(end, std::min(limit, fastest), limit);

	const auto count = int(texts.size());
	const auto minWordMs = std::min(kMinWordMs, (end - start) / count);
	auto bounds = std::vector<std::int64_t>(count + 1);
	auto cumulative = 0.;
	for (auto j = 0; j <= count; ++j) {
		bounds[j] = start + std::int64_t((end - start) * cumulative / total);
		if (j < count) {
			cumulative += weights[j];
		}
	}
	const auto average = double(end - start) / count;
	const auto window = std::max<std::int64_t>(40, std::min<std::int64_t>(220, std::int64_t(average * 0.45)));
	for (auto j = 1; j < count; ++j) {
		const auto expected = bounds[j];
		auto best = expected;
		auto bestValue = double(c.syllableAt(c.frameOf(expected)));
		for (auto f = c.frameOf(expected - window); f <= c.frameOf(expected + window); ++f) {
			const auto distance = std::abs(double(c.msOf(f) - expected)) / window;
			const auto value = c.syllableAt(f) * (1. - 0.5 * distance);
			if (value > bestValue) {
				bestValue = value;
				best = c.msOf(f);
			}
		}
		if (bestValue >= 0.22) {
			bounds[j] = best;
		}
		bounds[j] = std::clamp(
			bounds[j],
			bounds[j - 1] + minWordMs,
			end - (count - j) * minWordMs);
	}
	bounds[count] = end;

	auto words = std::vector<Word>();
	words.reserve(count);
	for (auto j = 0; j != count; ++j) {
		auto word = (j < int(line.words.size())) ? line.words[j] : Word();
		word.text = texts[j];
		word.startMs = bounds[j];
		word.endMs = bounds[j + 1];
		words.push_back(std::move(word));
	}
	line.words = std::move(words);
	line.endMs = bounds[count];
}

bool AutoSync(Data &data, const Curves &c) {
	const auto n = c.count;
	if (n < 50 || data.lines.empty()) {
		return false;
	}
	const auto radius = std::max(1, int(300. / c.frameMs));
	auto smooth = std::vector<float>(n);
	{
		auto prefix = std::vector<double>(n + 1, 0.);
		for (auto f = 0; f != n; ++f) {
			prefix[f + 1] = prefix[f] + c.voiceAt(f);
		}
		for (auto f = 0; f != n; ++f) {
			const auto a = std::max(0, f - radius), b = std::min(n, f + radius + 1);
			smooth[f] = float((prefix[b] - prefix[a]) / (b - a));
		}
	}
	const auto low = Percentile(smooth, 0.2);
	const auto high = Percentile(smooth, 0.9);
	auto mask = std::vector<char>(n, 0);
	if (high - low >= 0.08f) {
		const auto threshold = low + 0.45f * (high - low);
		for (auto f = 0; f != n; ++f) {
			mask[f] = (smooth[f] > threshold) ? 1 : 0;
		}
		const auto gap = int(450. / c.frameMs);
		const auto island = int(300. / c.frameMs);
		for (auto f = 0; f < n;) {
			auto g = f;
			while (g < n && mask[g] == mask[f]) {
				++g;
			}
			const auto length = g - f;
			if (!mask[f] && f > 0 && g < n && length < gap) {
				std::fill(mask.begin() + f, mask.begin() + g, 1);
			} else if (mask[f] && length < island) {
				std::fill(mask.begin() + f, mask.begin() + g, 0);
			}
			f = g;
		}
	}
	auto singing = std::vector<int>();
	for (auto f = 0; f != n; ++f) {
		if (mask[f]) {
			singing.push_back(f);
		}
	}
	if (singing.size() < std::size_t(n) / 8) {
		singing.clear();
		auto first = -1, last = -1;
		for (auto f = 0; f != n; ++f) {
			if (c.voiceAt(f) > 0.02f) {
				first = (first < 0) ? f : first;
				last = f;
			}
		}
		if (first < 0 || last - first < n / 4) {
			return false;
		}
		for (auto f = first; f <= last; ++f) {
			singing.push_back(f);
			mask[f] = 1;
		}
	}
	auto segmentStarts = std::vector<int>();
	for (auto f = 1; f != n; ++f) {
		if (mask[f] && !mask[f - 1]) {
			segmentStarts.push_back(f);
		}
	}
	if (mask[0]) {
		segmentStarts.insert(segmentStarts.begin(), 0);
	}

	const auto lines = int(data.lines.size());
	auto weights = std::vector<double>(lines);
	auto syllables = std::vector<double>(lines);
	auto total = 0.;
	auto totalSyllables = 0.;
	for (auto i = 0; i != lines; ++i) {
		weights[i] = LineWeight(data.lines[i]);
		total += weights[i];
		for (const auto &text : WordTexts(data.lines[i])) {
			syllables[i] += std::max(1, Syllables(text.trimmed()));
		}
		totalSyllables += syllables[i];
	}
	// WHY: a syllable is one attack of the voice, so counting attacks inside the sung parts
	// follows the real pace of the singer instead of assuming an even speed.
	auto events = std::vector<int>();
	{
		const auto gate = low + 0.3f * (high - low);
		for (auto f = 2; f + 2 < n; ++f) {
			const auto value = c.syllableAt(f);
			if (value < 0.16f || !mask[f] || smooth[f] < gate) {
				continue;
			}
			if (value >= c.syllableAt(f - 1) && value >= c.syllableAt(f - 2)
				&& value > c.syllableAt(f + 1) && value >= c.syllableAt(f + 2)) {
				if (events.empty() || (f - events.back()) * c.frameMs >= 90.) {
					events.push_back(f);
				}
			}
		}
	}
	const auto byEvents = (events.size() >= totalSyllables * 0.35);
	auto starts = std::vector<int>(lines);
	auto cumulative = 0.;
	auto cumulativeSyllables = 0.;
	for (auto i = 0; i != lines; ++i) {
		auto target = 0;
		if (byEvents) {
			const auto index = std::clamp(
				int(std::floor(events.size() * cumulativeSyllables / totalSyllables)),
				0,
				int(events.size()) - 1);
			target = events[index];
		} else {
			const auto index = std::clamp(
				int(std::floor(singing.size() * cumulative / total)),
				0,
				int(singing.size()) - 1);
			target = singing[index];
		}
		const auto snap = int((byEvents ? 600. : 1200.) / c.frameMs);
		const auto onsetSnap = int(400. / c.frameMs);
		auto best = target;
		auto bestScore = 0.25;
		for (const auto start : segmentStarts) {
			const auto distance = std::abs(start - target);
			if (distance <= snap) {
				const auto score = 1. - double(distance) / snap;
				if (score > bestScore) {
					bestScore = score;
					best = start;
				}
			}
		}
		for (auto f = std::max(0, target - onsetSnap); f <= std::min(n - 1, target + onsetSnap); ++f) {
			const auto strength = c.syllableAt(f);
			if (strength >= 0.45f) {
				const auto score = 0.5 * strength * (1. - double(std::abs(f - target)) / onsetSnap);
				if (score > bestScore) {
					bestScore = score;
					best = f;
				}
			}
		}
		if (i > 0) {
			const auto minimum = starts[i - 1] + int(weights[i - 1] * 100. / c.frameMs);
			best = std::max(best, std::min(minimum, n - 1));
		}
		starts[i] = best;
		cumulative += weights[i];
		cumulativeSyllables += syllables[i];
	}
	const auto tail = c.msOf(singing.back()) + 300;
	for (auto i = 0; i != lines; ++i) {
		auto &line = data.lines[i];
		line.ms = c.msOf(starts[i]);
		line.words.clear();
	}
	for (auto i = 0; i != lines; ++i) {
		const auto limit = (i + 1 < lines) ? data.lines[i + 1].ms : std::max(tail, data.lines[i].ms + 3000);
		PlaceWords(data.lines[i], limit, c);
	}
	data.synced = true;
	data.wordSynced = false;
	data.autoSynced = true;
	return true;
}

} // namespace

AlignReport Align(Data &data, const Analysis::Result &analysis) {
	auto report = AlignReport();
	if (!data.hasData
		|| data.synced
		|| data.lines.empty()
		|| !analysis.ok
		|| analysis.frameMs <= 0.
		|| analysis.voice.empty()
		|| analysis.syllable.size() != analysis.voice.size()) {
		return report;
	}
	const auto c = Curves{
		.voice = &analysis.voice,
		.syllable = &analysis.syllable,
		.frameMs = analysis.frameMs,
		.count = int(analysis.voice.size()),
	};
	report.autoSynced = report.changed = AutoSync(data, c);
	if (report.changed) {
		for (auto &line : data.lines) {
			for (auto &word : line.words) {
				word.keyword = !data.keyword.isEmpty() && (NormalizedWord(word.text) == data.keyword);
			}
		}
		++data.revision;
	}
	return report;
}

AlignReport AlignOffset(Data &data, const Analysis::Result &analysis) {
	auto report = AlignReport();
	if (!data.hasData
		|| !data.synced
		|| data.autoSynced
		|| data.lines.size() < 6
		|| !analysis.ok
		|| analysis.frameMs <= 0.
		|| analysis.voice.empty()
		|| analysis.syllable.size() != analysis.voice.size()) {
		return report;
	}
	const auto c = Curves{
		.voice = &analysis.voice,
		.syllable = &analysis.syllable,
		.frameMs = analysis.frameMs,
		.count = int(analysis.voice.size()),
	};
	auto prefix = std::vector<double>(c.count + 1, 0.);
	for (auto f = 0; f != c.count; ++f) {
		prefix[f + 1] = prefix[f] + c.voiceAt(f);
	}
	const auto mean = [&](std::int64_t from, std::int64_t till) {
		const auto a = c.frameOf(from);
		const auto b = std::max(a + 1, c.frameOf(till));
		return (prefix[std::min(b, c.count)] - prefix[a]) / std::max(1, b - a);
	};
	// A sung line starts where the voice rises and a syllable attacks.
	const auto score = [&](std::int64_t shift) {
		auto total = 0.;
		auto counted = 0;
		for (auto i = 0; i != int(data.lines.size()); ++i) {
			const auto &line = data.lines[i];
			const auto at = line.ms + shift;
			if (at < 600 || at > analysis.durationMs - 1000) {
				continue;
			}
			const auto next = (i + 1 < int(data.lines.size())) ? data.lines[i + 1].ms : line.ms + 3000;
			const auto span = std::clamp<std::int64_t>(next - line.ms, 300, 1200);
			auto attack = 0.f;
			for (auto f = c.frameOf(at - 80); f <= c.frameOf(at + 80); ++f) {
				attack = std::max(attack, c.syllableAt(f));
			}
			total += (mean(at, at + span) - mean(at - 500, at)) + 0.5 * attack;
			++counted;
		}
		return counted ? total / counted : 0.;
	};
	const auto fileMs = analysis.durationMs;
	const auto mismatch = (data.sourceDurationMs > 0 && fileMs > 0)
		? std::abs(fileMs - data.sourceDurationMs)
		: std::int64_t(0);
	// Another cut moves lines by about the length difference; a matching cut only drifts a little.
	const auto range = (mismatch >= 1200) ? std::min<std::int64_t>(8000, mismatch + 1500) : 1500;
	const auto step = std::max<std::int64_t>(10, std::int64_t(std::lround(analysis.frameMs)));
	const auto base = score(0);
	auto best = std::int64_t(0);
	auto bestScore = base;
	for (auto shift = -range; shift <= range; shift += step) {
		const auto value = score(shift) - 0.004 * std::abs(shift) / 1000.;
		if (value > bestScore) {
			bestScore = value;
			best = shift;
		}
	}
	const auto needed = (mismatch >= 1200) ? 0.03 : 0.05;
	if (std::abs(best) < 200 || bestScore - base < std::max(needed, 0.25 * std::abs(base))) {
		return report;
	}
	for (auto &line : data.lines) {
		line.ms = std::max<std::int64_t>(0, line.ms + best);
		line.endMs = std::max(line.ms, line.endMs + best);
		for (auto &word : line.words) {
			word.startMs = std::max<std::int64_t>(0, word.startMs + best);
			word.endMs = std::max(word.startMs, word.endMs + best);
		}
	}
	++data.revision;
	report.changed = true;
	report.offsetMs = best;
	return report;
}

bool AlignWithModel(Data &data, const QString &path, const QByteArray &audio) {
	const auto python = qEnvironmentVariable("WHITEGRAM_KARAOKE_PYTHON");
	const auto model = qEnvironmentVariable("WHITEGRAM_KARAOKE_MODEL");
	const auto script = QCoreApplication::applicationDirPath() + u"/karaoke_align.py"_q;
	if (python.isEmpty() || model.isEmpty() || data.wordSynced || !data.hasData
		|| !QFileInfo::exists(model) || !QFileInfo::exists(script)) return false;
	static auto modelMutex = std::mutex();
	const auto modelLock = std::unique_lock<std::mutex>(modelMutex, std::try_to_lock);
	if (!modelLock.owns_lock()) return false;
	auto directory = QTemporaryDir();
	if (!directory.isValid()) return false;
	auto audioPath = path;
	if (audioPath.isEmpty()) {
		if (audio.isEmpty()) return false;
		audioPath = directory.filePath(u"audio.bin"_q);
		auto file = QFile(audioPath);
		if (!file.open(QIODevice::WriteOnly) || file.write(audio) != audio.size()) return false;
	}
	const auto textPath = directory.filePath(u"lyrics.txt"_q);
	{
		auto file = QFile(textPath);
		if (!file.open(QIODevice::WriteOnly)) return false;
		for (const auto &line : data.lines) file.write((line.text + QChar(u'\n')).toUtf8());
	}
	const auto output = directory.filePath(u"aligned.lrc"_q);
	auto process = QProcess();
	process.setStandardOutputFile(QProcess::nullDevice());
	process.setStandardErrorFile(QProcess::nullDevice());
	process.start(python, { script, audioPath, textPath, output, model });
	if (!process.waitForStarted(5000)) return false;
	if (!process.waitForFinished(180000)) {
		process.kill();
		process.waitForFinished(5000);
		return false;
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return false;
	auto file = QFile(output);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) return false;
	auto aligned = ParseTimedText(QString::fromUtf8(file.readAll()));
	if (!aligned.wordSynced || aligned.lines.size() != data.lines.size()) return false;
	for (auto i = 0; i != int(data.lines.size()); ++i) {
		if (NormalizedWord(aligned.lines[i].text) != NormalizedWord(data.lines[i].text)) return false;
		const auto &line = aligned.lines[i];
		if (line.ms < 0 || line.endMs <= line.ms) return false;
		for (const auto &word : line.words) {
			if (word.endMs <= word.startMs || word.startMs < line.ms || word.endMs > line.endMs) return false;
		}
	}
	aligned.artist = data.artist;
	aligned.title = data.title;
	aligned.source = data.source + u" + local alignment"_q;
	aligned.revision = data.revision + 1;
	data = std::move(aligned);
	return true;
}

} // namespace Ayu::Ui::Lyrics
