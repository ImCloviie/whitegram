// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/connection_bypass/bypass_hosts.h"
#include "ayu/features/whitegram_backend/whitegram_backend.h"

#include "ayu/ayu_settings.h"
#include "ayu/libs/json.hpp"
#include "base/debug_log.h"
#include "core/application.h"
#include "core/update_checker.h"
#include "core/version.h"
#include "lang/lang_instance.h"
#include "base/platform/base_platform_info.h"
#include "storage/localstorage.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>
#include <QtCore/QUrl>
#include <QtNetwork/QHttpMultiPart>
#include <QtNetwork/QHttpPart>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <map>
#include <set>

namespace Whitegram::Backend {
namespace {

constexpr auto kDefaultBaseUrl = "http://31.76.111.240:8787";
constexpr auto kDefaultApiKey = "WgProd_k7Hx9mP2vQ4nR8sT1uY6zA3bC5dE0fJ";

[[nodiscard]] int CompareVersionStrings(
		const QString &left,
		const QString &right) {
	const auto leftParts = left.split('.');
	const auto rightParts = right.split('.');
	const auto count = std::max(leftParts.size(), rightParts.size());
	for (auto i = 0; i != count; ++i) {
		const auto leftPart = (i < leftParts.size()) ? leftParts[i].toInt() : 0;
		const auto rightPart = (i < rightParts.size()) ? rightParts[i].toInt() : 0;
		if (leftPart != rightPart) {
			return (leftPart < rightPart) ? -1 : 1;
		}
	}
	return 0;
}

[[nodiscard]] uint64 ParseReleasedVersion(const QByteArray &payload) {
	try {
		const auto json = nlohmann::json::parse(payload.constData());
		const auto platform = Platform::AutoUpdateKey().toStdString();
		if (!json.contains(platform) || !json[platform].is_object()) {
			return 0;
		}
		const auto &types = json[platform];
		if (!types.contains("stable") || !types["stable"].is_object()) {
			return 0;
		}
		const auto &stable = types["stable"];
		if (!stable.contains("released")) {
			return 0;
		}
		if (stable["released"].is_number()) {
			return stable["released"].get<uint64>();
		}
		if (stable["released"].is_string()) {
			return std::stoull(stable["released"].get<std::string>());
		}
	} catch (...) {
	}
	return 0;
}

struct CachedBackground {
	Zone zone = Zone::Full;
	QString path;
	bool isVideo = false;
};

using CacheMap = std::map<uint64, std::vector<CachedBackground>>;

[[nodiscard]] QString RemoteCacheDirectory() {
	return cWorkingDir() + u"tdata/whitegram/profile_bg/remote/"_q;
}

[[nodiscard]] QString ZoneName(Zone zone) {
	switch (zone) {
	case Zone::Top: return u"top"_q;
	case Zone::Bottom: return u"bottom"_q;
	case Zone::Full: return u"full"_q;
	}
	Unexpected("Zone value in Whitegram::Backend.");
}

[[nodiscard]] std::optional<Zone> ZoneFromName(const QString &name) {
	if (name == u"top"_q) {
		return Zone::Top;
	} else if (name == u"bottom"_q) {
		return Zone::Bottom;
	} else if (name == u"full"_q) {
		return Zone::Full;
	}
	return std::nullopt;
}

[[nodiscard]] QString DestinationPath(
		uint64 userId,
		Zone zone,
		const QString &suffix) {
	const auto dir = RemoteCacheDirectory();
	QDir().mkpath(dir);
	return dir
		+ QString::number(userId)
		+ u'_'
		+ ZoneName(zone)
		+ u'.'
		+ suffix;
}

[[nodiscard]] QString SuffixFromUrl(const QUrl &url) {
	const auto file = url.fileName();
	const auto index = file.lastIndexOf('.');
	return (index > 0) ? file.mid(index + 1) : u"bin"_q;
}

[[nodiscard]] bool PathIsVideo(const QString &path) {
	const auto mime = QMimeDatabase().mimeTypeForFile(path);
	return mime.inherits(u"video/"_q);
}

[[nodiscard]] QString DiskCachedPath(uint64 userId, Zone zone) {
	const auto prefix = QString::number(userId)
		+ u'_'
		+ ZoneName(zone)
		+ u'.';
	const QDir dir(RemoteCacheDirectory());
	if (!dir.exists()) {
		return QString();
	}
	for (const auto &info : dir.entryInfoList(QDir::Files)) {
		if (info.fileName().startsWith(prefix)) {
			return info.absoluteFilePath();
		}
	}
	return QString();
}

[[nodiscard]] bool IsRussian() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
}

[[nodiscard]] QString FormatBetaBadge(
		const QString &betaLabel,
		const QString &betaVersion) {
	if (IsRussian()) {
		return u"Версия pre-beta "_q + betaVersion;
	}
	return u"Version pre-beta "_q + betaVersion;
}

class Service {
public:
	static Service &Instance() {
		static Service instance;
		return instance;
	}

