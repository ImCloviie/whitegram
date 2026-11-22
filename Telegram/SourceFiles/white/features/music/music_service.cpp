/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "white/features/music/music_service.h"

#include "white/features/whitegram_backend/whitegram_backend.h"
#include "apiwrap.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_location.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "media/media_common.h"
#include "media/player/media_player_instance.h"
#include "storage/storage_shared_media.h"
#include "ui/text/format_song_document_name.h"

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Whitegram::Music {
namespace {

constexpr auto kBackendTimeout = 20 * 1000;
constexpr auto kDownloadTimeout = 60 * 1000;
constexpr auto kMaxCovers = 400;
constexpr auto kCoverSide = 600;
constexpr auto kMaxCachedFiles = 60;
constexpr auto kMaxAudioSize = qint64(150 * 1024 * 1024);
constexpr auto kSyntheticMask = 0x7757000000000000ULL;

using Media::OrderMode;
using Media::RepeatMode;

[[nodiscard]] QNetworkAccessManager &Network() {
	static QNetworkAccessManager manager;
	return manager;
}

[[nodiscard]] QNetworkRequest BackendRequest(
		not_null<Main::Session*> session,
		const QString &path) {
	Backend::EnsureOwnerToken(session);
	auto request = QNetworkRequest(QUrl(Backend::BaseUrl() + path));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kBackendTimeout);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	Backend::ApplyTlsPinning(request);
	Backend::ApplyOwnerToken(request, session->userId().bare);
	return request;
}

void OnJson(QNetworkReply *reply, Fn<void(QJsonObject, QString)> done) {
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto json = QJsonDocument::fromJson(reply->readAll()).object();
		if (reply->error() == QNetworkReply::NoError) {
			done(json, QString());
		} else if (status == 401) {
			done({}, QString::fromUtf8(
				"Нет доступа к серверу Whitegram, повторите через минуту"));
		} else {
			const auto error = json.value(u"error"_q).toString();
			done({}, error.isEmpty() ? reply->errorString() : error);
		}
	});
}

[[nodiscard]] Track ParseTrack(const QJsonObject &json) {
	auto result = Track();
	result.source = json.value(u"source"_q).toString();
	result.id = json.value(u"id"_q).toString();
	result.title = json.value(u"title"_q).toString();
	result.artist = json.value(u"artist"_q).toString();
	result.uploader = json.value(u"uploader"_q).toString();
	result.uploaderUrl = json.value(u"uploaderUrl"_q).toString();
	result.durationMs = int64(json.value(u"durationMs"_q).toDouble());
	result.artworkUrl = json.value(u"artworkUrl"_q).toString();
	result.permalink = json.value(u"permalink"_q).toString();
	result.streamable = json.value(u"streamable"_q).toBool(true);
	const auto ref = json.value(u"ref"_q).toObject();
	if (!ref.isEmpty()) {
		const auto peer = ref.value(u"peerId"_q).toString().toULongLong();
		const auto msg = ref.value(u"msgId"_q).toString().toLongLong();
		if (peer && msg) {
			result.context = FullMsgId(PeerId(peer), MsgId(msg));
		}
	}
	return result;
}

[[nodiscard]] QJsonObject TrackJson(const Track &track) {
	auto result = QJsonObject{
		{ u"source"_q, track.source },
		{ u"id"_q, track.id },
		{ u"title"_q, track.title },
		{ u"artist"_q, track.artist },
		{ u"uploader"_q, track.uploader },
		{ u"uploaderUrl"_q, track.uploaderUrl },
		{ u"durationMs"_q, double(track.durationMs) },
		{ u"artworkUrl"_q, track.artworkUrl },
		{ u"permalink"_q, track.permalink },
	};
	if (track.telegram()) {
		result.insert(u"ref"_q, QJsonObject{
			{ u"peerId"_q, QString::number(track.context.peer.value) },
			{ u"msgId"_q, QString::number(track.context.msg.bare) },
			{ u"documentId"_q, track.id },
		});
	}
	return result;
}

[[nodiscard]] Playlist ParsePlaylist(const QJsonObject &json) {
	auto result = Playlist{
		.id = json.value(u"id"_q).toString(),
		.title = json.value(u"title"_q).toString(),
	};
	for (const auto &value : json.value(u"tracks"_q).toArray()) {
		result.tracks.push_back(ParseTrack(value.toObject()));
	}
	return result;
}

