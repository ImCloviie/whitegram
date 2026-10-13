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

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QMimeDatabase>
#include <QtCore/QTimer>
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

constexpr auto kDefaultBaseUrl = "http://31.76.111.19:8787";
constexpr auto kDefaultApiKey = "WgProd_k7Hx9mP2vQ4nR8sT1uY6zA3bC5dE0fJ";
constexpr auto kBackendRequestTimeout = 15 * 1000;
// A change has longer, the server stores a whole background before it
// answers. Both are times without any data going either way.
constexpr auto kBackendWriteTimeout = 60 * 1000;
// An upload is stopped for a newer change only while more than this is
// left to send. The network thread can be a little ahead of the reported
// progress, and a body that reaches the server whole gets stored. The
// server writes an upload in place of the stored file as it arrives, so a
// stopped one can leave it an incomplete file, the newer change is sent
// right after and replaces or removes it.
constexpr auto kStoppableUploadLeft = qint64(1024 * 1024);
// An upload the server did not take is sent again at most this many times
// in a run, a server that keeps failing would otherwise get the whole file
// on every visit of the own profile.
constexpr auto kUploadResends = 2;
// Windows refuses to delete or replace a file while it is read, a
// background reads its file once when it starts showing it.
constexpr auto kFileRetryDelay = 500;
constexpr auto kFileRetryAttempts = 20;

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
	return mime.isValid() && mime.name().startsWith(u"video/"_q);
}

[[nodiscard]] QString ZoneFilePrefix(uint64 userId, Zone zone) {
	return QString::number(userId) + u'_' + ZoneName(zone) + u'.';
}

// A download is written aside under this name and its number first, so
// that a half written file is never picked up. The name does not match the
// cached files prefix.
[[nodiscard]] QString ZonePartialPrefix(uint64 userId, Zone zone) {
	return QString::number(userId) + u'_' + ZoneName(zone) + u"~part"_q;
}

// The names of the cached files of the zone, the newest first, and the
// partial downloads of it. This runs for every zone a profile has no
// background in, so only the names of the zone are listed.
[[nodiscard]] QStringList DiskCachedNames(
		const QDir &dir,
		uint64 userId,
		Zone zone,
		QStringList *partials = nullptr) {
	const auto prefix = ZoneFilePrefix(userId, zone);
	const auto partialPrefix = ZonePartialPrefix(userId, zone);
	auto result = QStringList();
	for (const auto &name : dir.entryList(
			{ QString::number(userId) + u'_' + ZoneName(zone) + u'*' },
			QDir::Files,
			QDir::Time)) {
		if (name.startsWith(prefix)) {
			result.push_back(name);
		} else if (partials && name.startsWith(partialPrefix)) {
			partials->push_back(name);
		}
	}
	return result;
}

// Removes the cached files of the zone except the one to keep, and the
// partial downloads of it if asked. Returns whether a cached file was
// removed, sets left when one could not be.
bool RemoveDiskCached(
		uint64 userId,
		Zone zone,
		const QString &keep,
		bool withPartials,
		bool *left = nullptr) {
	const QDir dir(RemoteCacheDirectory());
	if (!dir.exists()) {
		return false;
	}
	const auto kept = keep.isEmpty()
		? QString()
		: QFileInfo(keep).absoluteFilePath();
	auto partials = QStringList();
	auto removed = false;
	for (const auto &name : DiskCachedNames(
			dir,
			userId,
			zone,
			withPartials ? &partials : nullptr)) {
		const auto path = dir.absoluteFilePath(name);
		if (path == kept) {
			continue;
		} else if (QFile::remove(path)) {
			removed = true;
		} else if (left && QFile::exists(path)) {
			*left = true;
		}
	}
	for (const auto &name : partials) {
		QFile::remove(dir.absoluteFilePath(name));
	}
	return removed;
}

// The zones whose server copy is gone while its file could not be deleted
// yet. That file is never used again, and deleting it is tried again. A
// mark is kept on the disk too, the file could stay there until a restart.
[[nodiscard]] std::set<std::pair<uint64, Zone>> &UndeletedZones() {
	static auto result = std::set<std::pair<uint64, Zone>>();
	return result;
}

[[nodiscard]] QString UndeletedMarkPath(uint64 userId, Zone zone) {
	// The name does not match the cached files prefix.
	return RemoteCacheDirectory()
		+ QString::number(userId)
		+ u'_'
		+ ZoneName(zone)
		+ u"~gone"_q;
}

void MarkUndeleted(uint64 userId, Zone zone) {
	UndeletedZones().insert({ userId, zone });
	auto file = QFile(UndeletedMarkPath(userId, zone));
	if (file.open(QIODevice::WriteOnly)) {
		file.close();
	}
}

void UnmarkUndeleted(uint64 userId, Zone zone) {
	UndeletedZones().erase({ userId, zone });
	QFile::remove(UndeletedMarkPath(userId, zone));
}

