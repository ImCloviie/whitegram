// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "white/white_lang.h"

#include "qjsondocument.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_instance.h"
#include "storage/localstorage.h"

#include <QDir>
#include <QFile>

// hard-coded languages
std::map<QString, QString> langMapping = {
	{"pt-br", "pt"},
	{"zh-hans-beta", "zh-hans"},
	{"zh-hant-beta", "zh-hant"},
	{"zh-hans-raw", "zh-hans"},
	{"zh-hant-raw", "zh-hant"},
};

constexpr auto postfixes = {
	"zero",
	"one",
	"two",
	"few",
	"many",
	"other"
};

WhiteLanguage *WhiteLanguage::instance = nullptr;

WhiteLanguage::WhiteLanguage() = default;

void WhiteLanguage::init() {
	if (!instance) instance = new WhiteLanguage;
	instance->loadCachedLanguage();
}

WhiteLanguage *WhiteLanguage::currentInstance() {
	return instance;
}

QString WhiteLanguage::getCacheDir() const {
	return cWorkingDir() + u"tdata/ayu/languages/"_q;
}

QString WhiteLanguage::getCachePath(const QString &langId) const {
	return getCacheDir() + langId + u".json"_q;
}

// Whitegram ships its own translations of these strings inside the app
// instead of downloading the AyuGram ones: English comes from
// lang.strings, other languages from :/whitegram/langs/<id>.json.
void WhiteLanguage::loadCachedLanguage() {
	loadEmbeddedLanguage(
		Lang::GetInstance().id(),
		Lang::GetInstance().baseId());
}

void WhiteLanguage::loadEmbeddedLanguage(
		const QString &id,
		const QString &baseId) {
	const auto mapped = langMapping.contains(id) ? langMapping[id] : id;
	for (const auto &candidate : { mapped, baseId }) {
		if (candidate.isEmpty()) {
			continue;
		}
		QFile file(u":/whitegram/langs/%1.json"_q.arg(candidate));
		if (!file.open(QIODevice::ReadOnly)) {
			continue;
		}
		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
		if (error.error == QJsonParseError::NoError) {
			LOG(("Whitegram language: %1").arg(candidate));
			applyLanguageJson(doc);
		}
		return;
	}
}

void WhiteLanguage::saveCachedLanguage(const QByteArray &, const QString &) {
}

void WhiteLanguage::fetchLanguage(const QString &id, const QString &baseId) {
	_currentLangId = id.isEmpty() ? baseId : id;
	loadEmbeddedLanguage(id, baseId);
}

void WhiteLanguage::fetchFinished() {
	if (!_chkReply) return;

	QString langPackBaseId = Lang::GetInstance().baseId();
	QString langPackId = Lang::GetInstance().id();
	auto statusCode = _chkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if (statusCode == 404 && !langPackId.isEmpty() && !langPackBaseId.isEmpty() && !needFallback) {
		LOG(("AyuGram Language not found! Fallback to main language: %1...").arg(langPackBaseId));
		needFallback = true;
		_chkReply->disconnect();
		fetchLanguage("", langPackBaseId);
	} else {
		const auto result = _chkReply->readAll().trimmed();
		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(result, &error);
		if (error.error == QJsonParseError::NoError) {
			saveCachedLanguage(result, _currentLangId);
			applyLanguageJson(doc);
		} else {
			LOG(("Incorrect language JSON File."));
		}

		_chkReply = nullptr;
	}
}

void WhiteLanguage::fetchError(QNetworkReply::NetworkError e) {
	LOG(("Network error: %1").arg(e));

	if (e == QNetworkReply::NetworkError::ContentNotFoundError) {
		const auto baseId = Lang::GetInstance().baseId();
		const auto id = Lang::GetInstance().id();

		if (!id.isEmpty() && !baseId.isEmpty() && !needFallback) {
			LOG(("AyuGram Language not found! Fallback to main language: %1...").arg(baseId));
			needFallback = true;
			_chkReply->disconnect();
			fetchLanguage("", baseId);
		} else {
			LOG(("AyuGram Language not found!"));
			_chkReply = nullptr;
		}
	}
}

void WhiteLanguage::applyLanguageJson(QJsonDocument doc) {
	const auto json = doc.object();
	for (const QString &brokenKey : json.keys()) {
		auto key = qsl("white_") + brokenKey;
		auto val = json.value(brokenKey).toString().replace(qsl("&amp;"), qsl("&"));

		if (key.endsWith("_Android")) {
			continue;
		}

		for (const auto &postfix : postfixes) {
			if (key.endsWith(qsl("_") + postfix)) {
				key = key.replace(qsl("_") + postfix, qsl("#") + postfix);
				break;
			}
		}

		if (key.endsWith("_PC")) {
			key = key.replace("_PC", "");
		}

		if (val.contains(qsl("%1$d")) && !val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count}"));
		} else if (val.contains(qsl("%1$d")) && val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count1}")).replace(qsl("%2$d"), qsl("{count2}"));
		} else if (val.contains(qsl("%1$s")) && !val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item}"));
		} else if (val.contains(qsl("%1$s")) && val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item1}")).replace(qsl("%2$s"), qsl("{item2}"));
		}

		Lang::GetInstance().resetValue(key.toUtf8());
		Lang::GetInstance().applyValue(key.toUtf8(), val.toUtf8());
	}
	Lang::GetInstance().updatePluralRules();
}