	[[nodiscard]] CacheMap &cache() {
		return _cache;
	}

	[[nodiscard]] rpl::event_stream<uint64> &updates() {
		return _updates;
	}

	[[nodiscard]] QNetworkAccessManager &accessManager() {
		return _accessManager;
	}

	[[nodiscard]] rpl::variable<QString> &betaBadgeText() {
		return _betaBadgeText;
	}

	[[nodiscard]] rpl::variable<bool> &updateAvailable() {
		return _updateAvailable;
	}

	void applyLocalBetaBadge() {
		_betaBadgeText = FormatBetaBadge(
			u"pre-beta"_q,
			Whitegram::Backend::LocalPreBetaVersion());
	}

	void setUpdateAvailable(bool available) {
		_updateAvailable = available;
	}

	void setServerBetaVersion(const QString &version) {
		_serverBetaVersion = version;
		recomputeUpdateAvailable();
	}

	void setManifestReleased(uint64 released) {
		_manifestReleased = released;
		recomputeUpdateAvailable();
	}

	void recomputeUpdateAvailable() {
		if (_manifestReleased > 0) {
			_updateAvailable = _manifestReleased > uint64(AppVersion);
			return;
		}
		const auto localBeta = Whitegram::Backend::LocalPreBetaVersion();
		_updateAvailable = !_serverBetaVersion.isEmpty()
			&& CompareVersionStrings(localBeta, _serverBetaVersion) < 0;
	}