[[nodiscard]] bool IsUndeleted(uint64 userId, Zone zone) {
	auto &undeleted = UndeletedZones();
	if (undeleted.contains({ userId, zone })) {
		return true;
	} else if (!QFile::exists(UndeletedMarkPath(userId, zone))) {
		return false;
	}
	// Marked in an earlier run.
	undeleted.insert({ userId, zone });
	return true;
}

// Returns whether a file of a forgotten zone is still there.
bool RemoveUndeleted(uint64 userId, Zone zone) {
	if (!IsUndeleted(userId, zone)) {
		return false;
	}
	auto left = false;
	RemoveDiskCached(userId, zone, QString(), false, &left);
	if (left) {
		return true;
	}
	UnmarkUndeleted(userId, zone);
	return false;
}

[[nodiscard]] QString DiskCachedPath(uint64 userId, Zone zone) {
	if (IsUndeleted(userId, zone)) {
		RemoveUndeleted(userId, zone);
		return QString();
	}
	const QDir dir(RemoteCacheDirectory());
	if (!dir.exists()) {
		return QString();
	}
	for (const auto &name : DiskCachedNames(dir, userId, zone)) {
		// A larger file is left from an older version. It would be played
		// from the disk and kept open, so it could not be removed.
		const auto path = dir.absoluteFilePath(name);
		if (QFileInfo(path).size() <= kMaxProfileBackgroundSize) {
			return path;
		}
	}
	return QString();
}

// A change this client made that the server has not confirmed yet. It is
// kept on the disk and sent again after a failure or a restart, otherwise
// a removed background would come back from the server.
enum class PendingChange {
	None,
	Upload,
	Remove,
};

struct PendingState {
	PendingChange change = PendingChange::None;
	// When the change was made, in ms since the epoch, zero if not known.
	qint64 made = 0;
};

[[nodiscard]] QString PendingChangePath(uint64 userId, Zone zone) {
	// The name does not match the cached files prefix.
	return RemoteCacheDirectory()
		+ QString::number(userId)
		+ u'_'
		+ ZoneName(zone)
		+ u"~pending"_q;
}

