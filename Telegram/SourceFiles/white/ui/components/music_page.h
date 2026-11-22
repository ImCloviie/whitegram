/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "white/features/music/music_service.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>

class QLineEdit;

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace WhiteUi {

class MusicPage final : public Ui::RpWidget {
public:
	MusicPage(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~MusicPage();

	static void Show(not_null<Window::SessionController*> controller);


protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	using Track = Whitegram::Music::Track;
	using Playlist = Whitegram::Music::Playlist;
	using Source = Whitegram::Music::Source;

	enum class Section {
		Home,
		Search,
		Library,
		Playlists,
		Playlist,
	};

	enum class Action {
		None,
		Close,
		Nav,
		Chip,
		Row,
		RowPlay,
		RowMore,
		RowLink,
		Card,
		NewPlaylist,
		Back,
		PlayAll,
		Shuffle,
		Rename,
		Delete,
		Retry,
		NowPrevious,
		NowPlay,
		NowNext,
		NowLink,
	};

	struct Hit {
		QRectF rect;
		Action action = Action::None;
		int index = -1;
	};

	[[nodiscard]] static std::vector<QPointer<MusicPage>> &Opened();

	void appear();
	void disappear();
	void setSection(Section section, const QString &playlistId = QString());
	void runSearch();
	void applySearch(
		int generation,
		bool telegram,
		Whitegram::Music::SearchResult result);
	void activate(const Hit &hit);
	void playFrom(int index, bool shuffle);
	void showTrackMenu(int index);
	void askTitle(
		const QString &title,
		const QString &initial,
		Fn<void(QString)> done);
	void raiseIsland();
	void updateSearchField();

	[[nodiscard]] std::vector<Track> visibleTracks() const;
	[[nodiscard]] const Playlist *openedPlaylist() const;
	[[nodiscard]] std::optional<Track> nowPlaying() const;
	[[nodiscard]] Hit hitAt(QPointF point) const;
	void addHit(QRectF rect, Action action, int index = -1);
	[[nodiscard]] double railWidth() const;
	[[nodiscard]] double stripHeight() const;
	[[nodiscard]] QRectF searchCapsule() const;
	[[nodiscard]] double scrollValue() const;
	void scrollTo(double value, bool animated);
	[[nodiscard]] QFont font(double size, int weight = QFont::Normal) const;
	[[nodiscard]] QImage coverImage(const Track &track);
	[[nodiscard]] QPixmap roundedCover(
		const Track &track,
		int size,
		double radius);
	[[nodiscard]] std::pair<QColor, QColor> toneFor(const Track &track);

	void paintBackground(QPainter &p);
	void paintRail(QPainter &p);
	void paintNavIcon(QPainter &p, int index, QRectF rect, QColor color);
	double paintTitle(
		QPainter &p,
		double top,
		const QString &title,
		const QString &subtitle);
	double paintHome(QPainter &p, double top);
	double paintSearch(QPainter &p, double top);
	double paintLibrary(QPainter &p, double top);
	double paintPlaylists(QPainter &p, double top, int limit);
	double paintPlaylist(QPainter &p, double top);
	double paintRows(
		QPainter &p,
		const std::vector<Track> &tracks,
		double top,
		int limit = -1);
	void paintRow(
		QPainter &p,
		const Track &track,
		int index,
		QRectF rect);
	void paintCoverBox(
		QPainter &p,
		const Track &track,
		QRectF rect,
		double radius);
	void paintCollage(
		QPainter &p,
		const Playlist &playlist,
		QRectF rect,
		double radius);
	void paintBadge(QPainter &p, const Track &track, QRectF rect, int index);
	double paintPill(
		QPainter &p,
		QPointF topLeft,
		const QString &text,
		bool filled,
		Action action,
		int index = -1);
	void paintHint(QPainter &p, double top, const QString &text);
	void paintSpinner(QPainter &p, QRectF rect, QColor color);
	void paintNowPlaying(QPainter &p);
	void paintClose(QPainter &p);

	const not_null<Window::SessionController*> _controller;
	const not_null<Main::Session*> _session;

	Section _section = Section::Home;
	QString _playlistId;
	bool _shown = false;
	Ui::Animations::Simple _showAnimation;
	Ui::Animations::Simple _sectionAnimation;
	Ui::Animations::Simple _scrollAnimation;
	Ui::Animations::Basic _spinner;

	QLineEdit *_searchField = nullptr;
	base::Timer _searchTimer;
	Source _source = Source::All;
	QString _query;
	int _searchGeneration = 0;
	int _searchPending = 0;
	std::vector<Track> _externalResults;
	std::vector<Track> _telegramResults;
	QString _searchError;

	std::vector<Track> _saved;
	double _scroll = 0.;
	double _scrollMax = 0.;
	QRectF _clip;

	std::vector<Hit> _hits;
	Hit _hover;
	std::vector<Track> _paintedTracks;

	base::flat_map<
		not_null<DocumentData*>,
		std::shared_ptr<Data::DocumentMedia>> _media;
	base::flat_map<QString, QPixmap> _covers;
	base::flat_map<QString, std::pair<QColor, QColor>> _tones;
	mutable base::flat_map<int, QFont> _fonts;

	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace AyuUi