	void setBetaBadge(
			const QString &betaLabel,
			const QString &betaVersion,
			const QString &displayVersion = {}) {
		Q_UNUSED(betaLabel);
		Q_UNUSED(betaVersion);
		Q_UNUSED(displayVersion);
		applyLocalBetaBadge();
	}

private:
	CacheMap _cache;
	rpl::event_stream<uint64> _updates;
	QNetworkAccessManager _accessManager;
	rpl::variable<QString> _betaBadgeText;
	rpl::variable<bool> _updateAvailable = false;
	QString _serverBetaVersion;
	uint64 _manifestReleased = 0;
};

[[nodiscard]] CachedBackground *FindCached(uint64 userId, Zone zone) {
	auto &entries = Service::Instance().cache()[userId];
	const auto i = ranges::find_if(entries, [&](const CachedBackground &entry) {
		return entry.zone == zone;
	});
	return (i != end(entries)) ? &*i : nullptr;
}

void UpsertCached(
		uint64 userId,
		Zone zone,
		const QString &path,
		bool isVideo) {
	auto &entries = Service::Instance().cache()[userId];
	if (const auto i = ranges::find_if(entries, [&](const CachedBackground &entry) {
		return entry.zone == zone;
	}); i != end(entries)) {
		i->path = path;
		i->isVideo = isVideo;
	} else {
		entries.push_back({ zone, path, isVideo });
	}
	Service::Instance().updates().fire_copy(userId);
}

void HydrateCacheFromDisk(uint64 userId) {
	for (const auto zone : { Zone::Top, Zone::Bottom, Zone::Full }) {
		const auto path = DiskCachedPath(userId, zone);
		if (path.isEmpty()) {
			continue;
		}
		if (const auto cached = FindCached(userId, zone)) {
			if (cached->path == path && QFile::exists(path)) {
				continue;
			}
		}
		UpsertCached(userId, zone, path, PathIsVideo(path));
	}
}

void ClearRemoteCacheForUser(uint64 userId) {
	Service::Instance().cache().erase(userId);
	const auto prefix = QString::number(userId) + u'_';
	const QDir dir(RemoteCacheDirectory());
	if (!dir.exists()) {
		return;
	}
	for (const auto &info : dir.entryInfoList(QDir::Files)) {
		if (info.fileName().startsWith(prefix)) {
			QFile::remove(info.absoluteFilePath());
		}
	}
}

void RemoveCached(uint64 userId, Zone zone) {
	auto &map = Service::Instance().cache();
	const auto mapIt = map.find(userId);
	if (mapIt == end(map)) {
		return;
	}
	auto &entries = mapIt->second;
	const auto i = ranges::remove_if(entries, [&](const CachedBackground &entry) {
		return entry.zone == zone;
	});
	if (i != end(entries)) {
		entries.erase(i, end(entries));
		Service::Instance().updates().fire_copy(userId);
	}
	if (entries.empty()) {
		map.erase(mapIt);
	}
}

void DownloadBackgroundFile(
		uint64 userId,
		Zone zone,
		bool isVideo,
		const QUrl &url) {
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = Service::Instance().accessManager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Whitegram Backend: background download failed: %1 (%2)"
				).arg(url.toString()).arg(reply->errorString()));
			return;
		}
		const auto suffix = SuffixFromUrl(url);
		const auto destination = DestinationPath(userId, zone, suffix);
		if (QFile::exists(destination)) {
			QFile::remove(destination);
		}
		QFile file(destination);
		if (!file.open(QIODevice::WriteOnly)) {
			return;
		}
		file.write(reply->readAll());
		file.close();
		UpsertCached(userId, zone, destination, isVideo);
	});
}

void ParseProfileBackgrounds(uint64 userId, const QByteArray &payload) {
	try {
		const auto json = nlohmann::json::parse(payload.constData());
		const auto list = json.value("backgrounds", nlohmann::json::array());
		if (list.empty()) {
			ClearRemoteCacheForUser(userId);
			Service::Instance().updates().fire_copy(userId);
			return;
		}
		auto seenZones = std::set<Zone>();
		for (const auto &item : list) {
			const auto zoneName = QString::fromStdString(
				item.value("zone", ""));
			const auto zone = ZoneFromName(zoneName);
			if (!zone) {
				continue;
			}
			const auto isVideo = item.value("isVideo", false);
			const auto urlPath = QString::fromStdString(item.value("url", ""));
			if (urlPath.isEmpty()) {
				continue;
			}
			seenZones.insert(*zone);
			const auto url = QUrl(BaseUrl() + urlPath);
			DownloadBackgroundFile(userId, *zone, isVideo, url);
		}
		if (const auto mapIt = Service::Instance().cache().find(userId);
				mapIt != end(Service::Instance().cache())) {
			auto &entries = mapIt->second;
			const auto i = ranges::remove_if(entries, [&](
					const CachedBackground &entry) {
				return !seenZones.contains(entry.zone);
			});
			if (i != end(entries)) {
				entries.erase(i, end(entries));
			}
		}
	} catch (...) {
		LOG(("Whitegram Backend: failed to parse profile backgrounds."));
	}
}

[[nodiscard]] QNetworkRequest AuthorizedRequest(const QUrl &url) {
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("X-Whitegram-Key", ApiKey().toUtf8());
	return request;
}