void SetPendingChange(uint64 userId, Zone zone, PendingChange change) {
	const auto path = PendingChangePath(userId, zone);
	if (change == PendingChange::None) {
		QFile::remove(path);
		return;
	}
	QDir().mkpath(RemoteCacheDirectory());
	auto data = QByteArray((change == PendingChange::Remove)
		? "remove"
		: "upload");
	data.append('\n');
	data.append(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
	auto file = QFile(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(data);
	}
}

[[nodiscard]] PendingState PendingChangeFor(uint64 userId, Zone zone) {
	auto file = QFile(PendingChangePath(userId, zone));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto data = file.readAll();
	const auto separator = data.indexOf('\n');
	const auto kind = (separator < 0) ? data : data.left(separator);
	const auto change = (kind == "remove")
		? PendingChange::Remove
		: (kind == "upload")
		? PendingChange::Upload
		: PendingChange::None;
	return {
		.change = change,
		.made = (separator < 0) ? 0 : data.mid(separator + 1).toLongLong(),
	};
}

[[nodiscard]] bool SameFileContent(
		const QString &path,
		const QByteArray &bytes) {
	auto file = QFile(path);
	return (file.size() == bytes.size())
		&& file.open(QIODevice::ReadOnly)
		&& (file.readAll() == bytes);
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
	// Looked up without adding an empty entry for every profile visited.
	auto &map = Service::Instance().cache();
	const auto mapIt = map.find(userId);
	if (mapIt == end(map)) {
		return nullptr;
	}
	auto &entries = mapIt->second;
	const auto i = ranges::find_if(entries, [&](const CachedBackground &entry) {
		return entry.zone == zone;
	});
	return (i != end(entries)) ? &*i : nullptr;
}

// The latest download of each zone, only that one is used. Every version
// of a zone has the same address on the server, so an older download that
// finishes last would replace a newer file otherwise. A removal or a copy
// set on this device drops the download on its way, so it cannot bring
// back a removed background either.
struct ExpectedDownload {
	uint64 number = 0;
	// What the server listed, the same file is not downloaded again.
	QUrl url;
	qint64 updated = 0;
	QPointer<QNetworkReply> reply;
	bool placed = false;
};

[[nodiscard]] auto ExpectedDownloads()
-> std::map<std::pair<uint64, Zone>, ExpectedDownload>& {
	static auto result = std::map<
		std::pair<uint64, Zone>,
		ExpectedDownload>();
	return result;
}

void DropDownload(uint64 userId, Zone zone) {
	auto &map = ExpectedDownloads();
	const auto i = map.find({ userId, zone });
	if (i == end(map)) {
		return;
	}
	const auto reply = i->second.reply;
	map.erase(i);
	if (reply && reply->isRunning()) {
		// Its data would be dropped anyway.
		reply->abort();
	}
}

[[nodiscard]] ExpectedDownload *FindExpectedDownload(
		uint64 userId,
		Zone zone,
		uint64 number) {
	auto &map = ExpectedDownloads();
	const auto i = map.find({ userId, zone });
	return (i != end(map) && i->second.number == number)
		? &i->second
		: nullptr;
}

[[nodiscard]] bool DownloadExpected(
		uint64 userId,
		Zone zone,
		uint64 number) {
	return FindExpectedDownload(userId, zone, number) != nullptr;
}

// Fetches of a user's backgrounds are numbered, a reply older than one
// already applied describes an outdated state and is ignored. So is a
// reply that comes while this client changes that user's backgrounds on
// the server, or that was sent before such a change finished: the server
// could have answered it with the state from before the change. The
// backgrounds are fetched again once the changes are done.
struct FetchState {
	uint64 sent = 0;
	uint64 applied = 0;
	uint64 changedAfter = 0;
	int running = 0;
	bool refetch = false;
	// The pending changes are to be sent again with the next reply, the
	// reply of a fetch that asked for that was dropped or failed.
	bool replay = false;
	// The last fetch sent before the pending changes were last sent again,
	// a reply of a fetch up to it does not ask for that once more.
	uint64 replayedAt = 0;
};

[[nodiscard]] FetchState &FetchStateFor(uint64 userId) {
	static auto result = std::map<uint64, FetchState>();
	return result[userId];
}

// A change of a zone on the server, empty bytes remove the background.
struct ServerWrite {
	QByteArray bytes;
	QString fileName;
	bool isVideo = false;
	// The WriteGeneration it was asked for with.
	uint64 generation = 0;
};

// The changes of each zone go to the server one at a time, in the order
// they were made, otherwise an older one could arrive last and win. While
// one is sent only the newest waiting change is kept, the ones between
// them would be overwritten anyway. A zone is listed here while it has a
// change being sent or waiting.
struct ZoneWrites {
	QPointer<QNetworkReply> sending;
	// How much of the change being sent is left to send, see
	// kStoppableUploadLeft.
	qint64 unsent = 0;
	std::optional<ServerWrite> waiting;
};

[[nodiscard]] std::map<std::pair<uint64, Zone>, ZoneWrites> &Writes() {
	static auto result = std::map<std::pair<uint64, Zone>, ZoneWrites>();
	return result;
}

[[nodiscard]] bool WritesPending(uint64 userId) {
	const auto &map = Writes();
	for (const auto zone : { Zone::Top, Zone::Bottom, Zone::Full }) {
		if (map.contains({ userId, zone })) {
			return true;
		}
	}
	return false;
}

// Counts the changes asked for each zone. An upload read away from the
// main thread is dropped if another change was asked for meanwhile, and
// only the latest change clears the pending one on the disk.
[[nodiscard]] uint64 &WriteGeneration(uint64 userId, Zone zone) {
	static auto result = std::map<std::pair<uint64, Zone>, uint64>();
	return result[{ userId, zone }];
}

// How many times the pending upload of each zone was sent again in this
// run, see kUploadResends.
[[nodiscard]] int &ResentUploads(uint64 userId, Zone zone) {
	static auto result = std::map<std::pair<uint64, Zone>, int>();
	return result[{ userId, zone }];
}

// The uploads whose files are being read, with their WriteGeneration. Such
// a zone is not in Writes() yet, but it has a change on its way.
[[nodiscard]] auto UploadsBeingRead()
-> std::map<std::pair<uint64, Zone>, uint64>& {
	static auto result = std::map<std::pair<uint64, Zone>, uint64>();
	return result;
}

[[nodiscard]] bool HasLocalCopy(uint64 userId, Zone zone) {
	const auto local = AyuSettings::getInstance().customProfileBackground(
		userId,
		zone);
	return local && !local->path.isEmpty() && QFile::exists(local->path);
}

void NotifyUpdated(uint64 userId) {
	Service::Instance().updates().fire_copy(userId);
}

// Returns whether the entry changed, the caller notifies about it.
bool UpsertCached(
		uint64 userId,
		Zone zone,
		const QString &path,
		bool isVideo) {
	auto &entries = Service::Instance().cache()[userId];
	if (const auto i = ranges::find_if(entries, [&](const CachedBackground &entry) {
		return entry.zone == zone;
	}); i != end(entries)) {
		if (i->path == path && i->isVideo == isVideo) {
			return false;
		}
		i->path = path;
		i->isVideo = isVideo;
	} else {
		entries.push_back({ zone, path, isVideo });
	}
	return true;
}

void HydrateCacheFromDisk(uint64 userId) {
	auto changed = false;
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
		changed |= UpsertCached(userId, zone, path, PathIsVideo(path));
	}
	if (changed) {
		NotifyUpdated(userId);
	}
}

