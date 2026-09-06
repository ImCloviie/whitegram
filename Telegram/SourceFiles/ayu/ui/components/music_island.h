// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "data/data_audio_msg_id.h"
#include "media/audio/media_audio.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Media::View {
class PlaybackProgress;
} // namespace Media::View

namespace Window {
class SessionController;
} // namespace Window

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
		QRectF close;
		QRectF seek;
		QRectF volumeToggle;
		QRectF volume;
		QRectF saveProfile;
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
	void updateExpanded();
	void updatePosition();
	void updateCursor();
	void updateHovered(QPoint point);
	void checkLiveAnimation();
	void snapToCorner();

	[[nodiscard]] float64 expandProgress() const;
	[[nodiscard]] float64 shownProgress() const;
	[[nodiscard]] QSize islandSize(float64 expand) const;
	[[nodiscard]] Layout computeLayout(float64 expand) const;
	[[nodiscard]] Part partAt(QPoint point) const;
	[[nodiscard]] QPoint cornerPosition(QSize outer) const;
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

	void activate(Part part);
	void showTrackMessage();
	void saveToProfile();
	void applySongVolume(float64 volume);

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
	void paintTimes(QPainter &p, const Layout &layout, float64 opacity) const;
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
	int _coverGeneration = 0;
	rpl::lifetime _coverLifetime;

	Ui::Animations::Simple _shownAnimation;
	Ui::Animations::Simple _expandAnimation;
	Ui::Animations::Simple _snapAnimation;
	Ui::Animations::Basic _liveAnimation;

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
	QPoint _downPoint;
	bool _drag = false;
	QPoint _dragLocalPoint;
	QPoint _dragFrom;

	bool _seeking = false;
	bool _volumeDragging = false;
	float64 _seekProgress = 0.;
	float64 _volumeDragProgress = 0.;

	crl::time _marqueeStart = 0;

};

} // namespace AyuUi