[[nodiscard]] std::optional<Track> FromItem(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document || !document->isAudioFile()) {
		return std::nullopt;
	}
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	auto result = Track();
	result.source = u"telegram"_q;
	result.id = QString::number(document->id);
	result.title = name.title;
	result.artist = name.performer;
	result.durationMs = document->duration();
	result.document = document;
	result.context = item->fullId();
	return result;
}

[[nodiscard]] bool ExternalSource(const QString &source) {
	return (source == u"soundcloud"_q)
		|| (source == u"audius"_q)
		|| (source == u"jamendo"_q);
}

[[nodiscard]] QString SourceName(Source source) {
	switch (source) {
	case Source::SoundCloud: return u"soundcloud"_q;
	case Source::Audius: return u"audius"_q;
	case Source::Jamendo: return u"jamendo"_q;
	default: return u"all"_q;
	}
}

void SearchTelegram(
		not_null<Main::Session*> session,
		const QString &query,
		Fn<void(SearchResult)> done) {
	const auto request = Api::PrepareGlobalMediaRequest(
		session,
		0,
		Data::MessagePosition(),
		Storage::SharedMediaType::MusicFile,
		query);
	if (!request) {
		done({});
		return;
	}
	auto copy = *request;
	session->api().request(
		std::move(copy)
	).done([=](const MTPmessages_Messages &result) {
		const auto parsed = Api::ParseGlobalMediaResult(session, result, false);
		auto answer = SearchResult();
		for (const auto &position : parsed.messageIds) {
			if (const auto item = session->data().message(position.fullId)) {
				if (auto track = FromItem(item)) {
					answer.tracks.push_back(std::move(*track));
				}
			}
		}
		done(std::move(answer));
	}).fail([=](const MTP::Error &error) {
		done({ .error = error.type() });
	}).send();
}

struct CoverState {
	base::flat_map<QString, QImage> images;
	base::flat_set<QString> loading;
	std::deque<QString> order;
	rpl::event_stream<QString> loaded;
};

[[nodiscard]] CoverState &Covers() {
	static CoverState state;
	return state;
}

[[nodiscard]] base::flat_map<DocumentId, Track> &Externals() {
	static base::flat_map<DocumentId, Track> map;
	return map;
}

[[nodiscard]] QString CacheDir() {
	return cWorkingDir() + u"tdata/whitegram/music/"_q;
}

[[nodiscard]] QString CachePath(const Track &track) {
	const auto hash = QCryptographicHash::hash(
		track.key().toUtf8(),
		QCryptographicHash::Sha1).toHex().left(24);
	return CacheDir() + QString::fromLatin1(hash) + u".mp3"_q;
}

void PruneCache(const QString &keep) {
	auto files = QDir(CacheDir()).entryInfoList(
		{ u"*.mp3"_q },
		QDir::Files,
		QDir::Time);
	for (auto i = kMaxCachedFiles; i < files.size(); ++i) {
		if (files[i].absoluteFilePath() != QFileInfo(keep).absoluteFilePath()) {
			QFile::remove(files[i].absoluteFilePath());
		}
	}
}

struct Download {
	std::vector<Fn<void(QString, QString)>> callbacks;
};

[[nodiscard]] base::flat_map<QString, Download> &Downloads() {
	static base::flat_map<QString, Download> map;
	return map;
}

void FinishDownload(const QString &key, const QString &path, const QString &error) {
	auto &map = Downloads();
	const auto i = map.find(key);
	if (i == end(map)) {
		return;
	}
	const auto callbacks = std::move(i->second.callbacks);
	map.erase(i);
	for (const auto &callback : callbacks) {
		if (callback) {
			callback(path, error);
		}
	}
}