void ParseClientInfo(const QByteArray &payload) {
	try {
		const auto json = nlohmann::json::parse(payload.constData());
		const auto serverBeta = QString::fromStdString(
			json.value("betaVersion", std::string()));
		if (!serverBeta.isEmpty()) {
			Service::Instance().setServerBetaVersion(serverBeta);
		}
		if (json.contains("bypassMirrors")
			&& json["bypassMirrors"].is_array()) {
			auto mirrors = std::vector<QString>();
			for (const auto &entry : json["bypassMirrors"]) {
				mirrors.push_back(QString::fromStdString(
					entry.get<std::string>()));
			}
			AyuFeatures::BypassHosts::setServerMirrors(mirrors);
		}
		if (json.contains("bypassMirrorHosts")
			&& json["bypassMirrorHosts"].is_array()) {
			auto hosts = std::vector<QString>();
			for (const auto &entry : json["bypassMirrorHosts"]) {
				hosts.push_back(QString::fromStdString(
					entry.get<std::string>()));
			}
			AyuFeatures::BypassHosts::setServerMirrorHosts(hosts);
		}
	} catch (...) {
	}
}

void ParseUpdateManifest(const QByteArray &payload) {
	const auto released = ParseReleasedVersion(payload);
	if (released) {
		Service::Instance().setManifestReleased(released);
	}
}

} // namespace

QString BaseUrl() {
	return kDefaultBaseUrl;
}

QString ApiKey() {
	return kDefaultApiKey;
}

void UploadProfileBackground(
		uint64 userId,
		Zone zone,
		const QString &path,
		bool isVideo) {
	const auto info = QFileInfo(path);
	if (!info.exists()) {
		return;
	}
	const auto url = QUrl(BaseUrl()
		+ u"/api/v1/profile-backgrounds/"_q
		+ QString::number(userId)
		+ u'/'
		+ ZoneName(zone));
	auto file = std::make_unique<QFile>(path);
	if (!file->open(QIODevice::ReadOnly)) {
		return;
	}
	const auto disposition = u"form-data; name=\"file\"; filename=\""_q
		+ info.fileName()
		+ u'"';
	auto multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
	auto filePart = QHttpPart();
	filePart.setHeader(
		QNetworkRequest::ContentDispositionHeader,
		disposition);
	filePart.setHeader(
		QNetworkRequest::ContentTypeHeader,
		QByteArray("application/octet-stream"));
	filePart.setBodyDevice(file.get());
	multipart->append(filePart);

	auto isVideoPart = QHttpPart();
	isVideoPart.setHeader(
		QNetworkRequest::ContentDispositionHeader,
		QByteArray("form-data; name=\"isVideo\""));
	isVideoPart.setBody(isVideo ? "true" : "false");
	multipart->append(isVideoPart);

	file->setParent(multipart);
	const auto reply = Service::Instance().accessManager().put(
		AuthorizedRequest(url),
		multipart);
	multipart->setParent(reply);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Whitegram Backend: background upload failed: %1 (%2)"
				).arg(url.toString()).arg(reply->errorString()));
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status < 200 || status >= 300) {
			LOG(("Whitegram Backend: background upload HTTP %1: %2"
				).arg(status).arg(QString::fromUtf8(reply->readAll())));
		}
	});
}

void RemoveProfileBackground(uint64 userId, Zone zone) {
	const auto url = QUrl(BaseUrl()
		+ u"/api/v1/profile-backgrounds/"_q
		+ QString::number(userId)
		+ u'/'
		+ ZoneName(zone));
	const auto reply = Service::Instance().accessManager().deleteResource(
		AuthorizedRequest(url));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
	});
	if (const auto cached = FindCached(userId, zone)) {
		QFile::remove(cached->path);
	}
	RemoveCached(userId, zone);
}

void FetchProfileBackgrounds(uint64 userId) {
	HydrateCacheFromDisk(userId);
	const auto url = QUrl(BaseUrl()
		+ u"/api/v1/profile-backgrounds/"_q
		+ QString::number(userId));
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = Service::Instance().accessManager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Whitegram Backend: background fetch failed for %1: %2"
				).arg(userId).arg(reply->errorString()));
			return;
		}
		ParseProfileBackgrounds(userId, reply->readAll());
	});
}

