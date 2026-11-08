// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#pragma once

#include "ayu/ui/utils/lyrics_engine.h"
#include "ayu/ui/utils/lyrics_translate.h"
#include "ayu/ui/utils/track_analysis.h"
#include "ayu/ui/utils/weather_engine.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

#include <QtCore/QHash>

#include <array>
#include <deque>
#include <unordered_map>

namespace AyuUi {

class LyricsWindow final : public Ui::RpWidget {
public:
	explicit LyricsWindow(not_null<QWidget*> parent);

	void setTrack(const QString &artist, const QString &title);
	void setCover(QPixmap art, QPixmap blurred, QColor accent);
	void setLyrics(const Ayu::Ui::Lyrics::Data &data, bool loading);
	void setAnalysis(std::shared_ptr<const Ayu::Ui::Analysis::Result> analysis);
	void setPlayback(
		std::int64_t positionMs,
		std::int64_t durationMs,
		bool playing,
		double speed);
	void setSeekCallback(Fn<void(std::int64_t)> callback);
	void setSaveCallbacks(Fn<int()> state, Fn<void()> toggle);

	void toggle(bool shown);
	[[nodiscard]] bool shown() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	enum class Control {
		None,
		Close,
		Previous,
		PlayPause,
		Next,
		Repeat,
		Order,
		Save,
		Seek,
		VolumeIcon,
		Volume,
		Line,
	};

	enum class ToastKind {
		Health,
		Music,
		Calm,
		Saved,
		Removed,
		Pills,
		Message,
		Weather,
		Clock,
		Focus,
		Photo,
		Recorder,
		Search,
		Map,
		Phone,
		Reminder,
	};

	enum class PillMode {
		Track,
		Timer,
		Alert,
		Call,
	};

	enum class Theme {
		Default,
		Health,
		Love,
		Night,
		Rain,
	};

	enum class Widget {
		None,
		Pulse,
		Lines,
		Pills,
		Messages,
		Weather,
		Sleep,
		Radar,
		Clock,
		Friends,
		Battery,
		Paleness,
		Emblem,
		FaceId,
		Recorder,
		Ring,
		Spectrum,
		Rhythm,
		Rings,
		ScreenTime,
		Target,
		Block,
		Counter,
		Route,
		Infinity,
		StoppedClock,
		Alarm,
		Reminders,
		Geo,
		Compass,
		Search,
		MissedList,
		Phone,
		Steps,
		Dots,
		Stopwatch,
	};

	struct LineLayout {
		double top = 0.;
		double height = 0.;
		std::vector<Ayu::Ui::Lyrics::Word> words;
		std::vector<QRectF> boxes;
		std::vector<std::pair<int, int>> stickers;
		std::vector<int> fx;
		std::vector<int> numbers;
		std::vector<int> icons;
		QString translationHtml;
		double translationTop = 0.;
		double translationTextTop = 0.;
		QPixmap translation;
		QPixmap sharp;
		QPixmap blurred;
	};

	struct Geometry {
		double unit = 1.;
		QRectF close;
		QRectF pill;
		QRectF more;
		QRectF nowCard;
		QRectF playerCard;
		QRectF leftCard;
		QRectF rightCard;
		QRectF wideCard;
		QRectF lyrics;
		QRectF seek;
		QRectF previous;
		QRectF playPause;
		QRectF next;
		QRectF repeat;
		QRectF order;
		QRectF save;
		QRectF volumeIcon;
		QRectF volume;
	};

	struct Toast {
		ToastKind kind = ToastKind::Music;
		QString label;
		QString title;
		QString text;
		crl::time shownAt = 0;
	};

	struct Particle {
		double x = 0.;
		double y = 0.;
		double radius = 0.;
		double speed = 0.;
		double phase = 0.;
		double alpha = 0.;
	};

	[[nodiscard]] std::int64_t position() const;
	[[nodiscard]] int activeIndex(std::int64_t position) const;
	[[nodiscard]] QFont lyricFont() const;
	[[nodiscard]] double anchorY() const;
	[[nodiscard]] int lineAt(QPoint point) const;
	[[nodiscard]] Control controlAt(QPoint point) const;
	[[nodiscard]] QColor wordColor(const Ayu::Ui::Lyrics::Word &word, double lit) const;
	[[nodiscard]] float pulse(std::int64_t position) const;
	[[nodiscard]] bool inChorus(std::int64_t position) const;
	[[nodiscard]] bool alarm() const;
	[[nodiscard]] QFont makeFont(double size, int weight = 400) const;
	[[nodiscard]] std::int64_t nextChorusStart(std::int64_t position) const;
	[[nodiscard]] QString chorusLine(std::int64_t start) const;
	void detectTheme();
	void paintStatusBar(QPainter &p);
	void paintGlyph(QPainter &p, Control control, const QRectF &rect, QColor color) const;

	void computeGeometry();
	void relayout();
	void ensureCache(int index);
	void tick(crl::time now);
	void tickHeart(double dt, std::int64_t position);
	void tickParticles(double dt, std::int64_t position);
	void showToast(ToastKind kind, QString label, QString title, QString text);
	void activate(Control control, QPoint point);
	void updateDrag(QPoint point);