void FetchAudio(
		not_null<Main::Session*> session,
		const Track &track,
		Fn<void(QString path, QString error)> done) {
	const auto path = CachePath(track);
	if (QFileInfo(path).size() > 0) {
		if (done) {
			done(path, QString());
		}
		return;
	}
	const auto key = track.key();
	auto &map = Downloads();
	if (const auto i = map.find(key); i != end(map)) {
		i->second.callbacks.push_back(std::move(done));
		return;
	}
	map[key].callbacks.push_back(std::move(done));
	if (!ExternalSource(track.source)) {
		FinishDownload(key, QString(), u"unknown source"_q);
		return;
	}
	const auto request = BackendRequest(
		session,
		u"/api/v1/music/stream/"_q
			+ track.source
			+ u'/'
			+ QString::fromUtf8(QUrl::toPercentEncoding(track.id)));
	OnJson(Network().get(request), [=](QJsonObject json, QString error) {
		const auto url = QUrl(json.value(u"url"_q).toString());
		if (!error.isEmpty() || url.scheme() != u"https"_q) {
			FinishDownload(key, QString(), error.isEmpty()
				? QString::fromUtf8("Сервер не дал ссылку на трек")
				: error);
			return;
		}
		auto audio = QNetworkRequest(url);
		audio.setAttribute(
			QNetworkRequest::RedirectPolicyAttribute,
			QNetworkRequest::NoLessSafeRedirectPolicy);
		audio.setTransferTimeout(kDownloadTimeout);
		QDir().mkpath(CacheDir());
		const auto part = path + u".part"_q;
		const auto file = std::make_shared<QFile>(part);
		if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			FinishDownload(key, QString(), u"cache write failed"_q);
			return;
		}
		const auto reply = Network().get(audio);
		const auto failure = std::make_shared<QString>();
		const auto store = [=] {
			const auto bytes = reply->readAll();
			if (!failure->isEmpty() || bytes.isEmpty()) {
				return;
			} else if (file->size() + bytes.size() > kMaxAudioSize) {
				*failure = QString::fromUtf8("Трек слишком большой");
				reply->abort();
			} else if (file->write(bytes) != bytes.size()) {
				*failure = u"cache write failed"_q;
				reply->abort();
			}
		};
		QObject::connect(reply, &QNetworkReply::readyRead, store);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			reply->deleteLater();
			if (failure->isEmpty() && reply->error() == QNetworkReply::NoError) {
				store();
			}
			file->close();
			if (!failure->isEmpty()
				|| reply->error() != QNetworkReply::NoError
				|| file->size() <= 0) {
				QFile::remove(part);
				FinishDownload(key, QString(), failure->isEmpty()
					? reply->errorString()
					: *failure);
				return;
			}
			QFile::remove(path);
			QFile::rename(part, path);
			PruneCache(path);
			FinishDownload(key, path, QString());
		});
	});
}

[[nodiscard]] DocumentId SyntheticId(const QString &key) {
	const auto hash = QCryptographicHash::hash(
		key.toUtf8(),
		QCryptographicHash::Sha1);
	auto value = uint64();
	memcpy(&value, hash.constData(), sizeof(value));
	return DocumentId((value & 0x0000FFFFFFFFFFFFULL) | kSyntheticMask);
}

} // namespace

QString Track::key() const {
	return source + u':' + id;
}

bool Track::telegram() const {
	return source == u"telegram"_q;
}

bool Track::soundcloud() const {
	return source == u"soundcloud"_q;
}

void Search(
		not_null<Main::Session*> session,
		const QString &query,
		Source source,
		Fn<void(SearchResult)> done) {
	if (source == Source::Telegram) {
		SearchTelegram(session, query, std::move(done));
		return;
	}
	auto params = QUrlQuery();
	params.addQueryItem(u"q"_q, query);
	params.addQueryItem(u"source"_q, SourceName(source));
	const auto request = BackendRequest(
		session,
		u"/api/v1/music/search?"_q
			+ params.toString(QUrl::FullyEncoded));
	OnJson(Network().get(request), [=](QJsonObject json, QString error) {
		auto result = SearchResult{ .error = error };
		for (const auto &value : json.value(u"results"_q).toArray()) {
			auto track = ParseTrack(value.toObject());
			if (!track.id.isEmpty()) {
				result.tracks.push_back(std::move(track));
			}
		}
		done(std::move(result));
	});
}

std::vector<Track> SavedTracks(not_null<Main::Session*> session) {
	auto &saved = session->data().savedMusic();
	const auto peerId = session->userPeerId();
	if (!saved.fullyLoaded(peerId)) {
		saved.loadMore(peerId);
	}
	auto result = std::vector<Track>();
	for (const auto &item : saved.list(peerId)) {
		if (auto track = FromItem(item)) {
			result.push_back(std::move(*track));
		}
	}
	return result;
}

rpl::producer<> SavedTracksChanges(not_null<Main::Session*> session) {
	const auto peerId = session->userPeerId();
	return session->data().savedMusic().changed(
	) | rpl::filter(rpl::mappers::_1 == peerId) | rpl::to_empty;
}