bool HasCachedBackground(uint64 userId, Zone zone) {
	const auto &map = Service::Instance().cache();
	const auto mapIt = map.find(userId);
	if (mapIt == end(map)) {
		return false;
	}
	return ranges::any_of(mapIt->second, [&](const CachedBackground &entry) {
		return entry.zone == zone && QFile::exists(entry.path);
	});
}

std::optional<Zone> CachedTopZone(uint64 userId) {
	if (HasCachedBackground(userId, Zone::Full)) {
		return Zone::Full;
	} else if (HasCachedBackground(userId, Zone::Top)) {
		return Zone::Top;
	}
	return std::nullopt;
}

std::optional<Zone> CachedBottomZone(uint64 userId) {
	if (HasCachedBackground(userId, Zone::Full)) {
		return Zone::Full;
	} else if (HasCachedBackground(userId, Zone::Bottom)) {
		return Zone::Bottom;
	}
	return std::nullopt;
}

bool CachedUsesFullBackground(uint64 userId) {
	return HasCachedBackground(userId, Zone::Full);
}

QString CachedBackgroundPath(uint64 userId, Zone zone) {
	if (const auto cached = FindCached(userId, zone)) {
		if (QFile::exists(cached->path)) {
			return cached->path;
		}
	}
	if (const auto path = DiskCachedPath(userId, zone); !path.isEmpty()) {
		UpsertCached(userId, zone, path, PathIsVideo(path));
		return path;
	}
	return QString();
}

bool CachedBackgroundIsVideo(uint64 userId, Zone zone) {
	if (const auto cached = FindCached(userId, zone)) {
		return cached->isVideo;
	}
	return false;
}

rpl::producer<uint64> profileBackgroundUpdates() {
	return Service::Instance().updates().events();
}

void InitClientInfo() {
	Service::Instance().applyLocalBetaBadge();
	FetchClientInfo();
	CheckUpdateAvailability();
}

void FetchClientInfo() {
	const auto url = QUrl(BaseUrl() + u"/api/v1/client-info"_q);
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = Service::Instance().accessManager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		ParseClientInfo(reply->readAll());
	});
}

QString BetaBadgeText() {
	return Service::Instance().betaBadgeText().current();
}

rpl::producer<QString> betaBadgeTextValue() {
	return Service::Instance().betaBadgeText().value();
}

QString LocalPreBetaVersion() {
	const auto version = AppVersion;
	if (version >= 7000008 && version < 7010000) {
		return u"1.0."_q + QString::number(version - 7000008);
	}
	const auto offset = version - 7000000;
	const auto major = offset / 10000;
	const auto remainder = offset % 10000;
	const auto minor = remainder / 100;
	const auto patch = remainder % 100;
	return QString::number(major)
		+ u'.'
		+ QString::number(minor)
		+ u'.'
		+ QString::number(patch);
}

void CheckUpdateAvailability() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::UpdaterDisabled()) {
		return;
	}
	const auto updaterVersion = Platform::AutoUpdateVersion();
	const auto path = Local::readAutoupdatePrefix()
		+ u"/current"_q
		+ (updaterVersion > 1
			? QString::number(int(updaterVersion))
			: QString());
	const auto url = QUrl(path);
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = Service::Instance().accessManager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		ParseUpdateManifest(reply->readAll());
	});
#endif
}

bool IsUpdateAvailable() {
	return Service::Instance().updateAvailable().current();
}

rpl::producer<bool> updateAvailableValue() {
	return Service::Instance().updateAvailable().value();
}

void TriggerUpdateDownload() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::UpdaterDisabled()) {
		return;
	}
	cSetLastUpdateCheck(0);
	Core::UpdateChecker().startForced(true);
#endif
}

void InstallReadyUpdate() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::UpdaterDisabled()) {
		return;
	}
	if (Core::UpdateChecker().state() != Core::UpdateChecker::State::Ready) {
		return;
	}
	Core::checkReadyUpdate();
	Core::Restart();
#endif
}

void RefreshUpdateAvailability() {
	Service::Instance().recomputeUpdateAvailable();
}

void ClearUpdateAvailable() {
	RefreshUpdateAvailability();
}

} // namespace Whitegram::Backend