// Returns whether the entry was there, the caller notifies about it.
bool RemoveCached(uint64 userId, Zone zone) {
	auto &map = Service::Instance().cache();
	const auto mapIt = map.find(userId);
	if (mapIt == end(map)) {
		return false;
	}
	auto &entries = mapIt->second;
	const auto i = ranges::remove_if(entries, [&](const CachedBackground &entry) {
		return entry.zone == zone;
	});
	const auto removed = (i != end(entries));
	entries.erase(i, end(entries));
	if (entries.empty()) {
		map.erase(mapIt);
	}
	return removed;
}

void RetryRemoveUndeleted(uint64 userId, Zone zone, int attempt) {
	const auto context = &Service::Instance().accessManager();
	QTimer::singleShot(kFileRetryDelay, context, [=] {
		if (RemoveUndeleted(userId, zone)
			&& (attempt + 1 < kFileRetryAttempts)) {
			RetryRemoveUndeleted(userId, zone, attempt + 1);
		}
	});
}

// Forgets the server copy of the zone, in the memory and on the disk.
// Returns whether there was one, the caller notifies about it.
bool ForgetZone(uint64 userId, Zone zone) {
	// A download still on its way is dropped, the file would be picked up
	// from the disk otherwise.
	DropDownload(userId, zone);
	auto removed = false;
	if (const auto cached = FindCached(userId, zone)) {
		removed |= QFile::remove(cached->path);
	}
	removed |= RemoveCached(userId, zone);
	auto left = false;
	removed |= RemoveDiskCached(userId, zone, QString(), true, &left);
	if (left) {
		// Being read right now, it must not be picked up from the disk.
		MarkUndeleted(userId, zone);
		RetryRemoveUndeleted(userId, zone, 0);
		removed = true;
	}
	return removed;
}

// Puts the written file in place of the cached one and uses it, an empty
// partial path keeps the cached one. Windows refuses to replace a file
// while it is read, so that is tried again a moment later.
void PlaceDownload(
		uint64 userId,
		Zone zone,
		bool isVideo,
		const QString &destination,
		uint64 download,
		const QString &partial,
		int attempt) {
	if (!DownloadExpected(userId, zone, download)) {
		if (!partial.isEmpty()) {
			QFile::remove(partial);
		}
		return;
	}
	if (!partial.isEmpty()) {
		if (!QFile::exists(partial)) {
			// Nothing is left to put in place, the cached file is kept.
			return;
		}
		QFile::remove(destination);
		if (!QFile::rename(partial, destination)) {
			if (QFile::exists(partial)
				&& (attempt + 1 < kFileRetryAttempts)) {
				const auto context = &Service::Instance().accessManager();
				QTimer::singleShot(kFileRetryDelay, context, [=] {
					PlaceDownload(
						userId,
						zone,
						isVideo,
						destination,
						download,
						partial,
						attempt + 1);
				});
			} else {
				QFile::remove(partial);
			}
			return;
		}
	}
	FindExpectedDownload(userId, zone, download)->placed = true;
	UnmarkUndeleted(userId, zone);
	// A file of another type could be left from an earlier background, and
	// the partial files of the downloads dropped for this one.
	RemoveDiskCached(userId, zone, destination, true);
	const auto changed = UpsertCached(userId, zone, destination, isVideo);
	if (changed || !partial.isEmpty()) {
		NotifyUpdated(userId);
	}
}

void StoreDownload(
		uint64 userId,
		Zone zone,
		bool isVideo,
		const QUrl &url,
		uint64 download,
		const QByteArray &bytes) {
	const auto destination = DestinationPath(
		userId,
		zone,
		SuffixFromUrl(url));
	// The file downloaded again, after a restart for example, is mostly the
	// same. Rewriting it would reload it and restart a playing video.
	if (SameFileContent(destination, bytes)) {
		PlaceDownload(
			userId,
			zone,
			isVideo,
			destination,
			download,
			QString(),
			0);
		return;
	}
	// A name of its own, a retry of an earlier download can't take it.
	const auto partial = RemoteCacheDirectory()
		+ ZonePartialPrefix(userId, zone)
		+ QString::number(download);
	auto file = QFile(partial);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| file.write(bytes) != bytes.size()) {
		file.close();
		QFile::remove(partial);
		return;
	}
	file.close();
	PlaceDownload(userId, zone, isVideo, destination, download, partial, 0);
}

// Whether the file the server lists for the zone is shown already or on
// its way, it is fetched again on every profile visit otherwise.
[[nodiscard]] bool DownloadUpToDate(
		uint64 userId,
		Zone zone,
		const QUrl &url,
		qint64 updated) {
	const auto &map = ExpectedDownloads();
	const auto i = map.find({ userId, zone });
	if (i == end(map)
		|| i->second.url != url
		|| i->second.updated != updated) {
		return false;
	} else if (i->second.reply && i->second.reply->isRunning()) {
		// Starting it over on every visit could keep a large file from
		// ever arriving, even when the server gives no time.
		return true;
	} else if (!updated || !i->second.placed) {
		// Without a time the placed file can't be known to be current.
		return false;
	}
	const auto cached = FindCached(userId, zone);
	return cached && QFile::exists(cached->path);
}