Library &Library::Instance() {
	static Library instance;
	return instance;
}

void Library::load(not_null<Main::Session*> session) {
	_session = session;
	_userId = session->userId().bare;
	_loading = true;
	_changes.fire({});
	const auto userId = _userId;
	OnJson(
		Network().get(BackendRequest(session, u"/api/v1/playlists"_q)),
		[=](QJsonObject json, QString error) {
			if (_userId != userId) {
				return;
			}
			_loading = false;
			_error = error;
			if (error.isEmpty()) {
				_playlists.clear();
				for (const auto &value : json.value(u"playlists"_q).toArray()) {
					_playlists.push_back(ParsePlaylist(value.toObject()));
				}
			}
			_changes.fire({});
		});
}

const std::vector<Playlist> &Library::playlists() const {
	return _playlists;
}

const Playlist *Library::find(const QString &id) const {
	const auto i = ranges::find(_playlists, id, &Playlist::id);
	return (i != end(_playlists)) ? &*i : nullptr;
}

rpl::producer<> Library::changes() const {
	return _changes.events();
}

bool Library::loading() const {
	return _loading;
}

QString Library::error() const {
	return _error;
}

void Library::send(
		const QByteArray &verb,
		const QString &path,
		const QByteArray &body,
		Fn<void(QJsonObject)> done) {
	if (!_session) {
		return;
	}
	const auto session = _session;
	const auto userId = _userId;
	const auto request = BackendRequest(session, u"/api/v1/playlists"_q + path);
	auto buffer = new QBuffer();
	buffer->setData(body);
	buffer->open(QIODevice::ReadOnly);
	const auto reply = Network().sendCustomRequest(request, verb, buffer);
	buffer->setParent(reply);
	OnJson(reply, [=](QJsonObject json, QString error) {
		if (_userId != userId) {
			return;
		}
		if (!error.isEmpty()) {
			_error = error;
			load(session);
			return;
		}
		_error = QString();
		if (json.contains(u"playlist"_q)) {
			applyPlaylist(json.value(u"playlist"_q).toObject());
		}
		if (done) {
			done(json);
		}
	});
}

void Library::applyPlaylist(const QJsonObject &json) {
	auto playlist = ParsePlaylist(json);
	const auto i = ranges::find(_playlists, playlist.id, &Playlist::id);
	if (i != end(_playlists)) {
		*i = std::move(playlist);
	} else {
		_playlists.push_back(std::move(playlist));
	}
	_changes.fire({});
}

void Library::create(const QString &title, Fn<void(QString)> done) {
	const auto body = QJsonDocument(QJsonObject{
		{ u"title"_q, title },
	}).toJson(QJsonDocument::Compact);
	send("POST", QString(), body, [=](QJsonObject json) {
		if (done) {
			done(json.value(u"playlist"_q).toObject().value(u"id"_q).toString());
		}
	});
}

void Library::rename(const QString &id, const QString &title) {
	const auto i = ranges::find(_playlists, id, &Playlist::id);
	if (i == end(_playlists)) {
		return;
	}
	i->title = title;
	_changes.fire({});
	send("PUT", u'/' + id, QJsonDocument(QJsonObject{
		{ u"title"_q, title },
	}).toJson(QJsonDocument::Compact));
}

void Library::remove(const QString &id) {
	_playlists.erase(
		ranges::remove(_playlists, id, &Playlist::id),
		end(_playlists));
	_changes.fire({});
	send("DELETE", u'/' + id, QByteArray());
}

void Library::add(const QString &id, const Track &track) {
	const auto i = ranges::find(_playlists, id, &Playlist::id);
	if (i == end(_playlists)) {
		return;
	}
	if (ranges::find(i->tracks, track.key(), &Track::key) == end(i->tracks)) {
		i->tracks.push_back(track);
		_changes.fire({});
	}
	send("POST", u'/' + id + u"/tracks"_q, QJsonDocument(QJsonObject{
		{ u"track"_q, TrackJson(track) },
	}).toJson(QJsonDocument::Compact));
}

