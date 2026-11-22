// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#include "ayu/ui/utils/lyrics_engine.h"

#include "ayu/ui/utils/net_fetch.h"
#include "settings.h"

#include <QtCore/QCache>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QEventLoop>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMutex>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <future>
#include <map>
#include <optional>

namespace Ayu::Ui::Lyrics {
namespace {

constexpr auto kSyllableMs = 260;
constexpr auto kLineTailMs = 300;
// Search may find the same recording with encoder padding (e.g. 119 vs 122s).
// Keep timestamps intact; never stretch them to fill the file duration.
constexpr auto kDurationTolerance = 3.;

QCache<QString, Data> &Cache() {
	static auto result = QCache<QString, Data>(100);
	return result;
}

QMutex &CacheMutex() {
	static auto result = QMutex();
	return result;
}

QString CacheKey(const QString &artist, const QString &title, int duration) {
	return artist.trimmed().toLower()
		+ u" - "_q
		+ title.trimmed().toLower()
		+ u" / "_q
		+ QString::number(duration);
}

QByteArray GetBytes(const QUrl &url, int timeoutMs, bool browserAgent) {
	return Net::FetchSync({
		.url = url,
		.userAgent = browserAgent
			? u"Mozilla/5.0 (Windows NT 10.0; Win64; x64)"_q
			: QString(),
		.timeoutMs = timeoutMs,
	});
}

std::int64_t ParseStamp(const QString &mm, const QString &ss, const QString &frac) {
	auto ms = std::int64_t(mm.toInt() * 60 + ss.toInt()) * 1000;
	if (!frac.isEmpty()) {
		auto f = frac.left(3);
		while (f.size() < 3) {
			f += QChar(u'0');
		}
		ms += f.toInt();
	}
	return ms;
}

bool IsVowel(QChar ch) {
	static const auto vowels = QString::fromUtf8(
		"aeiouyаеёиоуыэюяáéíóúàèìòùäëïöüâêîôûæøåœ");
	return vowels.contains(ch.toLower());
}

int SyllableCount(const QString &word) {
	auto count = 0;
	auto previous = false;
	for (const auto ch : word) {
		const auto vowel = IsVowel(ch);
		if (vowel && !previous) {
			++count;
		}
		previous = vowel;
	}
	return std::max(count, 1);
}

bool IsCjk(QChar ch) {
	const auto u = ch.unicode();
	return (u >= 0x3040 && u <= 0x30FF)
		|| (u >= 0x3400 && u <= 0x9FFF)
		|| (u >= 0xAC00 && u <= 0xD7AF);
}

QString StripForMatch(QString value) {
	value = value.toLower();
	value.replace(QChar(u'ё'), QChar(u'е'));
	auto result = QString();
	result.reserve(value.size());
	auto space = false;
	for (const auto ch : value) {
		if (ch.isLetterOrNumber()) {
			result += ch;
			space = false;
		} else if (!space && !result.isEmpty()) {
			result += QChar(u' ');
			space = true;
		}
	}
	return result.trimmed();
}

QString CleanedTitle(const QString &title) {
	auto out = title.trimmed();
	static const auto tags = QRegularExpression(
		uR"(\s*[\(\[]\s*(prod\.?|producer|by|feat\.?|ft\.?|with|w/|remaster(ed)?|explicit|official|lyrics?|audio|video|sped up|slowed|unreleased|snippet|leak(ed)?|demo|prerelease)\b[^\(\)\[\]]*[\)\]])"_q,
		QRegularExpression::CaseInsensitiveOption);
	out.replace(tags, QString());
	static const auto tail = QRegularExpression(
		uR"((\s+[-–—]\s*(prod\.?|producer|feat\.?|ft\.?)\b.*$)|(\s+w/\s+.*$))"_q,
		QRegularExpression::CaseInsensitiveOption);
	out.replace(tail, QString());
	static const auto ext = QRegularExpression(
		uR"(\.(mp3|m4a|flac|ogg|opus|wav|aac)$)"_q,
		QRegularExpression::CaseInsensitiveOption);
	out.replace(ext, QString());
	// Uploaders append "| artist |" and "(@channel)" to titles; no catalogue has them.
	static const auto uploader = QRegularExpression(
		uR"((\s*[\(\[]\s*@[^\)\]]*[\)\]])|(\s*\|.*$))"_q);
	out.replace(uploader, QString());
	return out.trimmed();
}

QString CleanedArtist(const QString &artist) {
	auto out = artist;
	static const auto groups = QRegularExpression(uR"([\(\[（][^\)\]）]*[\)\]）])"_q);
	static const auto handles = QRegularExpression(uR"(@[\w.]+)"_q);
	static const auto channel = QRegularExpression(
		uR"(^\s*(тгк|tgk|tg|канал)\s+)"_q,
		QRegularExpression::CaseInsensitiveOption);
	out.replace(groups, QString());
	out.replace(handles, QString());
	out.replace(channel, QString());
	auto result = QString();
	for (const auto ch : out) {
		result += (ch.isLetterOrNumber() || ch.isSpace() || ch == QChar(u',') || ch == QChar(u'&') || ch == QChar(u'-') || ch == QChar(u'.') || ch == QChar(u'\''))
			? ch
			: QChar(u' ');
	}
	return result.simplified();
}

QString ProdArtistFromTitle(const QString &title) {
	static const auto re = QRegularExpression(
		uR"([\(\[]\s*prod\.?\s+([^\)\]]+)[\)\]])"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto match = re.match(title);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QStringList ArtistTokens(const QString &artist) {
	static const auto split = QRegularExpression(
		uR"(\s*(,|、|&|/|;|\bfeat\.?|\bft\.?|\bx\b|\band\b|\bи\b)\s*)"_q,
		QRegularExpression::CaseInsensitiveOption);
	auto result = QStringList();
	for (const auto &part : artist.split(split, Qt::SkipEmptyParts)) {
		const auto stripped = StripForMatch(part);
		if (!stripped.isEmpty()) {
			result.push_back(stripped);
		}
	}
	return result;
}

bool TitleMatches(const QString &have, const QString &want) {
	const auto a = StripForMatch(CleanedTitle(have));
	const auto b = StripForMatch(CleanedTitle(want));
	if (a.isEmpty() || b.isEmpty()) {
		return false;
	} else if (a == b) {
		return true;
	}
	const auto shorter = std::min(a.size(), b.size());
	const auto longer = std::max(a.size(), b.size());
	return (a.contains(b) || b.contains(a)) && (shorter * 10 >= longer * 6);
}

bool ArtistMatches(const QString &have, const QString &want) {
	const auto wanted = ArtistTokens(want);
	if (wanted.isEmpty()) {
		return true;
	}
	const auto available = ArtistTokens(have);
	for (const auto &token : wanted) {
		if (available.contains(token)) {
			return true;
		}
	}
	return false;
}

bool DurationMatches(double have, int want) {
	return (want <= 0) || (have <= 0.) || (std::abs(have - want) <= kDurationTolerance);
}

int MatchScore(
		const QString &title,
		const QString &artist,
		double duration,
		const QString &wantTitle,
		const QString &wantArtist,
		int wantDuration) {
	if (!TitleMatches(title, wantTitle)
		|| !DurationMatches(duration, wantDuration)) {
		return 0;
	}
	if (!ArtistMatches(artist, wantArtist)) {
		return 0;
	}
	auto score = 10;
	if (StripForMatch(CleanedTitle(title)) == StripForMatch(CleanedTitle(wantTitle))) {
		score += 4;
	}
	if (!wantArtist.isEmpty()) {
		score += 4;
	}
	if (wantDuration > 0 && duration > 0.) {
		score += std::max(0, 6 - int(std::abs(duration - wantDuration)));
	}
	return score;
}

void AppendWord(std::vector<Word> &words, Word word) {
	// A whitespace chunk only separates words; as its own word it would hold the line lit.
	if (word.text.trimmed().isEmpty()) {
		if (!words.empty()) {
			words.back().text += word.text;
		}
		return;
	}
	words.push_back(std::move(word));
}

void FixSpacing(Line &line) {
	// Syllables of one word arrive unspaced, so spaces are added only when a provider stripped them all.
	if (line.words.size() < 2) {
		return;
	}
	for (const auto &word : line.words) {
		if (word.text.contains(QChar(u' ')) || IsCjk(word.text.front())) {
			return;
		}
	}
	for (auto i = 0; i + 1 < int(line.words.size()); ++i) {
		line.words[i].text += QChar(u' ');
	}
}

bool IsCreditLine(const QString &text) {
	static const auto re = QRegularExpression(
		uR"(^\s*(作词|作曲|编曲|制作人|混音|母带|和声|吉他|贝斯|鼓|监制|出品|录音|词|曲|Lyrics by|Composed by|Produced by|Written by)\s*[:：])"_q,
		QRegularExpression::CaseInsensitiveOption);
	return re.match(text).hasMatch();
}

std::vector<Line> ParseYrc(const QString &yrc) {
	auto result = std::vector<Line>();
	static const auto head = QRegularExpression(uR"(^\[(\d+),(\d+)\])"_q);
	static const auto chunk = QRegularExpression(uR"(\((\d+),(\d+),\d+\)([^\(]*))"_q);
	for (const auto &raw : yrc.split(u'\n')) {
		const auto line = raw.trimmed();
		const auto match = head.match(line);
		if (!match.hasMatch()) {
			continue;
		}
		auto parsed = Line();
		parsed.ms = match.captured(1).toLongLong();
		parsed.endMs = parsed.ms + match.captured(2).toLongLong();
		auto it = chunk.globalMatch(line, match.capturedEnd());
		while (it.hasNext()) {
			const auto m = it.next();
			auto word = Word();
			word.startMs = m.captured(1).toLongLong();
			word.endMs = word.startMs + m.captured(2).toLongLong();
			word.text = m.captured(3);
			if (!word.text.isEmpty()) {
				AppendWord(parsed.words, std::move(word));
			}
		}
		FixSpacing(parsed);
		for (const auto &word : parsed.words) {
			parsed.text += word.text;
		}
		parsed.text = parsed.text.trimmed();
		if (parsed.text.isEmpty() || IsCreditLine(parsed.text)) {
			continue;
		}
		result.push_back(std::move(parsed));
	}
	std::sort(result.begin(), result.end(), [](const Line &a, const Line &b) {
		return a.ms < b.ms;
	});
	return result;
}

void EstimateWords(Line &line, std::int64_t limitMs) {
	if (!line.words.empty() || line.ms < 0) {
		return;
	}
	const auto parts = line.text.split(QChar(u' '), Qt::SkipEmptyParts);
	if (parts.isEmpty()) {
		return;
	}
	auto weights = std::vector<double>();
	auto total = 0.;
	for (const auto &part : parts) {
		const auto weight = 0.35 + SyllableCount(part);
		weights.push_back(weight);
		total += weight;
	}
	const auto natural = std::int64_t(total * kSyllableMs) + kLineTailMs;
	// An instrumental break before the next line must not stretch the fill over it.
	auto end = (limitMs > line.ms)
		? std::min(limitMs, line.ms + natural * 8 / 5)
		: line.ms + natural;
	if (line.endMs > line.ms) {
		end = std::min(end, line.endMs);
	}
	// A dense line must not spill across the next published line boundary.
	const auto span = std::max<std::int64_t>(0, end - line.ms);
	auto at = double(line.ms);
	for (auto i = 0; i != parts.size(); ++i) {
		const auto length = span * weights[i] / total;
		auto word = Word();
		word.startMs = std::int64_t(at);
		word.endMs = std::int64_t(at + length);
		word.text = parts[i] + ((i + 1 < parts.size()) ? u" "_q : QString());
		line.words.push_back(std::move(word));
		at += length;
	}
	line.words.back().endMs = end;
	line.endMs = end;
}

const QSet<QString> &StopWords() {
	static const auto result = [] {
		auto set = QSet<QString>();
		const auto list = QString::fromUtf8(
			"это как так что все всё мне меня тебя тебе его она они оно мы вы ты "
			"был была были быть есть только когда если или где там тут вот ну "
			"для при про над под без через этот эта эти тот та те кто чем чтобы "
			"себя себе свой своя свои очень просто хочу может нас вас нам вам "
			"мой моя мои твой твоя твои уже еще ещё даже нет да же бы ли "
			"the and you your for are was were with that this have has had not "
			"but all just like what when where who how why from they them their "
			"there then than its it's i'm im don't dont can't cant yeah oh ooh "
			"uh ah hey baby gonna wanna got get let me my mine our out into "
			"over about cause 'cause i'll you're we're she he her his him "
			"been can will would could should know say said one now see "
			"come go back take make way too yes no off on in of to it is");
		for (const auto &word : list.split(QChar(u' '), Qt::SkipEmptyParts)) {
			set.insert(word);
		}
		return set;
	}();
	return result;
}

bool IsChant(const QString &word) {
	auto letters = QSet<QChar>();
	for (const auto ch : word) {
		letters.insert(ch);
	}
	return letters.size() <= 2;
}

void Annotate(Data &data) {
	if (data.lines.empty()) {
		return;
	}
	if (data.synced) {
		for (auto i = 0; i != int(data.lines.size()); ++i) {
			auto &line = data.lines[i];
			const auto next = (i + 1 < int(data.lines.size()))
				? data.lines[i + 1].ms
				: std::int64_t(-1);
			if (line.endMs <= line.ms && next > line.ms) {
				line.endMs = next;
			}
			if (!line.words.empty() && line.words.back().endMs > line.ms) {
				// A held trailing chunk (16 s of silence in some yrc) is not part of the sung line.
				line.endMs = std::min(line.endMs, line.words.back().endMs + kLineTailMs);
			}
			EstimateWords(line, next);
			if (line.endMs <= line.ms) {
				line.endMs = line.ms + 3000;
			}
		}
	}

	auto lineCounts = std::map<QString, int>();
	for (const auto &line : data.lines) {
		const auto key = StripForMatch(line.text);
		if (key.size() >= 4) {
			++lineCounts[key];
		}
	}
	for (auto &line : data.lines) {
		const auto it = lineCounts.find(StripForMatch(line.text));
		line.repeated = (it != lineCounts.end()) && (it->second >= 2);
	}

	auto scores = std::map<QString, double>();
	auto lineHits = std::map<QString, QSet<int>>();
	for (auto i = 0; i != int(data.lines.size()); ++i) {
		const auto &line = data.lines[i];
		for (const auto &token : line.text.split(QChar(u' '), Qt::SkipEmptyParts)) {
			const auto word = NormalizedWord(token);
			if (word.size() < 3 || StopWords().contains(word) || IsChant(word)) {
				continue;
			}
			scores[word] += line.repeated ? 2. : 1.;
			lineHits[word].insert(i);
		}
	}
	auto best = QString();
	auto bestScore = 0.;
	for (const auto &[word, score] : scores) {
		const auto distinct = lineHits[word].size();
		const auto adjusted = score + 0.05 * word.size();
		if (distinct >= 3 && adjusted > bestScore) {
			best = word;
			bestScore = adjusted;
		}
	}
	if (best.isEmpty() || bestScore < 4.) {
		return;
	}
	data.keyword = best;
	for (auto &line : data.lines) {
		for (auto &word : line.words) {
			word.keyword = (NormalizedWord(word.text) == best);
		}
	}
}

Data FromLrcText(const QString &synced, const QString &plain) {
	auto data = Data();
	if (!synced.trimmed().isEmpty()) {
		data.lines = ParseLrc(synced);
		data.synced = !data.lines.empty();
		data.wordSynced = data.synced && std::all_of(
			data.lines.begin(),
			data.lines.end(),
			[](const Line &line) { return !line.words.empty(); });
	}
	if (!data.synced && !plain.trimmed().isEmpty()) {
		data.plain = plain.trimmed();
		for (const auto &row : data.plain.split(u'\n')) {
			const auto text = row.trimmed();
			if (!text.isEmpty()) {
				data.lines.push_back(Line{ .ms = -1, .text = text });
			}
		}
	}
	data.hasData = !data.lines.empty();
	return data;
}

struct Candidate {
	QString title;
	QString artist;
	double duration = 0.;
	QString synced;
	QString plain;
	int score = 0;
};

std::vector<Candidate> LrclibSearch(const QString &query, int timeoutMs) {
	auto url = QUrl(u"https://lrclib.net/api/search"_q);
	auto q = QUrlQuery();
	q.addQueryItem(u"q"_q, query);
	url.setQuery(q);
	auto result = std::vector<Candidate>();
	const auto doc = QJsonDocument::fromJson(GetBytes(url, timeoutMs, false));
	for (const auto &value : doc.array()) {
		const auto o = value.toObject();
		auto c = Candidate();
		c.title = o.value(u"trackName"_q).toString();
		c.artist = o.value(u"artistName"_q).toString();
		c.duration = o.value(u"duration"_q).toDouble();
		c.synced = o.value(u"syncedLyrics"_q).toString();
		c.plain = o.value(u"plainLyrics"_q).toString();
		if (!c.synced.trimmed().isEmpty() || !c.plain.trimmed().isEmpty()) {
			result.push_back(std::move(c));
		}
	}
	return result;
}

std::optional<Candidate> LrclibGet(
		const QString &artist,
		const QString &title,
		int duration,
		int timeoutMs) {
	auto url = QUrl(u"https://lrclib.net/api/get"_q);
	auto q = QUrlQuery();
	q.addQueryItem(u"artist_name"_q, artist);
	q.addQueryItem(u"track_name"_q, title);
	if (duration > 0) {
		q.addQueryItem(u"duration"_q, QString::number(duration));
	}
	url.setQuery(q);
	const auto doc = QJsonDocument::fromJson(GetBytes(url, timeoutMs, false));
	const auto o = doc.object();
	auto c = Candidate();
	c.title = o.value(u"trackName"_q).toString();
	c.artist = o.value(u"artistName"_q).toString();
	c.duration = o.value(u"duration"_q).toDouble();
	c.synced = o.value(u"syncedLyrics"_q).toString();
	c.plain = o.value(u"plainLyrics"_q).toString();
	if (c.synced.trimmed().isEmpty() && c.plain.trimmed().isEmpty()) {
		return std::nullopt;
	}
	return c;
}

std::optional<Candidate> BestLrclib(
		const QString &artist,
		const QString &title,
		int duration,
		int timeoutMs) {
	auto best = std::optional<Candidate>();
	const auto consider = [&](Candidate c) {
		c.score = MatchScore(c.title, c.artist, c.duration, title, artist, duration);
		if (!c.score) {
			return;
		}
		c.score += c.synced.trimmed().isEmpty() ? 0 : 8;
		if (!best || c.score > best->score) {
			best = std::move(c);
		}
	};
	if (!artist.isEmpty()) {
		if (auto exact = LrclibGet(artist, title, duration, timeoutMs)) {
			consider(std::move(*exact));
		}
		if (best && !best->synced.trimmed().isEmpty()) {
			return best;
		}
	}
	auto queries = QStringList();
	if (!artist.isEmpty()) {
		queries.push_back(artist + u" "_q + title);
	}
	queries.push_back(title);
	const auto words = StripForMatch(title).split(QChar(u' '), Qt::SkipEmptyParts);
	if (words.size() >= 3) {
		queries.push_back(words.mid(0, words.size() - 1).join(QChar(u' ')));
	}
	for (const auto &query : queries) {
		for (auto &c : LrclibSearch(query, timeoutMs)) {
			consider(std::move(c));
		}
		if (best && !best->synced.trimmed().isEmpty()) {
			break;
		}
	}
	return best;
}

Data NeteaseFetch(
		const QString &artist,
		const QString &title,
		int duration,
		int timeoutMs) {
	auto url = QUrl(u"https://music.163.com/api/search/get"_q);
	auto q = QUrlQuery();
	q.addQueryItem(u"s"_q, artist.isEmpty() ? title : (artist + u" "_q + title));
	q.addQueryItem(u"type"_q, u"1"_q);
	q.addQueryItem(u"limit"_q, u"10"_q);
	url.setQuery(q);
	const auto doc = QJsonDocument::fromJson(GetBytes(url, timeoutMs, true));
	const auto songs = doc.object()
		.value(u"result"_q).toObject()
		.value(u"songs"_q).toArray();
	auto bestId = qint64(0);
	auto bestDuration = 0.;
	auto bestScore = 0;
	for (const auto &value : songs) {
		const auto song = value.toObject();
		auto artists = QStringList();
		for (const auto &a : song.value(u"artists"_q).toArray()) {
			artists.push_back(a.toObject().value(u"name"_q).toString());
		}
		const auto score = MatchScore(
			song.value(u"name"_q).toString(),
			artists.join(u", "_q),
			song.value(u"duration"_q).toDouble() / 1000.,
			title,
			artist,
			duration);
		if (score > bestScore) {
			bestScore = score;
			bestId = qint64(song.value(u"id"_q).toDouble());
			bestDuration = song.value(u"duration"_q).toDouble();
		}
	}
	if (!bestId) {
		return Data();
	}
	auto lyricUrl = QUrl(u"https://music.163.com/api/song/lyric"_q);
	auto lq = QUrlQuery();
	lq.addQueryItem(u"id"_q, QString::number(bestId));
	lq.addQueryItem(u"lv"_q, u"1"_q);
	lq.addQueryItem(u"yv"_q, u"1"_q);
	lq.addQueryItem(u"tv"_q, u"-1"_q);
	lyricUrl.setQuery(lq);
	const auto lyric = QJsonDocument::fromJson(
		GetBytes(lyricUrl, timeoutMs, true)).object();
	const auto yrc = lyric.value(u"yrc"_q).toObject().value(u"lyric"_q).toString();
	auto data = Data();
	data.lines = ParseYrc(yrc);
	if (data.lines.size() >= 4) {
		data.synced = data.wordSynced = true;
	} else {
		const auto lrc = lyric.value(u"lrc"_q).toObject().value(u"lyric"_q).toString();
		data = FromLrcText(lrc, QString());
		data.lines.erase(
			std::remove_if(data.lines.begin(), data.lines.end(), [](const Line &l) {
				return IsCreditLine(l.text);
			}),
			data.lines.end());
		if (data.lines.size() < 4) {
			return Data();
		}
	}
	data.hasData = true;
	data.source = u"netease"_q;
	data.sourceDurationMs = std::int64_t(bestDuration);
	return data;
}

QByteArray Inflate(const QByteArray &data) {
	auto stream = z_stream();
	if (inflateInit(&stream) != Z_OK) {
		return {};
	}
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
	stream.avail_in = uInt(data.size());
	auto result = QByteArray();
	char buffer[16384];
	while (true) {
		stream.next_out = reinterpret_cast<Bytef*>(buffer);
		stream.avail_out = sizeof(buffer);
		const auto code = inflate(&stream, Z_NO_FLUSH);
		if (code != Z_OK && code != Z_STREAM_END) {
			inflateEnd(&stream);
			return {};
		}
		result.append(buffer, int(sizeof(buffer) - stream.avail_out));
		// A lyric file is a few kilobytes; a reply that inflates past this is a zip bomb.
		if (result.size() > 4 * 1024 * 1024) {
			inflateEnd(&stream);
			return {};
		}
		if (code == Z_STREAM_END || (stream.avail_in == 0 && stream.avail_out != 0)) {
			break;
		}
	}
	inflateEnd(&stream);
	return result;
}

QString DecodeKrc(const QByteArray &base64) {
	const auto raw = QByteArray::fromBase64(base64);
	if (!raw.startsWith("krc1")) {
		return QString();
	}
	static const unsigned char key[] = {
		64, 71, 97, 119, 94, 50, 116, 71, 81, 54, 49, 45, 206, 210, 110, 105 };
	auto body = raw.mid(4);
	for (auto i = 0; i != body.size(); ++i) {
		body[i] = char(uchar(body[i]) ^ key[i % 16]);
	}
	return QString::fromUtf8(Inflate(body));
}

bool IsHeaderLine(const QString &text, const QString &artist, const QString &title) {
	const auto stripped = StripForMatch(text);
	const auto wantTitle = StripForMatch(CleanedTitle(title));
	return !wantTitle.isEmpty()
		&& stripped.contains(wantTitle)
		&& (text.contains(u" - "_q) || (!artist.isEmpty() && stripped.contains(StripForMatch(artist))));
}

std::vector<Line> ParseKrc(const QString &krc, const QString &artist, const QString &title) {
	auto result = std::vector<Line>();
	static const auto head = QRegularExpression(uR"(^\[(\d+),(\d+)\])"_q);
	static const auto chunk = QRegularExpression(uR"(<(\d+),(\d+),\d+>([^<]*))"_q);
	for (const auto &raw : krc.split(u'\n')) {
		const auto line = raw.trimmed();
		const auto match = head.match(line);
		if (!match.hasMatch()) {
			continue;
		}
		auto parsed = Line();
		parsed.ms = match.captured(1).toLongLong();
		parsed.endMs = parsed.ms + match.captured(2).toLongLong();
		auto it = chunk.globalMatch(line, match.capturedEnd());
		while (it.hasNext()) {
			const auto m = it.next();
			auto word = Word();
			word.startMs = parsed.ms + m.captured(1).toLongLong();
			word.endMs = word.startMs + m.captured(2).toLongLong();
			word.text = m.captured(3);
			if (!word.text.isEmpty()) {
				AppendWord(parsed.words, std::move(word));
			}
		}
		FixSpacing(parsed);
		for (const auto &word : parsed.words) {
			parsed.text += word.text;
		}
		parsed.text = parsed.text.trimmed();
		if (parsed.text.isEmpty()
			|| IsCreditLine(parsed.text)
			|| (parsed.ms < 15000 && IsHeaderLine(parsed.text, artist, title))) {
			continue;
		}
		result.push_back(std::move(parsed));
	}
	std::sort(result.begin(), result.end(), [](const Line &a, const Line &b) {
		return a.ms < b.ms;
	});
	return result;
}

Data KugouFetch(
		const QString &artist,
		const QString &title,
		int duration,
		int timeoutMs) {
	// WHY: krcs answers a keyword search with nothing now; it finds lyrics only by a song hash.
	auto url = QUrl(u"http://mobilecdn.kugou.com/api/v3/search/song"_q);
	auto q = QUrlQuery();
	q.addQueryItem(u"format"_q, u"json"_q);
	q.addQueryItem(u"keyword"_q, artist.isEmpty() ? title : (artist + u" "_q + title));
	q.addQueryItem(u"page"_q, u"1"_q);
	q.addQueryItem(u"pagesize"_q, u"10"_q);
	q.addQueryItem(u"showtype"_q, u"1"_q);
	url.setQuery(q);
	const auto songs = QJsonDocument::fromJson(GetBytes(url, timeoutMs, false))
		.object().value(u"data"_q).toObject().value(u"info"_q).toArray();
	auto ranked = std::vector<std::pair<int, QJsonObject>>();
	for (const auto &value : songs) {
		const auto song = value.toObject();
		const auto score = MatchScore(
			song.value(u"songname"_q).toString(),
			song.value(u"singername"_q).toString(),
			song.value(u"duration"_q).toDouble(),
			title,
			artist,
			duration);
		if (score > 0) {
			ranked.emplace_back(score, song);
		}
	}
	std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
		return a.first > b.first;
	});
	auto bestId = QString();
	auto bestKey = QString();
	auto bestDuration = 0.;
	for (auto i = 0; i != std::min(int(ranked.size()), 3) && bestId.isEmpty(); ++i) {
		const auto &song = ranked[i].second;
		auto search = QUrl(u"https://krcs.kugou.com/search"_q);
		auto sq = QUrlQuery();
		sq.addQueryItem(u"ver"_q, u"1"_q);
		sq.addQueryItem(u"man"_q, u"yes"_q);
		sq.addQueryItem(u"client"_q, u"mobi"_q);
		sq.addQueryItem(u"hash"_q, song.value(u"hash"_q).toString());
		sq.addQueryItem(
			u"duration"_q,
			QString::number(qint64(song.value(u"duration"_q).toDouble() * 1000)));
		search.setQuery(sq);
		const auto candidates = QJsonDocument::fromJson(GetBytes(search, timeoutMs, false))
			.object().value(u"candidates"_q).toArray();
		if (!candidates.isEmpty()) {
			const auto candidate = candidates.first().toObject();
			bestId = candidate.value(u"id"_q).toVariant().toString();
			bestDuration = song.value(u"duration"_q).toDouble() * 1000.;
			bestKey = candidate.value(u"accesskey"_q).toString();
		}
	}
	if (bestId.isEmpty() || bestKey.isEmpty()) {
		return Data();
	}
	auto download = QUrl(u"https://krcs.kugou.com/download"_q);
	auto dq = QUrlQuery();
	dq.addQueryItem(u"ver"_q, u"1"_q);
	dq.addQueryItem(u"client"_q, u"pc"_q);
	dq.addQueryItem(u"id"_q, bestId);
	dq.addQueryItem(u"accesskey"_q, bestKey);
	dq.addQueryItem(u"fmt"_q, u"krc"_q);
	dq.addQueryItem(u"charset"_q, u"utf8"_q);
	download.setQuery(dq);
	const auto content = QJsonDocument::fromJson(GetBytes(download, timeoutMs, false))
		.object().value(u"content"_q).toString();
	auto data = Data();
	data.lines = ParseKrc(DecodeKrc(content.toLatin1()), artist, title);
	if (data.lines.size() < 4) {
		return Data();
	}
	data.synced = data.wordSynced = true;
	data.hasData = true;
	data.source = u"kugou"_q;
	data.sourceDurationMs = std::int64_t(bestDuration);
	return data;
}

QString DecodeEntities(QString text) {
	static const auto numeric = QRegularExpression(uR"(&#(x?)([0-9a-fA-F]+);)"_q);
	auto result = QString();
	auto last = 0;
	auto it = numeric.globalMatch(text);
	while (it.hasNext()) {
		const auto m = it.next();
		result += text.mid(last, m.capturedStart() - last);
		auto ok = false;
		const auto code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
		if (ok) {
			result += QString::fromUcs4(reinterpret_cast<const char32_t*>(&code), 1);
		}
		last = m.capturedEnd();
	}
	result += text.mid(last);
	result.replace(u"&quot;"_q, u"\""_q);
	result.replace(u"&apos;"_q, u"'"_q);
	result.replace(u"&lt;"_q, u"<"_q);
	result.replace(u"&gt;"_q, u">"_q);
	result.replace(u"&nbsp;"_q, u" "_q);
	result.replace(u"&amp;"_q, u"&"_q);
	return result;
}

int MatchingDivEnd(const QString &html, int from) {
	auto depth = 0;
	auto at = from;
	while (at < html.size()) {
		const auto open = html.indexOf(u"<div"_q, at);
		const auto close = html.indexOf(u"</div>"_q, at);
		if (close < 0) {
			return -1;
		} else if (open >= 0 && open < close) {
			++depth;
			at = open + 4;
		} else {
			--depth;
			at = close + 6;
			if (!depth) {
				return at;
			}
		}
	}
	return -1;
}

QString GeniusText(const QString &html) {
	auto blocks = QString();
	auto at = 0;
	while (true) {
		const auto marker = html.indexOf(u"data-lyrics-container=\"true\""_q, at);
		if (marker < 0) {
			break;
		}
		const auto start = html.lastIndexOf(u"<div"_q, marker);
		const auto end = (start >= 0) ? MatchingDivEnd(html, start) : -1;
		if (end < 0) {
			break;
		}
		auto block = html.mid(start, end - start);
		while (true) {
			const auto exclude = block.indexOf(u"data-exclude-from-selection=\"true\""_q);
			if (exclude < 0) {
				break;
			}
			const auto from = block.lastIndexOf(u"<div"_q, exclude);
			const auto till = (from >= 0) ? MatchingDivEnd(block, from) : -1;
			if (till < 0) {
				break;
			}
			block.remove(from, till - from);
		}
		blocks += block + u"\n"_q;
		at = end;
	}
	if (blocks.isEmpty()) {
		return QString();
	}
	static const auto br = QRegularExpression(uR"(<br\s*/?>)"_q, QRegularExpression::CaseInsensitiveOption);
	static const auto tags = QRegularExpression(uR"(<[^>]+>)"_q);
	blocks.replace(br, u"\n"_q);
	blocks.replace(tags, QString());
	blocks = DecodeEntities(blocks);
	auto lines = QStringList();
	for (const auto &row : blocks.split(u'\n')) {
		const auto text = row.trimmed();
		if (text.startsWith(QChar(u'[')) && text.endsWith(QChar(u']'))) {
			continue;
		}
		lines.push_back(text);
	}
	return lines.join(u'\n').trimmed();
}

QString WithoutParentheses(const QString &title) {
	static const auto groups = QRegularExpression(uR"(\s*[\(\[][^\)\]]*[\)\]])"_q);
	auto result = title;
	result.replace(groups, QString());
	return result.trimmed();
}

QString GeniusFetch(
		const QString &artist,
		const QString &title,
		int timeoutMs) {
	auto url = QUrl(u"https://genius.com/api/search/song"_q);
	auto q = QUrlQuery();
	q.addQueryItem(u"q"_q, artist.isEmpty() ? title : (artist + u" "_q + title));
	q.addQueryItem(u"per_page"_q, u"5"_q);
	url.setQuery(q);
	const auto doc = QJsonDocument::fromJson(GetBytes(url, timeoutMs, false));
	auto bestScore = 0;
	auto bestUrl = QString();
	const auto sections = doc.object().value(u"response"_q).toObject().value(u"sections"_q).toArray();
	for (const auto &section : sections) {
		for (const auto &hit : section.toObject().value(u"hits"_q).toArray()) {
			const auto result = hit.toObject().value(u"result"_q).toObject();
			const auto songTitle = result.value(u"title"_q).toString();
			const auto songArtist = result.value(u"artist_names"_q).toString();
			const auto score = std::max(
				MatchScore(songTitle, songArtist, 0., title, artist, 0),
				MatchScore(WithoutParentheses(songTitle), songArtist, 0., title, artist, 0));
			if (score > bestScore && !artist.isEmpty() && ArtistMatches(songArtist, artist)) {
				bestScore = score;
				bestUrl = result.value(u"url"_q).toString();
			}
		}
	}
	if (bestUrl.isEmpty()) {
		return QString();
	}
	return GeniusText(QString::fromUtf8(GetBytes(QUrl(bestUrl), timeoutMs, false)));
}

QString OvhFetch(const QString &artist, const QString &title, int timeoutMs) {
	if (artist.isEmpty()) {
		return QString();
	}
	auto url = QUrl(u"https://api.lyrics.ovh/v1/"_q);
	url.setPath(u"/v1/"_q + artist + u"/"_q + title);
	return QJsonDocument::fromJson(GetBytes(url, timeoutMs, false))
		.object().value(u"lyrics"_q).toString().trimmed();
}

Data PlainData(const QString &plain, const QString &source) {
	if (plain.count(QChar(u'\n')) < 3) {
		return Data();
	}
	auto data = FromLrcText(QString(), plain);
	data.source = source;
	return data;
}

QSet<QString> WordSet(const std::vector<Line> &lines) {
	auto result = QSet<QString>();
	for (const auto &line : lines) {
		for (const auto &token : line.text.split(QChar(u' '), Qt::SkipEmptyParts)) {
			const auto word = NormalizedWord(token);
			if (word.size() >= 3) {
				result.insert(word);
			}
		}
	}
	return result;
}

Data LrcCxDonor(
		const QString &artist,
		const QString &title,
		const Data &plain,
		int timeoutMs) {
	auto url = QUrl(u"https://api.lrc.cx/lyrics"_q);
	auto q = QUrlQuery();
	q.addQueryItem(u"title"_q, title);
	if (!artist.isEmpty()) {
		q.addQueryItem(u"artist"_q, artist);
	}
	url.setQuery(q);
	const auto text = QString::fromUtf8(GetBytes(url, timeoutMs, false));
	if (!text.trimmed().startsWith(QChar(u'['))) {
		return Data();
	}
	auto donor = FromLrcText(text, QString());
	if (!donor.synced || donor.lines.size() < 4) {
		return Data();
	}
	const auto mine = WordSet(plain.lines);
	const auto theirs = WordSet(donor.lines);
	if (mine.isEmpty() || theirs.isEmpty()) {
		return Data();
	}
	auto shared = 0;
	for (const auto &word : theirs) {
		shared += mine.contains(word) ? 1 : 0;
	}
	// WHY: lrc.cx answers with the closest song it knows, often a different one,
	// so its timing is only borrowed when the words agree with lyrics found elsewhere.
	if (shared < theirs.size() * 0.6 || shared < mine.size() * 0.5) {
		return Data();
	}
	donor.source = u"lrc.cx"_q;
	return donor;
}

} // namespace

std::vector<Line> ParseLrc(const QString &lrc) {
	auto out = std::vector<Line>();
	static const auto offsetTag = QRegularExpression(uR"(\[offset:\s*([+-]?\d+)\s*\])"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto offsetMatch = offsetTag.match(lrc);
	// Positive LRC offset means lyrics should appear earlier.
	const auto offset = offsetMatch.hasMatch() ? offsetMatch.captured(1).toInt() : 0;
	static const auto stamp = QRegularExpression(
		uR"(\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\])"_q);
	static const auto inner = QRegularExpression(
		uR"(<(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?>)"_q);
	auto gaps = std::vector<std::int64_t>();
	for (const auto &raw : lrc.split(u'\n')) {
		const auto line = QString(raw).remove(QChar(0xFEFF)).trimmed();
		auto it = stamp.globalMatch(line);
		auto stamps = std::vector<std::int64_t>();
		auto lastEnd = 0;
		while (it.hasNext()) {
			const auto m = it.next();
			if (m.capturedStart() != lastEnd) {
				break;
			}
			stamps.push_back(ParseStamp(m.captured(1), m.captured(2), m.captured(3)));
			lastEnd = m.capturedEnd();
		}
		if (stamps.empty()) {
			continue;
		}
		const auto body = line.mid(lastEnd);
		auto words = std::vector<Word>();
		auto plain = QString();
		auto wit = inner.globalMatch(body);
		if (wit.hasNext()) {
			auto matches = std::vector<QRegularExpressionMatch>();
			while (wit.hasNext()) {
				matches.push_back(wit.next());
			}
			for (auto i = 0; i != int(matches.size()); ++i) {
				const auto &m = matches[i];
				const auto from = m.capturedEnd();
				const auto till = (i + 1 < int(matches.size()))
					? matches[i + 1].capturedStart()
					: body.size();
				const auto text = body.mid(from, till - from);
				const auto at = ParseStamp(m.captured(1), m.captured(2), m.captured(3));
				if (!words.empty() && words.back().endMs <= words.back().startMs) {
					words.back().endMs = at;
				}
				if (!text.trimmed().isEmpty()) {
					words.push_back(Word{ .startMs = at, .endMs = at, .text = text });
				}
				plain += text;
			}
			if (!words.empty() && words.back().endMs <= words.back().startMs) {
				words.back().endMs = words.back().startMs + 500;
			}
		} else {
			plain = body;
		}
		plain = plain.trimmed();
		if (plain.isEmpty()) {
			gaps.insert(gaps.end(), stamps.begin(), stamps.end());
			continue;
		}
		if (plain.startsWith(QChar(u'[')) || IsCreditLine(plain)) {
			continue;
		}
		for (const auto ms : stamps) {
			auto parsed = Line{ .ms = ms, .text = plain };
			if (!words.empty() && stamps.size() == 1) {
				parsed.words = words;
				parsed.endMs = words.back().endMs;
			}
			out.push_back(std::move(parsed));
		}
	}
	for (auto &line : out) {
		line.ms = std::max<std::int64_t>(0, line.ms - offset);
		if (line.endMs > 0) line.endMs = std::max(line.ms, line.endMs - offset);
		for (auto &word : line.words) {
			word.startMs = std::max<std::int64_t>(0, word.startMs - offset);
			word.endMs = std::max(word.startMs, word.endMs - offset);
		}
	}
	for (auto &gap : gaps) gap = std::max<std::int64_t>(0, gap - offset);
	std::stable_sort(out.begin(), out.end(), [](const Line &a, const Line &b) {
		return a.ms < b.ms;
	});
	out.erase(std::unique(out.begin(), out.end(), [](const Line &a, const Line &b) {
		return a.ms == b.ms && a.text == b.text;
	}), out.end());
	std::sort(gaps.begin(), gaps.end());
	for (auto i = 0; i != int(out.size()); ++i) {
		const auto next = (i + 1 < int(out.size())) ? out[i + 1].ms : std::int64_t(-1);
		const auto gap = std::upper_bound(gaps.begin(), gaps.end(), out[i].ms);
		if (gap != gaps.end() && (next < 0 || *gap < next) && out[i].endMs <= out[i].ms) {
			out[i].endMs = *gap;
		}
	}
	return out;
}

Data ParseTimedText(const QString &text) {
	auto data = FromLrcText(text, QString());
	if (!data.hasData) {
		static const auto cue = QRegularExpression(
			uR"(^\s*((?:\d{1,3}:)?\d{2}:\d{2}[.,]\d{3})\s+-->\s+((?:\d{1,3}:)?\d{2}:\d{2}[.,]\d{3}))"_q);
		const auto milliseconds = [](QString stamp) {
			stamp.replace(QChar(u','), QChar(u'.'));
			const auto parts = stamp.split(QChar(u':'));
			auto seconds = 0.;
			for (const auto &part : parts) seconds = seconds * 60. + part.toDouble();
			return std::int64_t(std::llround(seconds * 1000.));
		};
		auto rows = text.split(QChar(u'\n'));
		for (auto i = 0; i < rows.size(); ++i) {
			const auto match = cue.match(rows[i]);
			if (!match.hasMatch()) continue;
			auto line = Line();
			line.ms = milliseconds(match.captured(1));
			line.endMs = milliseconds(match.captured(2));
			while (i + 1 < rows.size() && !rows[i + 1].trimmed().isEmpty()) {
				line.text += (line.text.isEmpty() ? QString() : u" "_q) + rows[++i].trimmed();
			}
			static const auto markup = QRegularExpression(uR"(<[^>]*>)"_q);
			line.text = DecodeEntities(line.text.remove(markup)).trimmed();
			if (!line.text.isEmpty() && line.endMs > line.ms) data.lines.push_back(std::move(line));
		}
		std::stable_sort(data.lines.begin(), data.lines.end(), [](const Line &a, const Line &b) { return a.ms < b.ms; });
		data.hasData = data.synced = !data.lines.empty();
	}
	Annotate(data);
	return data;
}

Data LoadSidecar(const QString &audioPath) {
	if (audioPath.isEmpty()) return {};
	const auto info = QFileInfo(audioPath);
	const auto base = info.absolutePath() + QChar(u'/') + info.completeBaseName();
	for (const auto &extension : { u".lrc"_q, u".srt"_q, u".vtt"_q }) {
		auto file = QFile(base + extension);
		if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) continue;
		auto result = ParseTimedText(QString::fromUtf8(file.readAll()));
		if (result.hasData) {
			result.source = u"local"_q + extension;
			return result;
		}
	}
	return {};
}

Data FetchExact(
	const QString &wantArtist,
	const QString &wantTitle,
	int durationSec,
	int timeoutMs);

Data FetchSync(const QString &artist, const QString &title, int durationSec, int timeoutMs) {
	auto wantArtist = CleanedArtist(artist);
	if (wantArtist.isEmpty()) {
		wantArtist = artist.trimmed();
	}
	auto wantTitle = CleanedTitle(title);
	if (wantArtist.isEmpty()) {
		wantArtist = ProdArtistFromTitle(title);
		const auto dash = wantTitle.indexOf(u" - "_q);
		if (wantArtist.isEmpty() && dash > 0) {
			wantArtist = wantTitle.left(dash).trimmed();
			wantTitle = wantTitle.mid(dash + 3).trimmed();
		}
	}
	if (wantTitle.isEmpty()) {
		return Data();
	}
	auto result = FetchExact(wantArtist, wantTitle, durationSec, timeoutMs);
	if (!result.synced && wantArtist.isEmpty()) {
		static const auto separators = QRegularExpression(uR"([\s_]+)"_q);
		const auto words = wantTitle.split(separators, Qt::SkipEmptyParts);
		for (auto k = 1; k < std::min(int(words.size()), 4); ++k) {
			auto attempt = FetchExact(
				words.mid(0, k).join(QChar(u' ')),
				words.mid(k).join(QChar(u' ')),
				durationSec,
				timeoutMs);
			if (attempt.synced || (attempt.hasData && !result.hasData)) {
				result = std::move(attempt);
			}
			if (result.synced) {
				break;
			}
		}
	}
	return result;
}

Data FetchExact(
		const QString &wantArtist,
		const QString &wantTitle,
		int durationSec,
		int timeoutMs) {
	auto result = Data();
	// The three timed catalogues answer in parallel, so a track waits for the slowest one only.
	auto lrclibTask = std::async(std::launch::async, [=] {
		return BestLrclib(wantArtist, wantTitle, durationSec, timeoutMs);
	});
	auto kugouTask = std::async(std::launch::async, [=] {
		return KugouFetch(wantArtist, wantTitle, durationSec, timeoutMs);
	});
	auto neteaseTask = std::async(std::launch::async, [=] {
		return NeteaseFetch(wantArtist, wantTitle, durationSec, timeoutMs);
	});
	const auto lrclib = lrclibTask.get();
	const auto lrclibSynced = lrclib && !lrclib->synced.trimmed().isEmpty();
	if (lrclibSynced) {
		result = FromLrcText(lrclib->synced, QString());
		result.source = u"lrclib"_q;
		result.sourceDurationMs = std::int64_t(lrclib->duration * 1000.);
	}
	auto kugou = kugouTask.get();
	auto netease = neteaseTask.get();
	if (!result.wordSynced) {
		if (kugou.hasData
			&& (!result.synced
				|| kugou.lines.size() * 10 >= result.lines.size() * 7)) {
			result = std::move(kugou);
		}
	}
	if (!result.wordSynced) {
		if (netease.hasData
			&& (netease.wordSynced || !result.synced)
			&& (!result.synced
				|| netease.lines.size() * 10 >= result.lines.size() * 7)) {
			result = std::move(netease);
		}
	}
	if (!result.synced && !wantArtist.isEmpty()) {
		// WHY: a same-tempo cut keeps timing up to a shift AlignOffset finds; sped-up cuts keep words only.
		auto candidates = LrclibSearch(wantArtist + u" "_q + wantTitle, timeoutMs);
		if (candidates.empty()) {
			candidates = LrclibSearch(wantTitle, timeoutMs);
		}
		auto best = (const Candidate*)nullptr;
		auto plainText = QString();
		for (const auto &c : candidates) {
			if (!TitleMatches(c.title, wantTitle) || !ArtistMatches(c.artist, wantArtist)) {
				continue;
			}
			const auto ratio = (durationSec > 0 && c.duration > 0.) ? (c.duration / durationSec) : 1.;
			if (!c.synced.trimmed().isEmpty() && std::abs(ratio - 1.) <= 0.06
				&& (!best || std::abs(c.duration - durationSec) < std::abs(best->duration - durationSec))) {
				best = &c;
			}
			if (plainText.isEmpty()) {
				plainText = c.plain.trimmed();
				if (plainText.isEmpty()) {
					auto rows = QStringList();
					for (const auto &line : ParseLrc(c.synced)) {
						rows.push_back(line.text);
					}
					plainText = rows.join(QChar(u'\n'));
				}
			}
		}
		if (best) {
			auto other = FromLrcText(best->synced, QString());
			if (other.synced) {
				other.source = u"lrclib (другая версия)"_q;
				other.sourceDurationMs = std::int64_t(best->duration * 1000.);
				result = std::move(other);
			}
		} else if (!result.hasData) {
			result = PlainData(plainText, u"lrclib (другая версия)"_q);
		}
	}
	if (!result.hasData && lrclib) {
		result = FromLrcText(QString(), lrclib->plain);
		result.source = u"lrclib"_q;
	}
	if (!result.hasData) {
		auto genius = std::async(std::launch::async, [=] {
			return GeniusFetch(wantArtist, wantTitle, timeoutMs);
		});
		auto ovh = std::async(std::launch::async, [=] {
			return OvhFetch(wantArtist, wantTitle, timeoutMs);
		});
		result = PlainData(genius.get(), u"genius"_q);
		const auto fallback = ovh.get();
		if (!result.hasData) {
			result = PlainData(fallback, u"lyrics.ovh"_q);
		}
	}
	if (result.hasData && !result.synced) {
		auto donor = LrcCxDonor(wantArtist, wantTitle, result, timeoutMs);
		if (donor.hasData) {
			result = std::move(donor);
		}
	}
	result.artist = wantArtist;
	result.title = wantTitle;
	Annotate(result);
	return result;
}

namespace {

constexpr auto kDiskCacheVersion = 1;

QString DiskCachePath(const QString &key) {
	const auto hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
	return cWorkingDir() + u"tdata/whitegram/lyrics/"_q + QString::fromLatin1(hash) + u".json"_q;
}

QJsonObject SerializeData(const Data &data) {
	auto lines = QJsonArray();
	for (const auto &line : data.lines) {
		auto words = QJsonArray();
		for (const auto &word : line.words) {
			words.push_back(QJsonArray{ double(word.startMs), double(word.endMs), word.text });
		}
		lines.push_back(QJsonObject{
			{ u"ms"_q, double(line.ms) },
			{ u"end"_q, double(line.endMs) },
			{ u"text"_q, line.text },
			{ u"words"_q, words },
		});
	}
	return QJsonObject{
		{ u"version"_q, kDiskCacheVersion },
		{ u"synced"_q, data.synced },
		{ u"wordSynced"_q, data.wordSynced },
		{ u"artist"_q, data.artist },
		{ u"title"_q, data.title },
		{ u"source"_q, data.source },
		{ u"plain"_q, data.plain },
		{ u"sourceDuration"_q, double(data.sourceDurationMs) },
		{ u"lines"_q, lines },
	};
}

std::optional<Data> DeserializeData(const QJsonObject &object) {
	if (object.value(u"version"_q).toInt() != kDiskCacheVersion) {
		return std::nullopt;
	}
	auto data = Data();
	data.synced = object.value(u"synced"_q).toBool();
	data.wordSynced = object.value(u"wordSynced"_q).toBool();
	data.artist = object.value(u"artist"_q).toString();
	data.title = object.value(u"title"_q).toString();
	data.source = object.value(u"source"_q).toString();
	data.plain = object.value(u"plain"_q).toString();
	data.sourceDurationMs = std::int64_t(object.value(u"sourceDuration"_q).toDouble());
	for (const auto &value : object.value(u"lines"_q).toArray()) {
		const auto o = value.toObject();
		auto line = Line();
		line.ms = std::int64_t(o.value(u"ms"_q).toDouble());
		line.endMs = std::int64_t(o.value(u"end"_q).toDouble());
		line.text = o.value(u"text"_q).toString();
		for (const auto &item : o.value(u"words"_q).toArray()) {
			const auto w = item.toArray();
			if (w.size() == 3) {
				line.words.push_back(Word{
					.startMs = std::int64_t(w[0].toDouble()),
					.endMs = std::int64_t(w[1].toDouble()),
					.text = w[2].toString(),
				});
			}
		}
		data.lines.push_back(std::move(line));
	}
	data.hasData = !data.lines.empty();
	if (!data.hasData) {
		return std::nullopt;
	}
	// Keywords and repeats are derived, so they are rebuilt instead of stored.
	Annotate(data);
	return data;
}

} // namespace

Data FetchCachedSync(const QString &artist, const QString &title, int durationSec) {
	const auto key = CacheKey(artist, title, durationSec);
	{
		const auto lock = QMutexLocker(&CacheMutex());
		if (const auto hit = Cache().object(key)) {
			return *hit;
		}
	}
	const auto path = DiskCachePath(key);
	auto file = QFile(path);
	if (file.open(QIODevice::ReadOnly)) {
		if (auto stored = DeserializeData(QJsonDocument::fromJson(file.readAll()).object())) {
			const auto lock = QMutexLocker(&CacheMutex());
			Cache().insert(key, new Data(*stored));
			return std::move(*stored);
		}
	}
	auto data = FetchSync(artist, title, durationSec);
	// Misses stay in memory only, so a track gets a fresh search after a restart.
	if (data.hasData) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		auto out = QFile(path);
		if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			out.write(QJsonDocument(SerializeData(data)).toJson(QJsonDocument::Compact));
		}
	}
	const auto lock = QMutexLocker(&CacheMutex());
	Cache().insert(key, new Data(data));
	return data;
}

