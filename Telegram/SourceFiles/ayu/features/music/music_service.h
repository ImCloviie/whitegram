/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

#include <QtCore/QJsonObject>
#include <QtGui/QImage>

class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Whitegram::Music {

struct Track {
	QString source;
	QString id;
	QString title;
	QString artist;
	QString uploader;
	QString uploaderUrl;
	int64 durationMs = 0;
	QString artworkUrl;
	QString permalink;
	bool streamable = true;
	DocumentData *document = nullptr;
	FullMsgId context;

	[[nodiscard]] QString key() const;
	[[nodiscard]] bool telegram() const;
	[[nodiscard]] bool soundcloud() const;
};

struct Playlist {
	QString id;
	QString title;
	std::vector<Track> tracks;
};

enum class Source {
	All,
	SoundCloud,
	Audius,
	Jamendo,
	Telegram,
};

struct SearchResult {
	std::vector<Track> tracks;
	QString error;
};

void Search(
	not_null<Main::Session*> session,
	const QString &query,
	Source source,
	Fn<void(SearchResult)> done);

[[nodiscard]] std::vector<Track> SavedTracks(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<> SavedTracksChanges(
	not_null<Main::Session*> session);

class Library final {
public:
	[[nodiscard]] static Library &Instance();

	void load(not_null<Main::Session*> session);
	[[nodiscard]] const std::vector<Playlist> &playlists() const;
	[[nodiscard]] const Playlist *find(const QString &id) const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] QString error() const;

	void create(const QString &title, Fn<void(QString id)> done = nullptr);
	void rename(const QString &id, const QString &title);
	void remove(const QString &id);
	void add(const QString &id, const Track &track);
	void removeTrack(const QString &id, const Track &track);
	void move(const QString &id, int from, int to);

private:
	void send(
		const QByteArray &verb,
		const QString &path,
		const QByteArray &body,
		Fn<void(QJsonObject)> done = nullptr);
	void applyPlaylist(const QJsonObject &json);

	Main::Session *_session = nullptr;
	uint64 _userId = 0;
	std::vector<Playlist> _playlists;
	rpl::event_stream<> _changes;
	bool _loading = false;
	QString _error;

};

[[nodiscard]] QImage Cover(const QString &url);
[[nodiscard]] rpl::producer<QString> CoverLoaded();

class Queue final {
public:
	[[nodiscard]] static Queue &Instance();

	void play(
		not_null<Main::Session*> session,
		std::vector<Track> tracks,
		int index,
		bool shuffle = false);
	[[nodiscard]] const Track *current() const;
	[[nodiscard]] const Track *preparing() const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] QString lastError() const;

	[[nodiscard]] bool active() const;
	[[nodiscard]] bool canSkip(int delta) const;
	bool skip(int delta, bool autonext);

private:
	[[nodiscard]] int target(int delta, bool autonext) const;
	void start(int position);
	void startExternal(int position, const QString &path);
	void prefetch(int position);

	Main::Session *_session = nullptr;
	std::vector<Track> _tracks;
	std::vector<int> _order;
	int _position = -1;
	int _preparing = -1;
	DocumentData *_document = nullptr;
	int _generation = 0;
	QString _lastError;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

[[nodiscard]] const Track *ExternalTrack(not_null<const DocumentData*> document);
[[nodiscard]] bool IsExternal(not_null<const DocumentData*> document);

} // namespace Whitegram::Music
