// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/ui/utils/lyrics_engine.h"
#include "ayu/ui/utils/track_analysis.h"
#include "data/data_audio_msg_id.h"
#include "media/audio/media_audio.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"
#include "base/unique_qptr.h"
#include <QPointer>
#include <QTimer>

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Media::View {
class PlaybackProgress;
} // namespace Media::View

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace AyuUi {
class LyricsWindow;
} // namespace AyuUi

namespace AyuUi {

class MusicIsland final : public Ui::RpWidget {
public:
	MusicIsland(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~MusicIsland();

	[[nodiscard]] bool replacesTopBar(AudioMsgId::Type type) const;
	[[nodiscard]] rpl::producer<bool> shownChanges() const;
	void refresh();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Part {
		None,
		Body,
		Cover,
		Previous,
		PlayPause,
		Next,
		Close,
		Seek,
		VolumeToggle,
		Volume,
		SaveProfile,
		LyricsButton,
		LyricsZone,
		Repeat,
		Order,
		Speed,
	};

	struct CoverData {
		QPixmap art;
		QPixmap blurred;
		QColor accent;
		bool hasArt = false;
	};

	struct Layout {
		QRectF island;
		QRectF cover;
		QRectF previous;
		QRectF playPause;
		QRectF next;
		QRectF repeat;
		QRectF order;
		QRectF speed;
		QRectF close;
		QRectF titleZone;
		QRectF seek;
		QRectF volumeToggle;
		QRectF volume;
		QRectF saveProfile;
		QRectF lyricsButton;
		QRectF lyricsBox;
		QRectF equalizer;
		QRectF collapsedProgress;
		QPointF title;
		QPointF performer;
		int textWidth = 0;
		float64 timesTop = 0.;
		float64 radius = 0.;
	};

	void setupPlayer();
	void setupSettings();

	void handleTrackChanged();
	void handleStateUpdate(const Media::Player::TrackState &state);
	void checkVisibility();
	void toggleShown(bool shown);
	[[nodiscard]] bool desiredExpanded() const;
	void updateExpanded();
	void updatePosition();
	void updateCursor();
	void updateHovered(QPoint point);
	void checkLiveAnimation();
	void pollHover();

	[[nodiscard]] float64 expandProgress() const;
	[[nodiscard]] float64 shownProgress() const;
	[[nodiscard]] QSize islandSize(float64 expand) const;
	[[nodiscard]] Layout computeLayout(float64 expand) const;
	[[nodiscard]] Part partAt(QPoint point) const;
	[[nodiscard]] QPoint freeTopLeft(QSize outer) const;
	void saveFreePosition();
	[[nodiscard]] float64 displayedProgress() const;
	[[nodiscard]] float64 seekProgressFor(QPoint point) const;
	[[nodiscard]] bool fitsInParent() const;
	[[nodiscard]] bool artMode() const;
	[[nodiscard]] QColor titleFg() const;
	[[nodiscard]] QColor subFg() const;
	[[nodiscard]] QColor iconFg() const;

	void loadCover();
	void buildCover(QImage source);
	void applyCover(CoverData cover, int generation);
	void loadCustomBackground();

	void activate(Part part, QPoint onPoint);
	void hintLockedPosition();
	void showTrackMessage();
	void saveToProfile();
	void toggleProfileMusic();
	[[nodiscard]] int profileMusicState() const;
	void applySongVolume(float64 volume);
	void toggleRepeat();
	void showOrderMenu();
	void showSpeedMenu();
	void applySpeed(float64 speed);
	void showMenuBelow(const QRectF &anchor);
	[[nodiscard]] bool hasSpeedControl() const;
	[[nodiscard]] float64 currentSpeed() const;

	[[nodiscard]] bool canSaveToProfile() const;
	[[nodiscard]] float64 songVolume() const;
	[[nodiscard]] float64 volumeProgressFor(QPoint point) const;
	[[nodiscard]] const style::icon &volumeIcon() const;

	void paintShadow(QPainter &p, const Layout &layout) const;
	void paintBackground(QPainter &p, const Layout &layout) const;
	void paintCoverArt(QPainter &p, const Layout &layout) const;
	void paintTexts(QPainter &p, const Layout &layout, float64 expand) const;
	void paintControls(QPainter &p, const Layout &layout, float64 opacity) const;
	void paintSeek(QPainter &p, const Layout &layout, float64 opacity) const;
	void paintVolume(QPainter &p, const Layout &layout, float64 opacity) const;
	void paintModes(QPainter &p, const Layout &layout, float64 opacity) const;
	void paintTimes(QPainter &p, const Layout &layout, float64 opacity) const;
	void paintLyrics(QPainter &p, const Layout &layout, float64 opacity) const;
	void paintFloatingLine(QPainter &p, const Layout &layout) const;
	void paintEqualizer(QPainter &p, const Layout &layout, float64 opacity) const;
	void paintCollapsedProgress(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const;

	const not_null<Window::SessionController*> _controller;
	const std::unique_ptr<Media::View::PlaybackProgress> _playback;

	AudioMsgId _songId;
	FullMsgId _contextId;
	Ui::Text::String _title;
	Ui::Text::String _performer;
	QString _titleRaw;
	QString _performerRaw;
	QString _playedText;
	QString _durationText;
	int64 _durationSeconds = 0;

	std::shared_ptr<Data::DocumentMedia> _mediaView;
	CoverData _cover;
	QPixmap _customBackground;
	int _coverGeneration = 0;
	rpl::lifetime _coverLifetime;

	Ui::Animations::Simple _shownAnimation;
	Ui::Animations::Simple _expandAnimation;
	Ui::Animations::Basic _liveAnimation;
	QTimer _hoverPoll;

	rpl::event_stream<bool> _shownChanges;

	bool _shown = false;
	bool _expanded = false;
	bool _forceExpanded = false;
	bool _hovered = false;
	bool _playing = false;
	Media::Player::TrackState _trackState;

	Part _hoveredPart = Part::None;
	Part _pressedPart = Part::None;
	bool _down = false;
	bool _mouseGrabbed = false;
	QPoint _downPoint;
	QPoint _downGlobalPoint;
	QPoint _lastMousePos;
	QPoint _dragStartPosition;
	bool _drag = false;
	bool _keepExpandedAfterSeek = false;

	Ayu::Ui::Lyrics::Data _lyrics;
	bool _lyricsLoading = false;
	int _lyricsGeneration = 0;
	int _lyricsActive = -1;
	int _lyricsDirection = 1; // +1 forward, -1 after a seek backwards
	float64 _lyricsProgress = -1.;
	int64 _lyricsPositionMs = 0;
	crl::time _lyricsLineChangedAt = 0;
	Ui::Animations::Simple _lyricsFade;
	QPointer<LyricsWindow> _lyricsWindow;
	int64 _positionBaseMs = 0;
	crl::time _positionAt = 0;
	std::shared_ptr<const Ayu::Ui::Analysis::Result> _analysis;
	int _analysisGeneration = 0;
	bool _analysisStarted = false;
	bool _analysisWaiting = false;
	rpl::lifetime _analysisLifetime;

	void requestLyrics();
	void applyLyrics(Ayu::Ui::Lyrics::Data data, int generation);
	void updateLyricsPosition();
	void pushLyricsToWindow();
	bool _lyricsAligned = false;
	void pushPlaybackToWindow();
	void resetAnalysis();
	void maybeStartAnalysis();
	void seekToMs(int64 ms);
	[[nodiscard]] int64 playbackPositionMs() const;
	[[nodiscard]] float64 lyricsPulse(int64 position) const;
	[[nodiscard]] bool lyricsPanelVisible(float64 expand) const;
	[[nodiscard]] bool hasLyrics() const;
	void openLyricsWindow();
	void ensureLyricsWindow();
	void alignLyrics();
	[[nodiscard]] QString floatingLineText() const;

	bool _seeking = false;
	bool _volumeDragging = false;
	float64 _seekProgress = 0.;
	float64 _volumeDragProgress = 0.;

	base::unique_qptr<Ui::PopupMenu> _menu;

	crl::time _marqueeStart = 0;
	crl::time _lockedHintAt = 0;

};

} // namespace AyuUi