int ActiveLineIndex(const Data &data, std::int64_t positionMs) {
	if (!data.hasData || data.lines.empty() || !data.synced) {
		return -1;
	}
	const auto it = std::upper_bound(
		data.lines.begin(),
		data.lines.end(),
		positionMs,
		[](std::int64_t ms, const Line &line) { return ms < line.ms; });
	return int(it - data.lines.begin()) - 1;
}

double LineProgress(const Data &data, int index, std::int64_t positionMs) {
	if (!data.synced || index < 0 || index >= int(data.lines.size())) {
		return -1.;
	}
	const auto &line = data.lines[index];
	if (line.endMs <= line.ms) {
		return 1.;
	}
	return std::clamp(
		double(positionMs - line.ms) / double(line.endMs - line.ms),
		0.,
		1.);
}

double WordProgress(const Word &word, std::int64_t positionMs) {
	if (positionMs <= word.startMs) {
		return 0.;
	} else if (positionMs >= word.endMs || word.endMs <= word.startMs) {
		return 1.;
	}
	return double(positionMs - word.startMs) / double(word.endMs - word.startMs);
}

std::vector<std::pair<std::int64_t, std::int64_t>> RepeatedBlocks(
		const Data &data) {
	auto result = std::vector<std::pair<std::int64_t, std::int64_t>>();
	if (!data.synced) {
		return result;
	}
	const auto count = int(data.lines.size());
	for (auto i = 0; i != count;) {
		if (!data.lines[i].repeated) {
			++i;
			continue;
		}
		auto till = i;
		while (till + 1 < count && data.lines[till + 1].repeated) {
			++till;
		}
		const auto start = data.lines[i].ms;
		const auto end = std::max(data.lines[till].endMs, data.lines[till].ms + 1000);
		if (till > i || end - start >= 6000) {
			result.emplace_back(start, end);
		}
		i = till + 1;
	}
	return result;
}

int Syllables(const QString &word) {
	return SyllableCount(word);
}

QString NormalizedWord(const QString &word) {
	auto result = QString();
	result.reserve(word.size());
	for (const auto ch : word.toLower()) {
		if (ch.isLetterOrNumber()) {
			result += (ch == QChar(u'ё')) ? QChar(u'е') : ch;
		}
	}
	return result;
}

} // namespace Ayu::Ui::Lyrics