void DownloadBackgroundFile(
		uint64 userId,
		Zone zone,
		bool isVideo,
		const QUrl &url,
		qint64 updated) {
	if (DownloadUpToDate(userId, zone, url, updated)) {
		return;
	}
	static auto lastDownload = uint64();
	const auto download = ++lastDownload;
	DropDownload(userId, zone);
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kBackendRequestTimeout);
	const auto reply = Service::Instance().accessManager().get(request);
	ExpectedDownloads()[{ userId, zone }] = ExpectedDownload{
		.number = download,
		.url = url,
		.updated = updated,
		.reply = reply,
	};
	const auto tooBig = std::make_shared<bool>(false);
	QObject::connect(reply, &QNetworkReply::downloadProgress, [=](
			qint64 received,
			qint64 total) {
		if (!*tooBig
			&& std::max(received, total) > kMaxProfileBackgroundSize) {
			*tooBig = true;
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (!DownloadExpected(userId, zone, download)) {
			// A newer download or a change of the zone replaced it.
			return;
		} else if (*tooBig) {
			// Such a background is not shown, an older one of the zone
			// would be outdated.
			LOG(("Whitegram Backend: background is too big: %1"
				).arg(url.toString()));
			if (ForgetZone(userId, zone)) {
				NotifyUpdated(userId);
			}
			return;
		} else if (reply->error() != QNetworkReply::NoError) {
			LOG(("Whitegram Backend: background download failed: %1 (%2)"
				).arg(url.toString()).arg(reply->errorString()));
			return;
		}
		const auto bytes = reply->readAll();
		if (bytes.isEmpty() || bytes.size() > kMaxProfileBackgroundSize) {
			return;
		}
		StoreDownload(userId, zone, isVideo, url, download, bytes);
	});
}

struct ServerBackground {
	Zone zone = Zone::Full;
	bool isVideo = false;
	QString url;
	// When it was stored, in ms since the epoch, zero if not known.
	qint64 updated = 0;
};

// Read leniently, without it a file is downloaded on every visit and the
// pending changes are sent again whatever the server has.
[[nodiscard]] qint64 ParseUpdated(const nlohmann::json &item) {
	const auto i = item.find("updatedAt");
	if (i == item.end() || !i->is_string()) {
		return 0;
	}
	const auto updated = QDateTime::fromString(
		QString::fromStdString(i->get<std::string>()),
		Qt::ISODateWithMs);
	return updated.isValid() ? updated.toMSecsSinceEpoch() : 0;
}

[[nodiscard]] auto ParseProfileBackgrounds(const QByteArray &payload)
-> std::optional<std::vector<ServerBackground>> {
	try {
		const auto json = nlohmann::json::parse(payload.constData());
		const auto list = json.value("backgrounds", nlohmann::json::array());
		auto result = std::vector<ServerBackground>();
		for (const auto &item : list) {
			const auto zone = ZoneFromName(QString::fromStdString(
				item.value("zone", "")));
			const auto url = QString::fromStdString(item.value("url", ""));
			if (!zone || url.isEmpty()) {
				continue;
			}
			result.push_back({
				.zone = *zone,
				.isVideo = item.value("isVideo", false),
				.url = url,
				.updated = ParseUpdated(item),
			});
		}
		return result;
	} catch (...) {
		LOG(("Whitegram Backend: failed to parse profile backgrounds."));
	}
	return std::nullopt;
}

void ApplyProfileBackgrounds(
		uint64 userId,
		const std::vector<ServerBackground> &list) {
	auto seenZones = std::set<Zone>();
	for (const auto &background : list) {
		const auto zone = background.zone;
		if (PendingChangeFor(userId, zone).change == PendingChange::Remove) {
			// Removed here, the server has not confirmed it yet.
			continue;
		}
		seenZones.insert(zone);
		if (HasLocalCopy(userId, zone)) {
			// The copy set on this device is shown instead, downloading
			// the same file on every visit would only waste traffic.
			continue;
		}
		DownloadBackgroundFile(
			userId,
			zone,
			background.isVideo,
			QUrl(BaseUrl() + background.url),
			background.updated);
	}
	// The zones removed on the server are removed from the disk too,
	// otherwise they come back from there on the next visit.
	auto removed = false;
	for (const auto zone : { Zone::Top, Zone::Bottom, Zone::Full }) {
		if (!seenZones.contains(zone)) {
			removed |= ForgetZone(userId, zone);
		}
	}
	if (removed) {
		NotifyUpdated(userId);
	}
}

[[nodiscard]] QNetworkRequest AuthorizedRequest(const QUrl &url) {
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("X-Whitegram-Key", ApiKey().toUtf8());
	request.setTransferTimeout(kBackendWriteTimeout);
	return request;
}