void Library::removeTrack(const QString &id, const Track &track) {
	const auto i = ranges::find(_playlists, id, &Playlist::id);
	if (i == end(_playlists)) {
		return;
	}
	i->tracks.erase(
		ranges::remove(i->tracks, track.key(), &Track::key),
		end(i->tracks));
	_changes.fire({});
	send(
		"DELETE",
		u'/' + id + u"/tracks/"_q
			+ QString::fromUtf8(QUrl::toPercentEncoding(track.source))
			+ u'/'
			+ QString::fromUtf8(QUrl::toPercentEncoding(track.id)),
		QByteArray());
}

void Library::move(const QString &id, int from, int to) {
	const auto i = ranges::find(_playlists, id, &Playlist::id);
	const auto count = (i != end(_playlists)) ? int(i->tracks.size()) : 0;
	if (from < 0 || to < 0 || from >= count || to >= count || from == to) {
		return;
	}
	auto moved = std::move(i->tracks[from]);
	i->tracks.erase(begin(i->tracks) + from);
	i->tracks.insert(begin(i->tracks) + to, std::move(moved));
	_changes.fire({});
	send("POST", u'/' + id + u"/tracks/move"_q, QJsonDocument(QJsonObject{
		{ u"from"_q, from },
		{ u"to"_q, to },
	}).toJson(QJsonDocument::Compact));
}

QImage Cover(const QString &url) {
	if (url.isEmpty()) {
		return QImage();
	}
	auto &state = Covers();
	if (const auto i = state.images.find(url); i != end(state.images)) {
		return i->second;
	} else if (state.loading.contains(url)) {
		return QImage();
	}
	state.loading.emplace(url);
	auto request = QNetworkRequest(QUrl(url));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kBackendTimeout);
	const auto reply = Network().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto bytes = reply->readAll();
		crl::async([=] {
			auto image = QImage::fromData(bytes);
			if (!image.isNull() && std::max(image.width(), image.height()) > kCoverSide) {
				image = image.scaled(
					kCoverSide,
					kCoverSide,
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
			}
			crl::on_main([=, image = std::move(image)]() mutable {
				auto &state = Covers();
				state.loading.remove(url);
				state.images[url] = std::move(image);
				state.order.push_back(url);
				while (state.order.size() > kMaxCovers) {
					state.images.remove(state.order.front());
					state.order.pop_front();
				}
				state.loaded.fire_copy(url);
			});
		});
	});
	return QImage();
}

rpl::producer<QString> CoverLoaded() {
	return Covers().loaded.events();
}

Queue &Queue::Instance() {
	static Queue instance;
	return instance;
}

void Queue::play(
		not_null<Main::Session*> session,
		std::vector<Track> tracks,
		int index,
		bool shuffle) {
	if (index < 0 || index >= int(tracks.size())) {
		return;
	}
	_session = session;
	_tracks = std::move(tracks);
	_order = ranges::views::ints(0, int(_tracks.size()))
		| ranges::to_vector;
	shuffle = shuffle
		|| (Core::App().settings().playerOrderMode() == OrderMode::Shuffle);
	if (shuffle) {
		std::swap(_order[0], _order[index]);
		for (auto i = int(_order.size()) - 1; i > 1; --i) {
			std::swap(_order[i], _order[1 + base::RandomIndex(i)]);
		}
		index = 0;
	} else {
		index = int(ranges::find(_order, index) - begin(_order));
	}
	if (!_lifetime) {
		session->lifetime().add([=] {
			if (_session == session) {
				_session = nullptr;
				_tracks.clear();
				_order.clear();
				_position = _preparing = -1;
				_document = nullptr;
				_lifetime.destroy();
			}
		});
		Media::Player::instance()->trackChanged(
		) | rpl::on_next([=](AudioMsgId::Type) {
			_changes.fire({});
		}, _lifetime);
	}
	start(index);
}

const Track *Queue::current() const {
	return (active() && _position >= 0)
		? &_tracks[_order[_position]]
		: nullptr;
}

const Track *Queue::preparing() const {
	return (_preparing >= 0) ? &_tracks[_order[_preparing]] : nullptr;
}

rpl::producer<> Queue::changes() const {
	return _changes.events();
}

QString Queue::lastError() const {
	return _lastError;
}

bool Queue::active() const {
	if (!_session || !_document || _position < 0) {
		return false;
	}
	const auto current = Media::Player::instance()->current(
		AudioMsgId::Type::Song);
	return current.audio() == _document;
}

