// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#include "ayu/ui/utils/lyrics_engine.h"

#include <QtCore/QCache>
#include <QtCore/QEventLoop>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <cmath>

namespace Ayu::Ui::Lyrics {
namespace {

QCache<QString, Data> &cache() {
	static QCache<QString, Data> c(100);
	return c;
}

QString cacheKey(const QString &artist, const QString &title) {
	return artist.trimmed().toLower()
		+ QString::fromUtf8(" - ")
		+ title.trimmed().toLower();
}

QByteArray getBytes(const QUrl &url, int timeoutMs) {
	QNetworkAccessManager nam;
	QNetworkRequest req(url);
	req.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
	req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromUtf8("WhitegramDesktop/1.0"));
#endif
	QNetworkReply *reply = nam.get(req);
	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	timer.start(timeoutMs);
	loop.exec();
	const auto timedOut = !timer.isActive();
	timer.stop();
	auto guard = std::unique_ptr<QNetworkReply, void(*)(QNetworkReply*)>(
		reply, [](QNetworkReply *r) { if (r) r->deleteLater(); });
	if (timedOut) {
		return {};
	}
	if (reply->error() != QNetworkReply::NoError) {
		return {};
	}
	return reply->readAll();
}

std::int64_t parseStamp(const QString &mm, const QString &ss, const QString &frac) {
	const auto minutes = mm.toInt();
	const auto seconds = ss.toInt();
	auto ms = std::int64_t(minutes * 60 + seconds) * 1000;
	if (!frac.isEmpty()) {
		auto f = frac;
		while (f.size() < 3) f += QChar(u'0');
		if (f.size() > 3) f = f.left(3);
		ms += f.toInt();
	}
	return ms;
}

} // namespace

std::vector<Line> ParseLrc(const QString &lrc) {
	auto out = std::vector<Line>();
	if (lrc.isEmpty()) {
		return out;
	}
	static const auto re = QRegularExpression(
		QString::fromUtf8(R"(\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\])"));
	for (const auto &raw : lrc.split(u'\n')) {
		const auto line = raw.trimmed();
		if (line.isEmpty()) {
			continue;
		}
		auto it = re.globalMatch(line);
		auto stamps = std::vector<std::int64_t>();
		auto lastEnd = 0;
		while (it.hasNext()) {
			const auto m = it.next();
			stamps.push_back(parseStamp(m.captured(1), m.captured(2), m.captured(3)));
			lastEnd = m.capturedEnd();
		}
		if (stamps.empty()) {
			continue;
		}
		const auto text = line.mid(lastEnd).trimmed();
		if (text.isEmpty()) {
			continue;
		}
		// Skip metadata tags like [ar:...] [ti:...] [length:...].
		if (text.startsWith(QChar(u'['))) {
			continue;
		}
		for (const auto ms : stamps) {
			out.push_back(Line{ ms, text });
		}
	}
	std::sort(out.begin(), out.end(), [](const Line &a, const Line &b) {
		return a.ms < b.ms;
	});
	// Drop exact duplicates.
	auto cleaned = std::vector<Line>();
	cleaned.reserve(out.size());
	for (const auto &l : out) {
		if (!cleaned.empty()
			&& cleaned.back().ms == l.ms
			&& cleaned.back().text == l.text) {
			continue;
		}
		cleaned.push_back(l);
	}
	return cleaned;
}

Data FetchSync(const QString &artist, const QString &title, int durationSec, int timeoutMs) {
	auto data = Data();
	data.artist = artist.trimmed();
	data.title = title.trimmed();
	if (data.artist.isEmpty() && data.title.isEmpty()) {
		return data;
	}
	QUrl url(QString::fromUtf8("https://lrclib.net/api/get"));
	QUrlQuery q;
	q.addQueryItem(QString::fromUtf8("artist_name"), data.artist);
	q.addQueryItem(QString::fromUtf8("track_name"), data.title);
	if (durationSec > 0) {
		q.addQueryItem(
			QString::fromUtf8("duration"),
			QString::number(durationSec));
	}
	url.setQuery(q);

	const auto bytes = getBytes(url, timeoutMs);
	if (bytes.isEmpty()) {
		return data;
	}
	const auto doc = QJsonDocument::fromJson(bytes);
	if (!doc.isObject()) {
		return data;
	}
	const auto o = doc.object();
	// lrclib returns 404 JSON with statusCode when not found.
	if (o.value(QString::fromUtf8("statusCode")).toInt(200) == 404) {
		return data;
	}
	const auto synced = o.value(QString::fromUtf8("syncedLyrics")).toString();
	const auto plain = o.value(QString::fromUtf8("plainLyrics")).toString();
	data.source = QString::fromUtf8("lrclib");
	if (!synced.trimmed().isEmpty()) {
		data.lines = ParseLrc(synced);
		data.synced = !data.lines.empty();
	}
	if (!data.synced && !plain.trimmed().isEmpty()) {
		data.plain = plain.trimmed();
		// Convert plain text into untimed lines so the UI can still
		// autoscroll them softly by track progress.
		data.lines.clear();
		for (const auto &row : data.plain.split(u'\n')) {
			const auto t = row.trimmed();
			if (!t.isEmpty()) {
				data.lines.push_back(Line{ -1, t });
			}
		}
	}
	data.hasData = data.synced || !data.plain.isEmpty();
	if (!data.hasData) {
		data.lines.clear();
	}
	return data;
}

Data FetchCachedSync(const QString &artist, const QString &title, int durationSec) {
	const auto key = cacheKey(artist, title);
	if (const auto *hit = cache().object(key)) {
		return *hit;
	}
	auto data = FetchSync(artist, title, durationSec);
	// Cache negatives too (short-lived enough via LRU) to avoid
	// hammering the API when switching tracks back and forth.
	cache().insert(key, new Data(data));
	return data;
}

int ActiveLineIndex(const Data &data, std::int64_t positionMs) {
	if (!data.hasData || data.lines.empty()) {
		return -1;
	}
	if (!data.synced) {
		// Untimed fallback: spread lines evenly by caller-provided progress.
		// Caller maps positionMs<0 to "not started".
		return -1;
	}
	auto index = -1;
	for (auto i = 0; i < int(data.lines.size()); ++i) {
		if (data.lines[i].ms <= positionMs) {
			index = i;
		} else {
			break;
		}
	}
	return index;
}

double LineProgress(const Data &data, int index, std::int64_t positionMs) {
	if (!data.synced
		|| index < 0
		|| index >= int(data.lines.size())) {
		return -1.;
	}
	const auto start = data.lines[index].ms;
	const auto end = (index + 1 < int(data.lines.size()))
		? data.lines[index + 1].ms
		: start + 3000;
	if (end <= start) {
		return 1.;
	}
	return std::clamp(double(positionMs - start) / double(end - start), 0., 1.);
}

} // namespace Ayu::Ui::Lyrics