[[nodiscard]] QUrl ZoneUrl(uint64 userId, Zone zone) {
	return QUrl(BaseUrl()
		+ u"/api/v1/profile-backgrounds/"_q
		+ QString::number(userId)
		+ u'/'
		+ ZoneName(zone));
}

[[nodiscard]] QNetworkReply *SendUpload(
		const QUrl &url,
		const ServerWrite &write) {
	const auto disposition = u"form-data; name=\"file\"; filename=\""_q
		+ write.fileName
		+ u'"';
	const auto multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
	auto filePart = QHttpPart();
	filePart.setHeader(
		QNetworkRequest::ContentDispositionHeader,
		disposition);
	filePart.setHeader(
		QNetworkRequest::ContentTypeHeader,
		QByteArray("application/octet-stream"));
	filePart.setBody(write.bytes);
	multipart->append(filePart);

	auto isVideoPart = QHttpPart();
	isVideoPart.setHeader(
		QNetworkRequest::ContentDispositionHeader,
		QByteArray("form-data; name=\"isVideo\""));
	isVideoPart.setBody(write.isVideo ? "true" : "false");
	multipart->append(isVideoPart);
	auto request = AuthorizedRequest(url);
	// Throttled progress could be far behind, see kStoppableUploadLeft.
	request.setAttribute(
		QNetworkRequest::EmitAllUploadProgressSignalsAttribute,
		true);
	const auto reply = Service::Instance().accessManager().put(
		request,
		multipart);
	multipart->setParent(reply);
	return reply;
}

// The users whose pending changes were sent again in this run.
[[nodiscard]] std::set<uint64> &ReplayedUsers() {
	static auto result = std::set<uint64>();
	return result;
}

void SendNextWrite(uint64 userId, Zone zone);
void ReplayPendingChanges(
	uint64 userId,
	const std::vector<ServerBackground> &list);

// A fetch the user caused sends again the changes the server has not
// confirmed, the fetch sent after the changes does not, so a server that
// keeps failing is not asked again and again.
void SendFetch(uint64 userId, bool replay) {
	HydrateCacheFromDisk(userId);
	auto &sending = FetchStateFor(userId);
	if (WritesPending(userId)) {
		// The answer would be dropped, the backgrounds are fetched once the
		// changes are done.
		sending.refetch = true;
		sending.replay |= replay;
		return;
	}
	const auto url = QUrl(BaseUrl()
		+ u"/api/v1/profile-backgrounds/"_q
		+ QString::number(userId));
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kBackendRequestTimeout);
	const auto number = ++sending.sent;
	++sending.running;
	const auto reply = Service::Instance().accessManager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		auto &state = FetchStateFor(userId);
		--state.running;
		const auto keepReplay = [&] {
			if (number > state.replayedAt) {
				state.replay |= replay;
			}
		};
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Whitegram Backend: background fetch failed for %1: %2"
				).arg(userId).arg(reply->errorString()));
			keepReplay();
			return;
		} else if (WritesPending(userId)) {
			state.refetch = true;
			keepReplay();
			return;
		} else if (number <= state.applied || number <= state.changedAfter) {
			keepReplay();
			return;
		}
		state.applied = number;
		const auto list = ParseProfileBackgrounds(reply->readAll());
		if (!list) {
			keepReplay();
			return;
		}
		if ((replay && number > state.replayedAt) || state.replay) {
			state.replay = false;
			state.replayedAt = state.sent;
			ReplayedUsers().insert(userId);
			ReplayPendingChanges(userId, *list);
		}
		ApplyProfileBackgrounds(userId, *list);
	});
}

void WriteFinished(
		uint64 userId,
		Zone zone,
		not_null<QNetworkReply*> reply,
		const QUrl &url,
		bool remove,
		uint64 generation) {
	reply->deleteLater();
	const auto error = reply->error();
	const auto status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	// The server has no such background left to remove, that's what the
	// removal wanted.
	const auto stored = ((error == QNetworkReply::NoError)
			&& (status >= 200 && status < 300))
		|| (remove && status == 404);
	const auto latest = (WriteGeneration(userId, zone) == generation);
	// A canceled change that is not the latest one was stopped for a newer
	// change, a canceled latest one timed out.
	const auto superseded = !latest
		&& (error == QNetworkReply::OperationCanceledError);
	if (!stored && !superseded) {
		LOG(("Whitegram Backend: background change failed: %1 (%2, %3)"
			).arg(url.toString()).arg(status).arg(reply->errorString()));
	}
	// Sending it again would not help when the server already has it, or
	// when it refuses it for good.
	const auto done = stored
		|| (status >= 400 && status < 500 && status != 408 && status != 429);
	if (done && latest) {
		SetPendingChange(userId, zone, PendingChange::None);
	} else if (!latest) {
		// Whatever the server made of this older change is older than the
		// newer one waiting. Its time would be taken for a change made from
		// another device after the newer one, which would then be dropped.
		const auto pending = PendingChangeFor(userId, zone).change;
		if (pending != PendingChange::None) {
			SetPendingChange(userId, zone, pending);
		}
	}
	if (!remove && latest && done) {
		ResentUploads(userId, zone) = 0;
	}

	// A fetch sent before this moment could be answered with the state
	// from before the change.
	auto &state = FetchStateFor(userId);
	state.changedAfter = state.sent;
	if (state.running > 0) {
		state.refetch = true;
	}

	auto &map = Writes();
	const auto i = map.find({ userId, zone });
	if (i != end(map) && i->second.sending == reply.get()) {
		i->second.sending = nullptr;
		if (i->second.waiting) {
			SendNextWrite(userId, zone);
			return;
		}
		map.erase(i);
	}
	if (state.refetch && !WritesPending(userId)) {
		state.refetch = false;
		SendFetch(userId, false);
	}
}

