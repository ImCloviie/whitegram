// This file is part of Whitegram for Desktop.
#include "ayu/ui/utils/lyrics_translate.h"

#include "ayu/ui/utils/net_fetch.h"

#include <QtCore/QEventLoop>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <memory>

namespace Ayu::Ui::Lyrics {
namespace {

constexpr auto kTimeoutMs = 8000;
constexpr auto kChunkLimit = 4000;

QByteArray Post(const QString &text, const QString &target) {
	auto url = QUrl(u"https://translate.googleapis.com/translate_a/single"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"client"_q, u"gtx"_q);
	query.addQueryItem(u"sl"_q, u"auto"_q);
	query.addQueryItem(u"tl"_q, target);
	query.addQueryItem(u"dt"_q, u"t"_q);
	url.setQuery(query);
	auto body = QUrlQuery();
	body.addQueryItem(u"q"_q, text);
	return Net::FetchSync({
		.url = url,
		.body = body.toString(QUrl::FullyEncoded).toUtf8(),
		.contentType = "application/x-www-form-urlencoded;charset=UTF-8",
		.userAgent = u"Mozilla/5.0 (Windows NT 10.0; Win64; x64)"_q,
		.timeoutMs = kTimeoutMs,
	});
}

bool TranslateChunk(
		const std::vector<QString> &rows,
		const QString &target,
		std::vector<QString> &out,
		QString &source) {
	auto joined = QString();
	for (auto i = 0; i != int(rows.size()); ++i) {
		auto row = rows[i];
		row.replace(QChar(u'\n'), QChar(u' '));
		joined += (i ? u"\n"_q : QString()) + (row.trimmed().isEmpty() ? u"."_q : row);
	}
	const auto document = QJsonDocument::fromJson(Post(joined, target));
	const auto root = document.array();
	if (root.isEmpty()) {
		return false;
	}
	auto text = QString();
	for (const auto &segment : root.at(0).toArray()) {
		text += segment.toArray().at(0).toString();
	}
	if (root.size() > 2 && source.isEmpty()) {
		source = root.at(2).toString();
	}
	const auto parts = text.split(QChar(u'\n'));
	if (parts.size() != int(rows.size())) {
		return false;
	}
	for (const auto &part : parts) {
		out.push_back(part.trimmed());
	}
	return true;
}

} // namespace

bool LooksLike(const std::vector<QString> &lines, const QString &language) {
	auto cyrillic = 0;
	auto latin = 0;
	for (const auto &line : lines) {
		for (const auto ch : line) {
			const auto u = ch.unicode();
			if (u >= 0x0400 && u <= 0x04FF) {
				++cyrillic;
			} else if ((u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z')) {
				++latin;
			}
		}
	}
	const auto total = std::max(1, cyrillic + latin);
	const auto cyrillicLanguage = language.startsWith(u"ru"_q)
		|| language.startsWith(u"uk"_q)
		|| language.startsWith(u"be"_q);
	return cyrillicLanguage
		? (cyrillic * 10 >= total * 6)
		: (latin * 10 >= total * 6);
}

Translation TranslateSync(
		const std::vector<QString> &lines,
		const std::vector<QString> &keys,
		const QString &target) {
	auto result = Translation();
	result.target = target;
	auto rows = lines;
	rows.insert(rows.end(), keys.begin(), keys.end());
	auto out = std::vector<QString>();
	auto chunk = std::vector<QString>();
	auto size = 0;
	for (const auto &row : rows) {
		if (!chunk.empty() && size + row.size() + 1 > kChunkLimit) {
			if (!TranslateChunk(chunk, target, out, result.source)) {
				return result;
			}
			chunk.clear();
			size = 0;
		}
		chunk.push_back(row);
		size += row.size() + 1;
	}
	if (!chunk.empty() && !TranslateChunk(chunk, target, out, result.source)) {
		return result;
	}
	if (out.size() != rows.size() || result.source == target) {
		return result;
	}
	result.lines.assign(out.begin(), out.begin() + lines.size());
	result.keys.assign(out.begin() + lines.size(), out.end());
	result.ok = true;
	return result;
}

} // namespace Ayu::Ui::Lyrics