	void paintContent(QPainter &p);
	void paintBackground(QPainter &p, std::int64_t position);
	void paintParticles(QPainter &p, bool front);
	void paintGlass(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintPill(QPainter &p, std::int64_t position);
	void paintWeatherChip(QPainter &p);
	void paintNowCard(QPainter &p, std::int64_t position);
	void buildCardArt();
	void paintCardContent(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintPlayerCard(QPainter &p, std::int64_t position);
	void paintWidget(QPainter &p, Widget kind, const QRectF &rect, std::int64_t position);
	void paintPulseWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintLinesWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintPillsWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintMessagesWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintWeatherWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintSleepWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintWidgetHeader(
		QPainter &p,
		const QRectF &rect,
		const QString &glyph,
		QColor glyphColor,
		const QString &title,
		const QString &corner) const;
	void paintGeneratedArt(QPainter &p, const QRectF &rect) const;
	void paintArt(QPainter &p, const QRectF &rect, double zoom, QPointF drift) const;
	void paintLyrics(QPainter &p, std::int64_t position);
	void paintToast(QPainter &p);
	void paintToastContent(QPainter &p, const QRectF &rect);
	[[nodiscard]] QPixmap snapshotWidget(Widget kind, const QRectF &rect, std::int64_t position);
	void paintActiveLine(
		QPainter &p,
		int index,
		QPointF origin,
		double emphasis,
		std::int64_t position);
	void paintBleedingWord(
		QPainter &p,
		const QString &text,
		QPointF baseline,
		double width,
		QColor color,
		double strength,
		double age,
		int seed) const;
	void paintIconButton(
		QPainter &p,
		const QRectF &rect,
		const style::icon &icon,
		Control control,
		QColor color) const;
	void paintStar(QPainter &p, const QRectF &rect, bool filled) const;
	void paintModeIcon(QPainter &p, Control control, const QRectF &rect, QColor color, bool one) const;
	void paintEqualizer(QPainter &p, const QRectF &rect, std::int64_t position) const;
	void loadCustomCover(const QString &path);
	void requestWeather(bool force);
	void requestTranslation();
	void prepareTransition();
	void prewarm();
	void requestBackdrop(bool sync);
	void scheduleHiddenLayout();
	void setLyricsOffset(std::int64_t offset, bool announce);
	void applyBackdrop(std::uint64_t key, QImage background, QImage frost, QImage glow);
	[[nodiscard]] std::uint64_t backdropKey() const;
	[[nodiscard]] QString translationTarget() const;
	void renderTranslation(QPainter &p, const LineLayout &line, QPointF at, QColor color) const;
	void buildScenes();
	void updateScene(crl::time now, std::int64_t position);
	void paintSceneWidget(QPainter &p, bool left, const QRectF &rect, std::int64_t position);
	void paintSticker(QPainter &p, int sticker, QPointF anchor, double scale) const;
	void paintBloom(QPainter &p, const QString &text, QPointF baseline, QColor color, double strength, int seed) const;
	struct TextSprite {
		QPixmap pixmap;
		QPointF offset;
		int used = 0;
	};
	const TextSprite &textSprite(const QFont &font, QRgb rgb, const QString &text) const;
	void drawText(QPainter &p, QPointF baseline, const QString &text) const;
	void drawText(QPainter &p, const QRectF &rect, int flags, const QString &text) const;
	void paintRadarWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintClockWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintFriendsWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintBatteryWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintPalenessWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintEmblemWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintFaceWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintRecorderWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintRingWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintSpectrumWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	[[nodiscard]] int wordFx(const QString &normalized, int &number) const;
	bool paintFxWord(QPainter &p, int fx, const QString &text, QPointF baseline, const QRectF &box, double emphasis, double progress, double age, int seed) const;
	void paintFxOverlay(QPainter &p, int fx, int number, const QString &text, const QRectF &box, double age, double opacity, int seed) const;
	void paintWidgetFooter(QPainter &p, const QRectF &rect, const QString &title, const QString &subtitle, double size) const;
	void paintToggle(QPainter &p, const QRectF &rect, double on) const;
	void paintAvatar(QPainter &p, QPointF center, double radius, QColor color) const;
	[[nodiscard]] QString sceneTitle(const QString &fallback) const;
	void paintTargetWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintBlockWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintCounterWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintRouteWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintInfinityWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintStoppedClockWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintAlarmWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintRemindersWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintGeoWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintCompassWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintSearchWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintMissedListWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintPhoneWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintStepsWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintDotsWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintStopwatchWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintRhythmWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintRingsWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	void paintScreenTimeWidget(QPainter &p, const QRectF &rect, std::int64_t position);
	[[nodiscard]] double sceneProgress(std::int64_t position, std::int64_t span) const;
	[[nodiscard]] QString sceneWord() const;
	[[nodiscard]] int pillsTaken(std::int64_t position) const;

	Ayu::Ui::Lyrics::Data _data;
	std::shared_ptr<const Ayu::Ui::Analysis::Result> _analysis;
	QString _artist;
	QString _title;
	QPixmap _art;
	QPixmap _blurred;
	QColor _accent;
	bool _loading = false;

	Geometry _geometry;
	std::vector<LineLayout> _lines;
	std::vector<double> _emphasis;
	QFont _font;
	double _rowHeight = 0.;
	double _ascent = 0.;
	QSize _layoutSize;
	double _contentHeight = 0.;
	QPixmap _frost;
	QPixmap _customCover;
	std::vector<float> _eqLevels;
	QPixmap _backgroundCache;
	QPixmap _glowCache;
	std::vector<std::pair<QRectF, QPixmap>> _glassCache;

	std::int64_t _basePosition = 0;
	crl::time _baseAt = 0;
	std::int64_t _duration = 0;
	bool _playing = false;
	double _speed = 1.;

	double _scroll = 0.;
	double _velocity = 0.;
	double _manualOffset = 0.;
	crl::time _manualUntil = 0;
	crl::time _lastTick = 0;
	crl::time _statsAt = 0;
	crl::time _paintTime = 0;
	int _paintFrames = 0;
	double _avgPaint = 0.;
	bool _lowQuality = false;
	crl::time _nextFrameAt = 0;
	std::array<double, 5> _sectionTime = {};
	QPixmap _cardArt;
	QImage _cardFrame;
	QPixmap _cardMask;
	int _active = -1;
	int _hoveredLine = -1;
	Control _hovered = Control::None;
	Control _pressed = Control::None;
	double _dragValue = -1.;

	double _heart = 74.;
	int _heartMin = 0;
	int _heartMax = 0;
	std::vector<float> _ecg;
	int _ecgHead = 0;
	double _ecgClock = 0.;
	std::int64_t _lastBeatMs = -1;
	std::deque<std::pair<crl::time, int>> _heartHistory;
	std::deque<crl::time> _kicks;
	double _pulseAverage = 0.;
	float _lastPulse = 0.f;
	crl::time _lastKickAt = 0;
	double _baselinePhase = 0.;
	crl::time _syntheticBeatAt = 0;
	std::vector<float> _ecgPattern;
	int _ecgPatternAt = -1;
	double _redBoost = 0.;
	double _pillWidth = 0.;
	PillMode _pillMode = PillMode::Track;

	std::vector<Particle> _particles;
	bool _wasChorus = false;
	bool _wasAlarm = false;
	Toast _toast;
	QPixmap _toastCache;
	QPixmap _transitionSharp;
	QPixmap _transitionBlur;
	crl::time _toastCacheAt = 0;
	std::array<QPixmap, 4> _snapshots;
	crl::time _snapshotsAt = -1;
	std::array<QImage, 2> _widgetFrames;
	mutable std::unordered_map<int, QFont> _fontCache;
	mutable std::unordered_map<std::uint64_t, QPixmap> _blooms;
	mutable QHash<QString, TextSprite> _textSprites;
	mutable int _textFrame = 0;
	std::uint64_t _backdropKey = 0;
	std::uint64_t _backdropPending = 0;
	double _paintCpu = 0.;
	bool _everShown = false;
	std::int64_t _lyricsOffset = 0;
	bool _hiddenLayoutScheduled = false;
	std::array<crl::time, 2> _widgetFrameAt = {};
	std::array<Widget, 2> _widgetFrameKind = { Widget::None, Widget::None };
	crl::time _widgetRefreshFrame = 0;
	crl::time _themeToastAt = 0;
	int _lastPillsTaken = -1;
	std::vector<int> _lineCategory;
	std::vector<int> _categoryCounts;
	int _sceneCategory = 0;
	int _sceneLine = -1;
	std::int64_t _sceneStartMs = 0;
	crl::time _sceneSwitchAt = 0;
	Widget _prevLeftWidget = Widget::None;
	Widget _prevRightWidget = Widget::None;
	std::uint32_t _sceneToasts = 0;
	bool _bluePalette = false;
	bool _purplePalette = false;
	QString _sceneStatus;
	double _listenedSeconds = 0.;
	Ayu::Ui::Weather::Data _weather;
	crl::time _weatherAt = 0;
	bool _weatherLoading = false;
	crl::time _weatherRequestedAt = 0;
	int _rotationStep = 0;
	int _rotationLine = -2;
	crl::time _rotationAt = 0;
	bool _hasPills = false;
	Ayu::Ui::Lyrics::Translation _translation;
	int _translationGeneration = 0;
	QFont _translationFont;
	double _translationChip = 0.;
	int _weatherGeneration = 0;
	double _energySmooth = 0.;
	Theme _theme = Theme::Default;
	Widget _leftWidget = Widget::Pulse;
	Widget _rightWidget = Widget::Sleep;
	Widget _wideWidget = Widget::Lines;

	Fn<void(std::int64_t)> _seek;
	Fn<int()> _profileState;
	Fn<void()> _toggleProfile;
	bool _shown = false;
	Ui::Animations::Simple _showAnimation;
	Ui::Animations::Basic _animation;
};

} // namespace AyuUi