void SendNextWrite(uint64 userId, Zone zone) {
	auto &map = Writes();
	const auto i = map.find({ userId, zone });
	if (i == end(map) || i->second.sending || !i->second.waiting) {
		return;
	}
	const auto write = std::move(*i->second.waiting);
	i->second.waiting = std::nullopt;
	const auto url = ZoneUrl(userId, zone);
	const auto remove = write.bytes.isEmpty();
	const auto generation = write.generation;
	const auto reply = remove
		? Service::Instance().accessManager().deleteResource(
			AuthorizedRequest(url))
		: SendUpload(url, write);
	i->second.sending = reply;
	i->second.unsent = write.bytes.size();
	if (!remove) {
		QObject::connect(reply, &QNetworkReply::uploadProgress, [=](
				qint64 sent,
				qint64 total) {
			if (total <= 0) {
				return;
			}
			auto &writes = Writes();
			const auto j = writes.find({ userId, zone });
			if (j != end(writes) && j->second.sending == reply) {
				j->second.unsent = total - sent;
			}
		});
	}
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		WriteFinished(userId, zone, reply, url, remove, generation);
	});
}

void QueueWrite(uint64 userId, Zone zone, ServerWrite &&write) {
	auto &entry = Writes()[{ userId, zone }];
	entry.waiting = std::move(write);
	if (!entry.sending) {
		SendNextWrite(userId, zone);
	} else if (entry.unsent > kStoppableUploadLeft) {
		// Most of the upload is not sent, finishing it would only delay the
		// newer change. The waiting change is sent when it finishes.
		entry.sending->abort();
	}
}

// Sends the file of the zone set on this device to the server. The file is
// read away from the main thread and sent from memory: an open file could
// not be replaced or removed on Windows while it uploads.
void StartUpload(
		uint64 userId,
		Zone zone,
		const QString &path,
		bool isVideo,
		bool resend) {
	const auto generation = ++WriteGeneration(userId, zone);
	UploadsBeingRead()[{ userId, zone }] = generation;
	// The copy set here is shown instead of the server one, a download of
	// the older server copy still on its way is outdated.
	DropDownload(userId, zone);
	const auto fileName = QFileInfo(path).fileName();
	crl::async([=] {
		auto file = QFile(path);
		const auto size = file.size();
		auto bytes = QByteArray();
		if (size <= kMaxProfileBackgroundUploadSize
			&& file.open(QIODevice::ReadOnly)) {
			bytes = file.readAll();
		}
		crl::on_main([=, bytes = std::move(bytes)] {
			auto &reading = UploadsBeingRead();
			const auto i = reading.find({ userId, zone });
			if (i != end(reading) && i->second == generation) {
				reading.erase(i);
			}
			if (WriteGeneration(userId, zone) != generation) {
				// Removed or replaced while it was read.
				return;
			} else if (bytes.isEmpty()) {
				// Trying again would fail the same way.
				LOG(("Whitegram Backend: "
					"could not read the background to upload: %1 (%2 bytes)"
					).arg(path).arg(size));
				SetPendingChange(userId, zone, PendingChange::None);
				return;
			} else if (resend) {
				++ResentUploads(userId, zone);
			}
			QueueWrite(userId, zone, ServerWrite{
				.bytes = bytes,
				.fileName = fileName,
				.isVideo = isVideo,
				.generation = generation,
			});
		});
	});
}

