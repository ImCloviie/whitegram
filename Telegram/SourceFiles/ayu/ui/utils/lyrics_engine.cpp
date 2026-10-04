// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#include "ayu/ui/utils/lyrics_engine.h"

#include <QtCore/QCache>
#include <QtCore/QEventLoop>
#include <QtCore/QJsonArray>
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

namespace {

// Strip " (prod. X)", " [prod. X]", " (feat. Y)" tails that break exact match.
QString cleanedTitle(const QString &title) {
    auto out = title.trimmed();
    static const auto prodRe = QRegularExpression(
        QString::fromUtf8(R"(\s*[\(\[]\s*(prod\.?|producer|by|feat\.?|ft\.?)\b[^\(\)\[\]]*[\)\]])"),
        QRegularExpression::CaseInsensitiveOption);
    out = out.replace(prodRe, QString()).trimmed();
    // "Title - prod. X" style tails.
    static const auto tailRe = QRegularExpression(
        QString::fromUtf8(R"(\s+[-–—]\s*(prod\.?|producer)\b.*$)"),
        QRegularExpression::CaseInsensitiveOption);
    out = out.replace(tailRe, QString()).trimmed();
    return out;
}

// "Every Time (prod. LAZZY2WICE)" -> "LAZZY2WICE".
QString prodArtistFromTitle(const QString &title) {
    static const auto re = QRegularExpression(
        QString::fromUtf8(R"([\(\[]\s*prod\.?\s+([^\)\]]+)[\)\]])"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(title);
    if (m.hasMatch()) {
        return m.captured(1).trimmed();
    }
    static const auto preRe = QRegularExpression(
        QString::fromUtf8(R"(^\s*prod\.?\s+([^-–—\(\[]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m2 = preRe.match(title);
    return m2.hasMatch() ? m2.captured(1).trimmed() : QString();
}

struct SearchHit {
    QString trackName;
    QString artistName;
    QString synced;
    QString plain;
    double duration = 0.;
};

std::vector<SearchHit> parseSearch(const QByteArray &json) {
    auto out = std::vector<SearchHit>();
    const auto doc = QJsonDocument::fromJson(json);
    if (doc.isArray()) {
        for (const auto &v : doc.array()) {
            const auto o = v.toObject();
            auto h = SearchHit();
            h.trackName = o.value(QString::fromUtf8("trackName")).toString();
            h.artistName = o.value(QString::fromUtf8("artistName")).toString();
            h.synced = o.value(QString::fromUtf8("syncedLyrics")).toString();
            h.plain = o.value(QString::fromUtf8("plainLyrics")).toString();
            h.duration = o.value(QString::fromUtf8("duration")).toDouble();
            if (!h.synced.trimmed().isEmpty() || !h.plain.trimmed().isEmpty()) {
                out.push_back(std::move(h));
            }
        }
    }
    return out;
}

QString norm(const QString &s) {
    return s.trimmed().toLower();
}

int scoreHit(
        const SearchHit &h,
        const QString &wantTitle,
        const QString &wantArtist,
        int durationSec) {
    int score = 0;
    if (!h.synced.trimmed().isEmpty()) {
        score += 10;
    } else if (!h.plain.trimmed().isEmpty()) {
        score += 3;
    } else {
        return 0;
    }
    const auto ht = norm(h.trackName);
    const auto wt = norm(wantTitle);
    if (!wt.isEmpty()) {
        if (ht == wt) score += 6;
        else if (ht.contains(wt) || wt.contains(ht)) score += 2;
    }
    const auto ha = norm(h.artistName);
    const auto wa = norm(wantArtist);
    if (!wa.isEmpty() && !ha.isEmpty()) {
        if (ha == wa) score += 6;
        else if (ha.contains(wa) || wa.contains(ha)) score += 2;
    }
    if (durationSec > 0 && h.duration > 0.) {
        const auto diff = std::abs(h.duration - double(durationSec));
        if (diff <= 10.) score += 3;
        else if (diff <= 25.) score += 1;
        else score -= 2;
    }
    return score;
}

Data dataFromHit(SearchHit h, const QString &artist, const QString &title) {
    auto data = Data();
    data.artist = artist.trimmed();
    data.title = title.trimmed();
    data.source = QString::fromUtf8("lrclib-search");
    if (!h.synced.trimmed().isEmpty()) {
        data.lines = ParseLrc(h.synced);
        data.synced = !data.lines.empty();
    }
    if (!data.synced && !h.plain.trimmed().isEmpty()) {
        data.plain = h.plain.trimmed();
        for (const auto &row : data.plain.split(u'\n')) {
            const auto tt = row.trimmed();
            if (!tt.isEmpty()) {
                data.lines.push_back(Line{ -1, tt });
            }
        }
    }
    data.hasData = data.synced || !data.plain.isEmpty();
    if (!data.hasData) {
        data.lines.clear();
    }
    return data;
}

QByteArray searchRaw(const QString &query, int timeoutMs) {
    QUrl url(QString::fromUtf8("https://lrclib.net/api/search"));
    QUrlQuery q;
    q.addQueryItem(QString::fromUtf8("q"), query);
    url.setQuery(q);
    return getBytes(url, timeoutMs);
}

} // namespace

Data FetchSync(const QString &artist, const QString &title, int durationSec, int timeoutMs) {
	auto data = Data();
	data.artist = artist.trimmed();
	data.title = title.trimmed();
	if (data.artist.isEmpty() && data.title.isEmpty()) {
		return data;
	}

	const auto tryGet = [&](const QString &a, const QString &t) -> bool {
		QUrl url(QString::fromUtf8("https://lrclib.net/api/get"));
		QUrlQuery q;
		q.addQueryItem(QString::fromUtf8("artist_name"), a);
		q.addQueryItem(QString::fromUtf8("track_name"), t);
		if (durationSec > 0) {
			q.addQueryItem(
				QString::fromUtf8("duration"),
				QString::number(durationSec));
		}
		url.setQuery(q);
		const auto bytes = getBytes(url, timeoutMs);
		if (bytes.isEmpty()) {
			return false;
		}
		const auto doc = QJsonDocument::fromJson(bytes);
		if (!doc.isObject()) {
			return false;
		}
		const auto o = doc.object();
		// lrclib returns 404 JSON with statusCode when not found.
		if (o.value(QString::fromUtf8("statusCode")).toInt(200) == 404) {
			return false;
		}
		const auto synced = o.value(QString::fromUtf8("syncedLyrics")).toString();
		const auto plain = o.value(QString::fromUtf8("plainLyrics")).toString();
		if (synced.trimmed().isEmpty() && plain.trimmed().isEmpty()) {
			return false;
		}
		data.source = QString::fromUtf8("lrclib");
		if (!synced.trimmed().isEmpty()) {
			data.lines = ParseLrc(synced);
			data.synced = !data.lines.empty();
		}
		if (!data.synced && !plain.trimmed().isEmpty()) {
			data.plain = plain.trimmed();
			// Convert plain text into untimed lines so the UI can still
			// autoscroll them softly by track progress.
			for (const auto &row : data.plain.split(u'\n')) {
				const auto tt = row.trimmed();
				if (!tt.isEmpty()) {
					data.lines.push_back(Line{ -1, tt });
				}
			}
		}
		data.hasData = data.synced || !data.plain.isEmpty();
		if (!data.hasData) {
			data.lines.clear();
		}
		return data.hasData;
	};

	// 1. Exact query as tagged.
	if (tryGet(data.artist, data.title)) {
		return data;
	}
	// 2. Cleaned title ("Every Time (prod. X)" -> "Every Time").
	const auto cleaned = cleanedTitle(data.title);
	if (!cleaned.isEmpty() && cleaned != data.title && tryGet(data.artist, cleaned)) {
		return data;
	}
	// 3. Artist recovered from "(prod. X)" when the tag has none.
	if (data.artist.isEmpty()) {
		const auto prod = prodArtistFromTitle(data.title);
		const auto shortTitle = cleaned.isEmpty() ? data.title : cleaned;
		if (!prod.isEmpty() && tryGet(prod, shortTitle)) {
			data.artist = prod;
			data.title = shortTitle;
			return data;
		}
		// "Artist - Title" in a single tag.
		const auto dash = shortTitle.indexOf(QString::fromUtf8(" - "));
		if (dash > 0 && tryGet(
				shortTitle.left(dash).trimmed(),
				shortTitle.mid(dash + 3).trimmed())) {
			return data;
		}
	}

	// 4. Fallback: full-text search, best scored hit wins.
	const auto wantTitle = cleaned.isEmpty() ? data.title : cleaned;
	const auto wantArtist = data.artist.isEmpty()
		? prodArtistFromTitle(data.title)
		: data.artist;
	auto queries = QStringList();
	if (!wantArtist.isEmpty()) {
		queries.push_back(wantArtist + QString::fromUtf8(" ") + wantTitle);
	}
	queries.push_back(wantTitle);
	for (const auto &query : queries) {
		if (query.trimmed().isEmpty()) {
			continue;
		}
		const auto raw = searchRaw(query.trimmed(), timeoutMs);
		if (raw.isEmpty()) {
			continue;
		}
		const auto hits = parseSearch(raw);
		const auto *best = (const SearchHit*)(nullptr);
		auto bestScore = 0;
		for (const auto &h : hits) {
			const auto s = scoreHit(h, wantTitle, wantArtist, durationSec);
			if (s > bestScore) {
				bestScore = s;
				best = &h;
			}
		}
		if (best && bestScore > 0) {
			return dataFromHit(*best, wantArtist, wantTitle);
		}
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