int Queue::target(int delta, bool autonext) const {
	const auto count = int(_order.size());
	const auto from = (_preparing >= 0) ? _preparing : _position;
	if (from < 0 || !count) {
		return -1;
	}
	const auto &settings = Core::App().settings();
	if (settings.playerOrderMode() == OrderMode::Reverse) {
		delta = -delta;
	}
	auto result = from + delta;
	if (result < 0 || result >= count) {
		if (settings.playerRepeatMode() != RepeatMode::All
			&& (autonext || result >= count)) {
			return -1;
		}
		result = (result + count) % count;
	}
	return result;
}

bool Queue::canSkip(int delta) const {
	return target(delta, false) >= 0;
}

bool Queue::skip(int delta, bool autonext) {
	const auto position = target(delta, autonext);
	if (position < 0) {
		return false;
	}
	start(position);
	return true;
}

void Queue::start(int position) {
	const auto generation = ++_generation;
	auto &track = _tracks[_order[position]];
	_preparing = position;
	_lastError = QString();
	_changes.fire({});
	const auto session = _session;
	const auto fail = [=](const QString &error) {
		if (_generation != generation) {
			return;
		}
		_preparing = -1;
		_lastError = error;
		_changes.fire({});
	};
	if (track.telegram()) {
		if (!track.document && track.context) {
			if (const auto item = session->data().message(track.context)) {
				if (const auto found = FromItem(item)) {
					track.document = found->document;
				}
			} else if (const auto peer = session->data().peerLoaded(
					track.context.peer)) {
				session->api().requestMessageData(
					peer,
					track.context.msg,
					crl::guard(session, [=] {
						if (_generation == generation
							&& session->data().message(track.context)) {
							start(position);
						} else {
							fail(QString::fromUtf8("Сообщение с треком недоступно"));
						}
					}));
				return;
			}
		}
		if (!track.document) {
			fail(QString::fromUtf8("Трек из Telegram недоступен"));
			return;
		}
		_document = track.document;
		_position = position;
		_preparing = -1;
		Media::Player::instance()->play(
			AudioMsgId(track.document, track.context));
		_changes.fire({});
		prefetch(position + 1);
		return;
	}
	if (!track.streamable) {
		fail(QString::fromUtf8("Источник не даёт слушать этот трек"));
		return;
	}
	FetchAudio(session, track, [=](QString path, QString error) {
		if (_generation != generation || _session != session) {
			return;
		} else if (path.isEmpty()) {
			fail(error);
			return;
		}
		startExternal(position, path);
	});
}

void Queue::startExternal(int position, const QString &path) {
	const auto &track = _tracks[_order[position]];
	const auto id = SyntheticId(track.key());
	using Flag = MTPDdocumentAttributeAudio::Flag;
	const auto attributes = QVector<MTPDocumentAttribute>{
		MTP_documentAttributeAudio(
			MTP_flags(Flag::f_title | Flag::f_performer),
			MTP_int(int(track.durationMs / 1000)),
			MTP_string(track.title),
			MTP_string(track.artist),
			MTPbytes()),
		MTP_documentAttributeFilename(MTP_string(
			track.artist + u" - "_q + track.title + u".mp3"_q)),
	};
	Externals()[id] = track;
	const auto document = _session->data().document(
		id,
		0,
		QByteArray(),
		base::unixtime::now(),
		attributes,
		u"audio/mpeg"_q,
		InlineImageLocation(),
		ImageWithLocation(),
		ImageWithLocation(),
		false,
		0,
		QFileInfo(path).size());
	document->setLocation(Core::FileLocation(path));
	_document = document;
	_position = position;
	_preparing = -1;
	Media::Player::instance()->play(AudioMsgId(document, FullMsgId()));
	_changes.fire({});
	prefetch(position + 1);
}

void Queue::prefetch(int position) {
	if (position < 0 || position >= int(_order.size()) || !_session) {
		return;
	}
	const auto &track = _tracks[_order[position]];
	if (!track.telegram() && track.streamable) {
		FetchAudio(_session, track, nullptr);
	}
}

const Track *ExternalTrack(not_null<const DocumentData*> document) {
	const auto &map = Externals();
	const auto i = map.find(document->id);
	return (i != end(map)) ? &i->second : nullptr;
}

bool IsExternal(not_null<const DocumentData*> document) {
	return (document->id & 0xFFFF000000000000ULL) == kSyntheticMask
		&& ExternalTrack(document) != nullptr;
}

} // namespace Whitegram::Music