// Sends again the changes the server has not confirmed. A zone changed on
// the server after such a change, from another device, keeps the newer
// state. So does a zone the server stored the change for, while its answer
// was lost.
void ReplayPendingChanges(
		uint64 userId,
		const std::vector<ServerBackground> &list) {
	for (const auto zone : { Zone::Top, Zone::Bottom, Zone::Full }) {
		if (Writes().contains({ userId, zone })
			|| UploadsBeingRead().contains({ userId, zone })) {
			continue;
		}
		const auto pending = PendingChangeFor(userId, zone);
		if (pending.change == PendingChange::None) {
			continue;
		}
		const auto i = ranges::find(list, zone, &ServerBackground::zone);
		const auto listed = (i != end(list));
		if (listed && pending.made && i->updated > pending.made) {
			SetPendingChange(userId, zone, PendingChange::None);
			continue;
		}
		switch (pending.change) {
		case PendingChange::None:
			break;
		case PendingChange::Remove:
			if (!listed) {
				// Already gone from the server.
				SetPendingChange(userId, zone, PendingChange::None);
			} else {
				QueueWrite(userId, zone, ServerWrite{
					.generation = ++WriteGeneration(userId, zone),
				});
			}
			break;
		case PendingChange::Upload:
			if (!HasLocalCopy(userId, zone)) {
				SetPendingChange(userId, zone, PendingChange::None);
			} else if (ResentUploads(userId, zone) < kUploadResends) {
				const auto local = AyuSettings::getInstance(
				).customProfileBackground(userId, zone);
				StartUpload(userId, zone, local->path, local->isVideo, true);
			}
			break;
		}
	}
}

// Sends again the changes the server has not confirmed when the client
// starts, they are not left until the own profile is opened. They are
// found by the files that keep them, for every account. A user whose fetch
// failed is tried again the next time the server answers.
void ReplayAllPendingChanges() {
	auto users = std::set<uint64>();
	const auto names = QDir(RemoteCacheDirectory()).entryList(
		{ u"*~pending"_q },
		QDir::Files);
	for (const auto &name : names) {
		auto ok = false;
		const auto userId = name.left(name.indexOf(u'_')).toULongLong(&ok);
		if (ok && userId) {
			users.insert(userId);
		}
	}
	for (const auto userId : users) {
		if (!ReplayedUsers().contains(userId)
			&& !FetchStateFor(userId).running) {
			SendFetch(userId, true);
		}
	}
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
	SetPendingChange(userId, zone, PendingChange::Upload);
	ResentUploads(userId, zone) = 0;
	StartUpload(userId, zone, path, isVideo, false);
}

void RemoveProfileBackground(uint64 userId, Zone zone) {
	SetPendingChange(userId, zone, PendingChange::Remove);
	QueueWrite(userId, zone, ServerWrite{
		.generation = ++WriteGeneration(userId, zone),
	});
	if (ForgetZone(userId, zone)) {
		NotifyUpdated(userId);
	}
}

void FetchProfileBackgrounds(uint64 userId) {
	SendFetch(userId, true);
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
		// Only remembered here, this is called while the backgrounds are
		// resolved, so notifying would reenter that.
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

void SyncTimeSpent(uint64 userId, int64 localMinutes) {
	if (!userId || localMinutes < 0) {
		return;
	}
	const auto url = QUrl(BaseUrl()
		+ u"/api/v1/time-spent/"_q
		+ QString::number(userId));
	const auto get = Service::Instance().accessManager().get(
		AuthorizedRequest(url));
	QObject::connect(get, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { get->deleteLater(); });
		if (get->error() != QNetworkReply::NoError) {
			return;
		}
		int64 remote = 0;
		try {
			const auto data = nlohmann::json::parse(get->readAll().constData());
			remote = data.value("totalMinutes", int64(0));
		} catch (...) {
			return;
		}
		const auto merged = std::max(localMinutes, remote);
		const auto body = QByteArray("{\"totalMinutes\":")
			+ QByteArray::number(merged)
			+ QByteArray("}");
		auto put = AuthorizedRequest(url);
		put.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		const auto reply = Service::Instance().accessManager().put(put, body);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			const auto guard = gsl::finally([&] { reply->deleteLater(); });
			if (reply->error() != QNetworkReply::NoError) {
				return;
			}
			try {
				const auto data = nlohmann::json::parse(reply->readAll().constData());
				const auto server = data.value("totalMinutes", merged);
				if (server > AyuSettings::getInstance().getTotalTimeSpent()) {
					AyuSettings::getInstance().setTotalTimeSpent(server);
				}
			} catch (...) {
			}
		});
	});
}

void FetchClientInfo() {
	const auto url = QUrl(BaseUrl() + u"/api/v1/client-info"_q);
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kBackendRequestTimeout);
	const auto reply = Service::Instance().accessManager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		ParseClientInfo(reply->readAll());
		// The server can be reached now.
		ReplayAllPendingChanges();
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
	if (version >= 7010000 && version < 7020000) {
		const auto offset = version - 7010000;
		const auto major = offset / 10000;
		const auto remainder = offset % 10000;
		const auto minor = remainder / 100;
		const auto patch = remainder % 100;
		return QString::number(1 + major)
			+ u'.'
			+ QString::number(minor)
			+ u'.'
			+ QString::number(patch);
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
	request.setTransferTimeout(kBackendRequestTimeout);
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
