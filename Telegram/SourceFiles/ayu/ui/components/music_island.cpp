// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/music_island.h"

#include "ayu/ui/components/lyrics_window.h"
#include "ayu/ui/utils/lyrics_engine.h"

#include "ayu/ayu_settings.h"
#include "logs.h"
#include "ayu/ui/utils/lyrics_align.h"
#include "ayu/ui/utils/color_utils.h"
#include "ayu/ui/utils/itunes_search.h"
#include "ayu/ui/utils/palette.h"
#include "base/algorithm.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_saved_music.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/media_common.h"
#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_playback_progress.h"
#include "styles/palette.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "styles/style_menu_icons.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_music_island.h"
#include "styles/style_basic.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"

#include <QPainterPath>
#include <QSvgRenderer>
#include <QtWidgets/QApplication>

namespace AyuUi {
namespace {

constexpr auto kShowDuration = crl::time(320);
constexpr auto kExpandDuration = crl::time(280);
constexpr auto kShadowLayers = 7;
constexpr auto kBlurredCoverSize = 64;
constexpr auto kBlurRadius = 14;
constexpr auto kMarqueeHold = crl::time(1400);
constexpr auto kMarqueeSpeed = 40; // pixels per second

[[nodiscard]] const style::TextStyle &TitleStyle() {
	return st::musicIslandTitleStyle;
}

[[nodiscard]] const style::TextStyle &PerformerStyle() {
	return st::musicIslandPerformerStyle;
}

[[nodiscard]] const style::TextStyle &TimeStyle() {
	return st::musicIslandTimeStyle;
}

[[nodiscard]] QSize CollapsedSize() {
	const auto content = std::max(
		st::musicIslandCollapsedCover,
		TitleStyle().font->height
			+ st::musicIslandPerformerSkip
			+ PerformerStyle().font->height);
	return QSize(
		st::musicIslandCollapsedWidth,
		2 * st::musicIslandCollapsedPadding + content);
}

[[nodiscard]] QSize ExpandedSize() {
	const auto row = std::max(
		st::musicIslandExpandedCover,
		st::musicIslandTitleTop
			+ TitleStyle().font->height
			+ st::musicIslandPerformerSkip
			+ PerformerStyle().font->height
			+ st::musicIslandTextControlsSkip
			+ st::musicIslandPlaySize);
	return QSize(
		st::musicIslandExpandedWidth,
		st::musicIslandExpandedPadding
			+ row
			+ st::musicIslandVolumeTop
			+ st::musicIslandVolumeIconSize
			+ st::musicIslandSeekTop
			+ st::musicIslandSeekHeightOver
			+ st::musicIslandTimeTop
			+ TimeStyle().font->height
			+ st::musicIslandExpandedPaddingBottom);
}

[[nodiscard]] QSize CollapsedOuterSize() {
	const auto shadow = int(st::musicIslandShadow);
	return CollapsedSize() + QSize(2 * shadow, 2 * shadow);
}

[[nodiscard]] QColor AdjustLightness(QColor color, float64 factor) {
	auto hsl = Ayu::Ui::ColorUtils::colorToHSL(color.rgb());
	hsl[2] = std::clamp(hsl[2] * float(factor), 0.f, 1.f);
	return QColor::fromRgb(Ayu::Ui::ColorUtils::HSLToRGB(hsl));
}

[[nodiscard]] std::optional<QColor> ExtractAccent(const QImage &image) {
	if (image.isNull()) {
		return std::nullopt;
	}
	const auto palette = Ayu::Ui::Palette::from(image).generate();
	const auto *swatch = palette.darkVibrantSwatch();
	if (!swatch) {
		swatch = palette.mutedSwatch();
	}
	if (!swatch) {
		swatch = palette.darkMutedSwatch();
	}
	if (!swatch) {
		swatch = palette.dominantSwatch();
	}
	if (!swatch) {
		return std::nullopt;
	}

	constexpr auto kWhite = qRgb(255, 255, 255);
	const auto extracted = swatch->rgb();
	const auto contrast = Ayu::Ui::ColorUtils::calculateContrast(
		kWhite,
		extracted);

	auto adjusted = QColor::fromRgb(extracted);
	if (contrast > 15.) {
		adjusted = AdjustLightness(adjusted, 1.8);
	} else if (contrast < 8.) {
		adjusted = AdjustLightness(adjusted, 0.55);
	}
	if (Ayu::Ui::ColorUtils::calculateContrast(kWhite, adjusted.rgb()) < 3.) {
		adjusted = AdjustLightness(adjusted, 0.5);
	}
	return adjusted;
}

void DrawToFill(QPainter &p, const QRectF &target, const QPixmap &pixmap) {
	if (pixmap.isNull() || target.isEmpty()) {
		return;
	}
	const auto source = QSizeF(pixmap.size());
	const auto scaled = source.scaled(
		target.size(),
		Qt::KeepAspectRatioByExpanding);
	const auto destination = QRectF(
		target.center() - QPointF(scaled.width() / 2., scaled.height() / 2.),
		scaled);
	p.drawPixmap(destination, pixmap, QRectF(QPointF(), source));
}

void FillRounded(
		QPainter &p,
		const QRectF &rect,
		float64 radius,
		const QBrush &brush) {
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawRoundedRect(rect, radius, radius);
}

void FillProgress(
		QPainter &p,
		const QRectF &rect,
		float64 progress,
		const QColor &inactive,
		const QColor &active) {
	const auto radius = rect.height() / 2.;
	FillRounded(p, rect, radius, inactive);
	if (progress <= 0.) {
		return;
	}
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.save();
	p.setClipPath(path);
	p.fillRect(
		QRectF(rect.left(), rect.top(), rect.width() * progress, rect.height()),
		active);
	p.restore();
}

[[nodiscard]] float64 MarqueeOffset(int distance, crl::time elapsed) {
	if (distance <= 0 || elapsed < 0) {
		return 0.;
	}
	const auto scroll = std::max(
		crl::time(distance * 1000 / kMarqueeSpeed),
		crl::time(600));
	const auto period = 2 * (kMarqueeHold + scroll);
	const auto passed = elapsed % period;
	const auto smooth = [](float64 value) {
		return value * value * (3. - 2. * value);
	};
	if (passed < kMarqueeHold) {
		return 0.;
	} else if (passed < kMarqueeHold + scroll) {
		return distance * smooth(float64(passed - kMarqueeHold) / scroll);
	} else if (passed < 2 * kMarqueeHold + scroll) {
		return float64(distance);
	}
	const auto back = float64(passed - 2 * kMarqueeHold - scroll) / scroll;
	return distance * (1. - smooth(back));
}

void PaintNoCover(QPainter &p, const QRectF &rect, const QColor &color) {
	const auto side = int(base::SafeRound(rect.width() / 2.));
	if (side <= 0) {
		return;
	}
	struct Cache {
		QPixmap pixmap;
		int side = 0;
		QColor color;
	};
	static auto cache = Cache();
	if (cache.pixmap.isNull() || cache.side != side || cache.color != color) {
		const auto ratio = style::DevicePixelRatio();
		auto image = QImage(
			QSize(side, side) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			auto renderer = QSvgRenderer(u":/gui/icons/ayu/nocover.svg"_q);
			if (!renderer.isValid()) {
				return;
			}
			q.setPen(QPen(color));
			q.setBrush(color);
			renderer.render(&q, QRectF(0, 0, side, side));
		}
		cache.pixmap = Ui::PixmapFromImage(std::move(image));
		cache.side = side;
		cache.color = color;
	}
	p.drawPixmap(
		QRectF(
			rect.center() - QPointF(side / 2., side / 2.),
			QSizeF(side, side)),
		cache.pixmap,
		QRectF(QPointF(), QSizeF(cache.pixmap.size())));
}

[[nodiscard]] int PercentNominal(int pixel, int usable) {
	return (usable <= 0)
		? 500
		: std::clamp(int(base::SafeRound(1000. * pixel / usable)), 0, 1000);
}

} // namespace

MusicIsland::MusicIsland(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _playback(std::make_unique<Media::View::PlaybackProgress>()) {
	setCursor(style::cur_default);
	hide();

	_liveAnimation.init([=] {
		if (_lyricsWindow && _lyricsWindow->shown()) {
			return _shown;
		}
		updateLyricsPosition();
		update();
		return _shown
			&& (_playing || _expanded || _expandAnimation.animating());
	});
	_playback->setValueChangedCallback([=](float64, float64) {
		update();
	});

	if (parent) {
		base::install_event_filter(this, parent, [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::Resize) {
				if (!_drag) {
					updatePosition();
				}
				checkVisibility();
			}
			return base::EventFilterResult::Continue;
		});
	}

	setupPlayer();
	setupSettings();
	loadCustomBackground();

	_hoverPoll.setInterval(45);
	_hoverPoll.setSingleShot(false);
	connect(&_hoverPoll, &QTimer::timeout, this, [=] {
		if (_shown) {
			pollHover();
		}
	});

	_trackState = Media::Player::instance()->getState(AudioMsgId::Type::Song);
	handleTrackChanged();
	checkVisibility();
}

MusicIsland::~MusicIsland() {
	if (_lyricsWindow) {
		delete _lyricsWindow.data();
	}
}

bool MusicIsland::replacesTopBar(AudioMsgId::Type type) const {
	if (type != AudioMsgId::Type::Song) {
		return false;
	}
	const auto &settings = AyuSettings::getInstance();
	return settings.musicIslandEnabled()
		&& settings.musicIslandHideTopBar()
		&& fitsInParent();
}

rpl::producer<bool> MusicIsland::shownChanges() const {
	return _shownChanges.events();
}

bool MusicIsland::fitsInParent() const {
	const auto expanded = ExpandedSize();
	const auto margin = 2 * st::musicIslandScreenMargin;
	const auto parent = parentWidget();
	return parent
		&& (parent->width() >= expanded.width() + margin)
		&& (parent->height() >= expanded.height() + margin);
}

void MusicIsland::refresh() {
	if (!_drag) {
		updatePosition();
	}
	checkVisibility();
}

void MusicIsland::setupPlayer() {
	const auto player = Media::Player::instance();

	player->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &state) {
		handleStateUpdate(state);
	}, lifetime());

	player->trackChanged(
	) | rpl::filter([](AudioMsgId::Type type) {
		return (type == AudioMsgId::Type::Song);
	}) | rpl::on_next([=](AudioMsgId::Type) {
		handleTrackChanged();
	}, lifetime());

	player->startsPlay(
		AudioMsgId::Type::Song
	) | rpl::on_next([=] {
		handleTrackChanged();
		checkVisibility();
	}, lifetime());

	player->stops(
		AudioMsgId::Type::Song
	) | rpl::on_next([=] {
		_trackState = Media::Player::TrackState();
		checkVisibility();
	}, lifetime());
}

void MusicIsland::setupSettings() {
	const auto settings = &AyuSettings::getInstance();

	settings->musicIslandEnabledChanges(
	) | rpl::on_next([=](bool) {
		checkVisibility();
	}, lifetime());

	settings->musicIslandPositionChanges(
	) | rpl::on_next([=](int) {
		updatePosition();
	}, lifetime());

	settings->musicIslandFreeXChanges(
	) | rpl::on_next([=](int) {
		if (!_drag) {
			updatePosition();
		}
	}, lifetime());

	settings->musicIslandFreeYChanges(
	) | rpl::on_next([=](int) {
		if (!_drag) {
			updatePosition();
		}
	}, lifetime());

	settings->musicIslandExpandModeChanges(
	) | rpl::on_next([=](int) {
		_forceExpanded = false;
		_hovered = false;
		updateExpanded();
	}, lifetime());

	Core::App().settings().songVolumeChanges(
	) | rpl::on_next([=](float64) {
		update();
	}, lifetime());

	rpl::merge(
		Core::App().settings().playerRepeatModeChanges() | rpl::to_empty,
		Core::App().settings().playerOrderModeChanges() | rpl::to_empty,
		Core::App().settings().audioPlaybackSpeedChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	rpl::merge(
		settings->adaptiveCoverColorChanges(),
		settings->musicIslandCoverColorsChanges(),
		settings->musicIslandBlurCoverChanges(),
		settings->musicIslandEqualizerChanges(),
		settings->musicIslandCustomBackgroundEnabledChanges(),
		settings->musicIslandCustomBackgroundBlurChanges()
	) | rpl::on_next([=](bool) {
		loadCustomBackground();
		update();
	}, lifetime());

	rpl::merge(
		settings->musicLyricsEnabledChanges() | rpl::to_empty,
		settings->musicLyricsShowInIslandChanges() | rpl::to_empty,
		settings->musicLyricsFloatingLineChanges() | rpl::to_empty,
		settings->musicLyricsKeywordChanges() | rpl::to_empty,
		settings->musicLyricsFontScaleChanges() | rpl::to_empty,
		settings->musicLyricsKaraokeButtonChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		if (AyuSettings::getInstance().musicLyricsEnabled()
			&& !hasLyrics()
			&& !_lyricsLoading
			&& !_titleRaw.isEmpty()) {
			requestLyrics();
		}
		updatePosition();
		update();
		maybeStartAnalysis();
	}, lifetime());

	rpl::merge(
		settings->musicLyricsChorusChanges(),
		settings->musicLyricsBassPulseChanges()
	) | rpl::on_next([=](bool) {
		resetAnalysis();
		maybeStartAnalysis();
		update();
	}, lifetime());

	rpl::merge(
		settings->musicIslandCustomBackgroundPathChanges() | rpl::to_empty,
		settings->musicIslandCustomBackgroundOpacityChanges() | rpl::to_empty,
		settings->musicIslandCustomBackgroundDimChanges() | rpl::to_empty,
		settings->musicIslandCustomBackgroundModeChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		loadCustomBackground();
		update();
	}, lifetime());
}

void MusicIsland::handleTrackChanged() {
	const auto player = Media::Player::instance();
	const auto id = player->current(AudioMsgId::Type::Song);
	const auto document = id.audio();
	if (!document) {
		checkVisibility();
		return;
	} else if (_songId.audio() == document && _songId.contextId() == id.contextId()) {
		_songId = id;
		checkVisibility();
		return;
	}
	_songId = id;
	_contextId = id.contextId();
	_controller->session().data().savedMusic().loadIds();

	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	_titleRaw = name.title;
	_performerRaw = name.performer;
	_title.setText(TitleStyle(), _titleRaw, Ui::NameTextOptions());
	_performer.setText(PerformerStyle(), _performerRaw, Ui::NameTextOptions());
	_marqueeStart = crl::now();

	loadCover();
	resetAnalysis();
	requestLyrics();
	checkVisibility();
	update();
}

void MusicIsland::handleStateUpdate(const Media::Player::TrackState &state) {
	if (state.id.type() != AudioMsgId::Type::Song) {
		return;
	}
	_trackState = state;
	_playback->updateState(state);

	const auto playing = !Media::Player::IsStoppedOrStopping(state.state)
		&& !Media::Player::IsPausedOrPausing(state.state);
	if (_playing != playing) {
		_playing = playing;
		checkLiveAnimation();
	}

	const auto frequency = state.frequency
		? state.frequency
		: Media::Player::kDefaultFrequency;
	_durationSeconds = state.length / frequency;
	if (!_seeking) {
		_playedText = Ui::FormatDurationText(state.position / frequency);
	}
	_durationText = Ui::FormatDurationText(_durationSeconds);

	const auto reported = int64(state.position * 1000 / frequency);
	const auto predicted = playbackPositionMs();
	_positionBaseMs = (_playing
		&& _positionAt
		&& std::abs(reported - predicted) < 250)
		? (predicted + (reported - predicted) / 4)
		: reported;
	_positionAt = crl::now();
	updateLyricsPosition();
	pushPlaybackToWindow();
	checkVisibility();
	update();
}

void MusicIsland::checkVisibility() {
	const auto &settings = AyuSettings::getInstance();
	const auto type = AudioMsgId::Type::Song;
	const auto player = Media::Player::instance();
	const auto current = player->current(type);
	const auto &state = _trackState.id
		? _trackState
		: player->getState(type);
	const auto active = current
		&& current.audio()
		&& state.id
		&& (current == state.id)
		&& !Media::Player::IsStoppedOrStopping(state.state);

	toggleShown(settings.musicIslandEnabled() && active && fitsInParent());
}

void MusicIsland::toggleShown(bool shown) {
	if (_shown == shown) {
		return;
	}
	const auto from = shownProgress();
	_shown = shown;
	if (shown) {
		_forceExpanded = false;
		_hovered = false;
		_expanded = false;
		_expandAnimation.stop();
		_marqueeStart = crl::now();
		updatePosition();
		show();
		raise();
		_hoverPoll.start();
	} else {
		_down = false;
		_pressedPart = Part::None;
		_hoveredPart = Part::None;
		_drag = false;
		_seeking = false;
		_volumeDragging = false;
		// The track can end (or be closed) in the middle of a drag or a
		// seek: the pointer grab would then outlive the island and swallow
		// all mouse input in the whole window.
		if (_mouseGrabbed) {
			_mouseGrabbed = false;
			releaseMouse();
		}
		_hoverPoll.stop();
		_menu = nullptr;
	}
	_shownAnimation.start([=] {
		update();
		if (!_shown && shownProgress() < 0.01) {
			hide();
		}
	}, from, shown ? 1. : 0., kShowDuration, anim::easeOutCubic);
	checkLiveAnimation();
	_shownChanges.fire_copy(shown);
}

bool MusicIsland::desiredExpanded() const {
	if (_seeking || _volumeDragging || _keepExpandedAfterSeek || _menu) {
		return true;
	}
	const auto mode = AyuSettings::getInstance().musicIslandExpandMode();
	switch (mode) {
	case 0: // On hover only.
		return _hovered;
	case 1: // On click only (a click pins/unpins the island).
		return _forceExpanded;
	default: // Hover and click.
		return _hovered || _forceExpanded;
	}
}

void MusicIsland::updateExpanded() {
	const auto expand = desiredExpanded();
	if (_expanded == expand) {
		return;
	}
	const auto from = expandProgress();
	_expanded = expand;
	_marqueeStart = crl::now();
	_expandAnimation.start([=] {
		updatePosition();
	}, from, expand ? 1. : 0., kExpandDuration, anim::easeOutCubic);
	updatePosition();
	checkLiveAnimation();
}

void MusicIsland::updatePosition() {
	if (_drag) {
		update();
		return;
	}
	const auto inner = islandSize(expandProgress());
	const auto shadow = st::musicIslandShadow;
	const auto outer = QSize(
		inner.width() + 2 * shadow,
		inner.height() + 2 * shadow);
	auto position = freeTopLeft(outer);
	if (const auto parent = parentWidget()) {
		// Keep the whole island (including its shadow padding) inside the
		// window when it grows from a free position near an edge.
		const auto clampMargin = std::max(0, int(st::musicIslandShadow / 2));
		position.setX(std::clamp(
			position.x(),
			clampMargin,
			std::max(clampMargin, parent->width() - outer.width() - clampMargin)));
		position.setY(std::clamp(
			position.y(),
			clampMargin,
			std::max(clampMargin, parent->height() - outer.height() - clampMargin)));
	}
	setGeometry(QRect(position, outer));

	// Hover is updated from the OS pointer via pollHover() and the native
	// enter/move/leave handlers, never by re-reading QCursor during this
	// geometry change. This keeps expansion smooth near every window edge
	// without flapping.

	update();
}

void MusicIsland::updateCursor() {
	const auto pointer = (_hoveredPart != Part::None)
		&& (_hoveredPart != Part::Body);
	setCursor(pointer ? style::cur_pointer : style::cur_default);
}

void MusicIsland::checkLiveAnimation() {
	const auto needed = _shown
		&& (_playing || _expanded || _expandAnimation.animating());
	if (needed && !_liveAnimation.animating()) {
		_liveAnimation.start();
	} else if (!needed && _liveAnimation.animating()) {
		_liveAnimation.stop();
	}
}

float64 MusicIsland::expandProgress() const {
	return _expandAnimation.value(_expanded ? 1. : 0.);
}

float64 MusicIsland::shownProgress() const {
	return _shownAnimation.value(_shown ? 1. : 0.);
}

bool MusicIsland::lyricsPanelVisible(float64 expand) const {
	if (_lyrics.lines.empty() || !_lyrics.hasData) {
		return false;
	}
	const auto &s = AyuSettings::getInstance();
	if (!s.musicLyricsEnabled() || !s.musicLyricsShowInIsland()) {
		return false;
	}
	return expand > 0.5;
}

bool MusicIsland::hasLyrics() const {
	return _lyrics.hasData && !_lyrics.lines.empty();
}

QSize MusicIsland::islandSize(float64 expand) const {
	const auto collapsed = CollapsedSize();
	const auto expanded = ExpandedSize();
	auto result = QSize(
		anim::interpolate(collapsed.width(), expanded.width(), expand),
		anim::interpolate(collapsed.height(), expanded.height(), expand));
	const auto &s = AyuSettings::getInstance();
	if (s.musicLyricsEnabled()
		&& s.musicLyricsShowInIsland()
		&& hasLyrics()) {
		const auto extra = float64(
			st::musicIslandLyricsTop + st::musicIslandLyricsHeight);
		result.setHeight(result.height() + int(base::SafeRound(extra * expand)));
	}
	return result;
}

QPoint MusicIsland::freeTopLeft(QSize outer) const {
	const auto parent = parentWidget();
	if (!parent) {
		return QPoint();
	}
	const auto margin = std::max(0, int(st::musicIslandShadow / 2));
	const auto collapsed = CollapsedOuterSize();
	const auto &settings = AyuSettings::getInstance();
	const auto place = [&](int stored, int full, int pill, int size) {
		const auto span = std::max(1, full - 2 * margin);
		const auto norm = std::clamp(stored, 0, 1000) / 1000.;
		const auto center = std::clamp(
			margin + int(base::SafeRound(norm * span)),
			margin + pill / 2,
			std::max(margin + pill / 2, full - margin - pill + pill / 2));
		const auto start = center - pill / 2;
		return (center < full / 2) ? start : (start + pill - size);
	};
	return QPoint(
		place(
			settings.musicIslandFreeX(),
			parent->width(),
			collapsed.width(),
			outer.width()),
		place(
			settings.musicIslandFreeY(),
			parent->height(),
			collapsed.height(),
			outer.height()));
}

void MusicIsland::saveFreePosition() {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	// WHY: the stored point is the collapsed pill's center; the card grows
	// from the nearest window edge, so drop spot == collapsed spot.
	const auto margin = std::max(0, int(st::musicIslandShadow / 2));
	const auto collapsed = CollapsedOuterSize();
	const auto stored = [&](int start, int size, int full, int pill) {
		const auto fromStart = start + pill / 2;
		const auto fromEnd = start + size - pill + pill / 2;
		const auto startValid = (fromStart < full / 2);
		const auto endValid = (fromEnd >= full / 2);
		const auto center = (startValid && endValid)
			? ((start + size / 2 < full / 2) ? fromStart : fromEnd)
			: startValid
			? fromStart
			: fromEnd;
		return PercentNominal(
			center - margin,
			std::max(1, full - 2 * margin));
	};
	const auto x = stored(pos().x(), width(), parent->width(), collapsed.width());
	const auto y = stored(pos().y(), height(), parent->height(), collapsed.height());
	auto &islandSettings = AyuSettings::getInstance();
	islandSettings.setMusicIslandPosition(4);
	islandSettings.setMusicIslandFreeX(x);
	islandSettings.setMusicIslandFreeY(y);
}

MusicIsland::Layout MusicIsland::computeLayout(float64 expand) const {
	auto result = Layout();
	const auto shadow = float64(st::musicIslandShadow);
	const auto size = islandSize(expand);
	result.island = QRectF(shadow, shadow, size.width(), size.height());
	result.radius = st::musicIslandCollapsedRadius
		+ (st::musicIslandExpandedRadius - st::musicIslandCollapsedRadius)
			* expand;

	const auto origin = result.island.topLeft();
	const auto islandWidth = result.island.width();
	const auto islandHeight = result.island.height();
	const auto collapsed = CollapsedSize();
	const auto lerp = [&](float64 from, float64 to) {
		return from + (to - from) * expand;
	};
	const auto lerpRect = [&](const QRectF &from, const QRectF &to) {
		return QRectF(
			lerp(from.x(), to.x()),
			lerp(from.y(), to.y()),
			lerp(from.width(), to.width()),
			lerp(from.height(), to.height()));
	};

	const auto titleHeight = float64(TitleStyle().font->height);
	const auto performerHeight = float64(PerformerStyle().font->height);
	const auto performerSkip = float64(st::musicIslandPerformerSkip);

	const auto collapsedPadding = float64(st::musicIslandCollapsedPadding);
	const auto collapsedContent = collapsed.height() - 2 * collapsedPadding;
	const auto collapsedCoverSize = float64(st::musicIslandCollapsedCover);
	const auto collapsedCover = QRectF(
		collapsedPadding,
		collapsedPadding + (collapsedContent - collapsedCoverSize) / 2.,
		collapsedCoverSize,
		collapsedCoverSize);

	const auto bars = st::musicIslandEqualizerCount;
	const auto equalizerWidth = bars * st::musicIslandEqualizerWidth
		+ (bars - 1) * st::musicIslandEqualizerSkip;
	const auto equalizerRight = collapsed.width()
		- collapsedPadding
		- st::musicIslandEqualizerSkipRight;
	const auto equalizer = QRectF(
		equalizerRight - equalizerWidth,
		collapsedPadding
			+ (collapsedContent - st::musicIslandEqualizerHeight) / 2.,
		equalizerWidth,
		st::musicIslandEqualizerHeight);

	const auto collapsedTextX = collapsedCover.right()
		+ st::musicIslandTextSkip;
	const auto collapsedTextWidth = equalizer.left()
		- st::musicIslandTextSkip
		- collapsedTextX;
	const auto collapsedBlock = titleHeight + performerSkip + performerHeight;
	const auto collapsedTitleY = collapsedPadding
		+ (collapsedContent - collapsedBlock) / 2.;
	const auto collapsedPerformerY = collapsedTitleY
		+ titleHeight
		+ performerSkip;

	const auto padding = float64(st::musicIslandExpandedPadding);
	const auto coverSize = float64(st::musicIslandExpandedCover);
	const auto cover = QRectF(padding, padding, coverSize, coverSize);
	const auto textX = cover.right() + st::musicIslandTextSkip;
	const auto titleY = padding + st::musicIslandTitleTop;
	const auto performerY = titleY + titleHeight + performerSkip;

	const auto coverLocal = lerpRect(collapsedCover, cover);
	const auto textLocalX = lerp(collapsedTextX, textX);

	const auto playSize = float64(st::musicIslandPlaySize);
	const auto controlSize = float64(st::musicIslandControlSize);
	const auto controlSkip = float64(st::musicIslandControlSkip);
	const auto playY = coverLocal.bottom() - playSize;
	const auto sideY = playY + (playSize - controlSize) / 2.;
	result.previous = QRectF(textLocalX, sideY, controlSize, controlSize)
		.translated(origin);
	result.playPause = QRectF(
		textLocalX + controlSize + controlSkip,
		playY,
		playSize,
		playSize).translated(origin);
	result.next = QRectF(
		textLocalX + controlSize + controlSkip + playSize + controlSkip,
		sideY,
		controlSize,
		controlSize).translated(origin);
	{
		// Playback modes, flush right: [order] [repeat] [speed?].
		const auto modeSize = float64(st::musicIslandModeSize);
		const auto modeSkip = float64(st::musicIslandModeSkip);
		const auto modeY = playY + (playSize - modeSize) / 2.;
		auto right = islandWidth - padding;
		if (hasSpeedControl()) {
			const auto speedWidth = float64(st::musicIslandSpeedWidth);
			const auto speedHeight = float64(st::musicIslandSpeedHeight);
			result.speed = QRectF(
				right - speedWidth,
				playY + (playSize - speedHeight) / 2.,
				speedWidth,
				speedHeight).translated(origin);
			right -= speedWidth + modeSkip;
		}
		result.repeat = QRectF(right - modeSize, modeY, modeSize, modeSize)
			.translated(origin);
		right -= modeSize + modeSkip;
		result.order = QRectF(right - modeSize, modeY, modeSize, modeSize)
			.translated(origin);
	}
	const auto headerButtonY = st::musicIslandCloseSkip;
	const auto closeSize = float64(st::musicIslandCloseSize);
	const auto saveSize = float64(st::musicIslandSaveSize);
	const auto controlGap = float64(st::musicIslandControlSkip);
	const auto canSave = (profileMusicState() >= 0);

	// Right-aligned top rail packed flush-right: [lyrics?] [save?] [close].
	const auto lyricsSize = float64(st::musicIslandLyricsButtonSize);
	const auto showLyricsBtn = AyuSettings::getInstance().musicLyricsEnabled()
		&& AyuSettings::getInstance().musicLyricsKaraokeButton();
	auto railRight = islandWidth - float64(st::musicIslandCloseSkip);
	result.close = QRectF(
		railRight - closeSize,
		headerButtonY,
		closeSize,
		closeSize).translated(origin);
	auto railCursor = result.close.left() - origin.x() - controlGap - saveSize;
	if (canSave) {
		result.saveProfile = QRectF(
			railCursor,
			headerButtonY,
			saveSize,
			saveSize).translated(origin);
		railCursor -= controlGap + lyricsSize;
	} else {
		railCursor -= 0.; // keep cursor at save slot start
		railCursor = result.close.left() - origin.x() - controlGap - lyricsSize;
	}
	if (showLyricsBtn) {
		result.lyricsButton = QRectF(
			railCursor,
			headerButtonY,
			lyricsSize,
			lyricsSize).translated(origin);
	}

	// The leftmost occupied x of the entire top rail, in island coords, is the
	// cut-point used to bound how wide the track title may stretch.
	auto railCutX = result.close.left() - origin.x();
	if (canSave) {
		railCutX = std::min(railCutX, result.saveProfile.left() - origin.x());
	}
	if (showLyricsBtn) {
		railCutX = std::min(railCutX, result.lyricsButton.left() - origin.x());
	}

	const auto volumeRowTop = coverLocal.bottom()
		+ st::musicIslandVolumeTop;
	const auto volumeIconSize = float64(st::musicIslandVolumeIconSize);
	result.volumeToggle = QRectF(
		padding,
		volumeRowTop,
		volumeIconSize,
		volumeIconSize).translated(origin);
	result.volume = QRectF(
		padding + volumeIconSize + st::musicIslandVolumeSkip,
		volumeRowTop + (volumeIconSize - st::musicIslandVolumeHeight) / 2.,
		islandWidth
			- 2 * padding
			- volumeIconSize
			- st::musicIslandVolumeSkip,
		st::musicIslandVolumeHeight).translated(origin);

	const auto seekBand = float64(st::musicIslandSeekHeightOver);
	const auto seekHeight = float64(
		(_hoveredPart == Part::Seek || _seeking)
			? st::musicIslandSeekHeightOver
			: st::musicIslandSeekHeight);
	const auto timeHeight = float64(TimeStyle().font->height);
	const auto lyricsExtra = (hasLyrics()
		&& AyuSettings::getInstance().musicLyricsEnabled()
		&& AyuSettings::getInstance().musicLyricsShowInIsland())
		? float64(st::musicIslandLyricsTop + st::musicIslandLyricsHeight) * expand
		: 0.;
	const auto timesTop = islandHeight
		- st::musicIslandExpandedPaddingBottom
		- timeHeight
		- lyricsExtra;
	const auto seekCenter = volumeRowTop
		+ volumeIconSize
		+ st::musicIslandSeekTop
		+ seekBand / 2.;
	result.seek = QRectF(
		padding,
		seekCenter - seekHeight / 2.,
		islandWidth - 2 * padding,
		seekHeight).translated(origin);
	result.timesTop = timesTop + origin.y();
	{
		const auto lyricsTop = timesTop + timeHeight + st::musicIslandLyricsTop;
		result.lyricsBox = QRectF(
			padding,
			lyricsTop,
			islandWidth - 2 * padding,
			st::musicIslandLyricsHeight).translated(origin);
	}

	const auto progressInset = float64(st::musicIslandCollapsedRadius) / 2.;
	result.collapsedProgress = QRectF(
		progressInset,
		collapsed.height() - collapsedPadding + 1,
		collapsed.width() - 2 * progressInset,
		2.).translated(origin);

	result.cover = coverLocal.translated(origin);
	result.equalizer = equalizer.translated(origin);
	result.title = QPointF(
		textLocalX,
		lerp(collapsedTitleY, titleY)) + origin;
	result.performer = QPointF(
		textLocalX,
		lerp(collapsedPerformerY, performerY)) + origin;
	const auto expandedTextWidth = std::max(0, int(base::SafeRound(
		railCutX - textLocalX - 2.)));
	result.textWidth = std::max(0, int(base::SafeRound(
		lerp(collapsedTextWidth, float64(expandedTextWidth)))));

	// A dedicated click region covering the title + performer strip, used to
	// open the source message when the island is expanded ("click the name").
	const auto blockTop = lerp(collapsedTitleY, titleY);
	const auto blockBottom = blockTop + titleHeight + performerSkip + performerHeight;
	result.titleZone = QRectF(
		textLocalX,
		blockTop,
		result.textWidth,
		blockBottom - blockTop).translated(origin);

	return result;
}

float64 MusicIsland::displayedProgress() const {
	return _seeking
		? _seekProgress
		: std::clamp(_playback->value(), 0., 1.);
}

bool MusicIsland::artMode() const {
	const auto &settings = AyuSettings::getInstance();
	return _cover.hasArt
		&& _cover.accent.isValid()
		&& settings.musicIslandCoverColors();
}

QColor MusicIsland::titleFg() const {
	return artMode() ? QColor(255, 255, 255) : st::windowBoldFg->c;
}

QColor MusicIsland::subFg() const {
	return artMode()
		? QColor(255, 255, 255, 168)
		: st::windowSubTextFg->c;
}

QColor MusicIsland::iconFg() const {
	return artMode() ? QColor(255, 255, 255, 236) : st::windowBoldFg->c;
}

void MusicIsland::loadCover() {
	_coverLifetime.destroy();
	_cover = CoverData();

	const auto document = _songId.audio();
	if (!document) {
		_mediaView = nullptr;
		update();
		return;
	}
	_mediaView = document->createMediaView();

	const auto ready = [=] {
		return _mediaView && _mediaView->thumbnail();
	};
	if (document->isSongWithCover() && !ready()) {
		_mediaView->thumbnailWanted(Data::FileOrigin(_contextId));
		document->session().downloaderTaskFinished(
		) | rpl::take_while([=] {
			if (!ready()) {
				return true;
			}
			buildCover(_mediaView->thumbnail()->original());
			return false;
		}) | rpl::start(_coverLifetime);
	}
	buildCover(ready() ? _mediaView->thumbnail()->original() : QImage());
}

void MusicIsland::buildCover(QImage source) {
	const auto generation = ++_coverGeneration;
	const auto weak = base::make_weak(this);
	const auto performer = _performerRaw;
	const auto title = _titleRaw;
	const auto artSize = st::musicIslandExpandedCover
		* style::DevicePixelRatio()
		* 2;
	crl::async([=, source = std::move(source)]() mutable {
		if (source.isNull() && !(performer.isEmpty() && title.isEmpty())) {
			const auto fetched = Ayu::Ui::Itunes::FetchCover(
				performer,
				title,
				600);
			if (!fetched.isNull()) {
				source = fetched.toImage();
			}
		}
		auto art = QImage();
		auto blurred = QImage();
		auto accent = std::optional<QColor>();
		if (!source.isNull()) {
			art = source.scaled(
				artSize,
				artSize,
				Qt::KeepAspectRatioByExpanding,
				Qt::SmoothTransformation);
			blurred = Images::BlurLargeImage(
				source.scaled(
					kBlurredCoverSize,
					kBlurredCoverSize,
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation)
				.convertToFormat(QImage::Format_ARGB32_Premultiplied),
				kBlurRadius);
			accent = ExtractAccent(source);
		}
		crl::on_main([=, art = std::move(art), blurred = std::move(blurred)]()
		mutable {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto cover = CoverData();
			cover.hasArt = !art.isNull();
			if (cover.hasArt) {
				cover.art = Ui::PixmapFromImage(std::move(art));
				cover.blurred = Ui::PixmapFromImage(std::move(blurred));
				if (accent) {
					cover.accent = *accent;
				}
			}
			strong->applyCover(std::move(cover), generation);
		});
	});
}

void MusicIsland::applyCover(CoverData cover, int generation) {
	if (generation != _coverGeneration) {
		return;
	}
	_cover = std::move(cover);
	if (_lyricsWindow) {
		_lyricsWindow->setCover(_cover.art, _cover.blurred, _cover.accent);
	}
	update();
}

void MusicIsland::loadCustomBackground() {
	const auto &settings = AyuSettings::getInstance();
	if (!settings.musicIslandCustomBackgroundEnabled()) {
		_customBackground = QPixmap();
		return;
	}
	const auto path = settings.musicIslandCustomBackgroundPath();
	auto image = QImage(path);
	if (image.isNull()) {
		_customBackground = QPixmap();
		return;
	}
	constexpr auto kMaxSide = 1920;
	if (std::max(image.width(), image.height()) > kMaxSide) {
		image = image.scaled(
			kMaxSide,
			kMaxSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	if (settings.musicIslandCustomBackgroundBlur()) {
		image = Images::BlurLargeImage(std::move(image), kBlurRadius);
	}
	_customBackground = Ui::PixmapFromImage(std::move(image));
}

void MusicIsland::activate(Part part, QPoint onPoint) {
	const auto player = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	switch (part) {
	case Part::PlayPause:
		player->playPause(type);
		break;
	case Part::Previous:
		player->previous(type);
		break;
	case Part::Next:
		player->next(type);
		break;
	case Part::Close:
		player->stopAndClose();
		break;
	case Part::SaveProfile:
		toggleProfileMusic();
		break;
	case Part::LyricsButton:
	case Part::LyricsZone:
		openLyricsWindow();
		break;
	case Part::Repeat:
		toggleRepeat();
		break;
	case Part::Order:
		showOrderMenu();
		break;
	case Part::Speed:
		showSpeedMenu();
		break;
	case Part::VolumeToggle: {
		const auto current = songVolume();
		applySongVolume(current > 0.
			? 0.
			: Core::App().settings().rememberedSongVolume());
	} break;
	case Part::Cover:
	case Part::Body: {
		const auto &settings = AyuSettings::getInstance();
		const auto mode = settings.musicIslandExpandMode();
		const auto expanded = expandProgress() > 0.85;
		const auto inTitleZone = computeLayout(
			expandProgress()).titleZone.contains(QPointF(onPoint));
		const auto opensTrack = settings.musicIslandClickOpensTrack();

		if (mode == 0) {
			// Pure-hover mode: there is no pin concept, a tap on the title
			// strip simply jumps to the message.
			if (expanded && inTitleZone && opensTrack) {
				showTrackMessage();
			}
		} else if (!expanded) {
			// First tap: expand and remember it (keeps the pill open after a
			// quick hover flicker or when the pointer slides a hair away).
			_forceExpanded = true;
			updateExpanded();
		} else if (inTitleZone && opensTrack) {
			// Tapping the actual song/artist text opens the source message.
			showTrackMessage();
		} else {
			// Any other tap on the body when already expanded toggles the
			// "stick open" behaviour.
			_forceExpanded = !_forceExpanded;
			updateExpanded();
		}
	} break;
	default:
		break;
	}
}

void MusicIsland::hintLockedPosition() {
	const auto now = crl::now();
	if (now - _lockedHintAt < crl::time(3000)) {
		return;
	}
	_lockedHintAt = now;
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto ru = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	_controller->uiShow()->showToast(ru
		? QString::fromUtf8("Позиция островка зафиксирована — выключите «Зафиксировать позицию» в настройках Whitegram")
		: QString("The island position is locked — turn off “Lock the position” in Whitegram settings"));
}

void MusicIsland::showTrackMessage() {
	if (!_contextId) {
		return;
	}
	const auto item = _controller->session().data().message(_contextId);
	if (item) {
		_controller->showMessage(item);
	}
}

bool MusicIsland::canSaveToProfile() const {
	const auto document = _songId.audio();
	if (!document || !document->isMusicForProfile()) {
		return false;
	}
	return !_controller->session().data().savedMusic().has(document);
}

int MusicIsland::profileMusicState() const {
	const auto document = _songId.audio();
	if (!document || !document->isMusicForProfile()) {
		return -1;
	}
	return _controller->session().data().savedMusic().has(document) ? 1 : 0;
}

void MusicIsland::toggleProfileMusic() {
	const auto document = _songId.audio();
	if (!document || !document->isMusicForProfile()) {
		return;
	}
	auto &saved = _controller->session().data().savedMusic();
	const auto silent = _lyricsWindow && _lyricsWindow->shown();
	if (!saved.has(document)) {
		if (silent) {
			saved.save(document, _contextId);
			update();
		} else {
			saveToProfile();
		}
		return;
	}
	saved.remove(document);
	if (silent) {
		update();
		return;
	}
	const auto ru = Lang::GetInstance().id().startsWith(u"ru"_q)
		|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
	_controller->uiShow()->showToast(ru
		? QString::fromUtf8("Трек убран из профиля")
		: QString("Removed from your profile music"));
	update();
}

void MusicIsland::saveToProfile() {
	const auto document = _songId.audio();
	if (!document || !canSaveToProfile()) {
		return;
	}
	_controller->session().data().savedMusic().save(document, _contextId);
	_controller->uiShow()->showToast({
		.text = { tr::lng_saved_music_added(tr::now) },
		.iconLottie = u"toast/save_to_music"_q,
		.iconLottieSize = st::toastLottieIconSize,
	});
	update();
}

float64 MusicIsland::songVolume() const {
	return Core::App().settings().songVolume();
}

void MusicIsland::applySongVolume(float64 volume) {
	volume = std::clamp(volume, 0., 1.);
	if (volume > 0.) {
		Core::App().settings().setRememberedSongVolume(volume);
	}
	if (volume != songVolume()) {
		if (const auto audio = Media::Player::mixer()) {
			audio->setSongVolume(volume);
		}
		Core::App().settings().setSongVolume(volume);
	}
	update();
}

const style::icon &MusicIsland::volumeIcon() const {
	const auto volume = _volumeDragging
		? _volumeDragProgress
		: songVolume();
	if (volume <= 0.) {
		return st::musicIslandIconVolume0;
	} else if (volume < 0.66) {
		return st::musicIslandIconVolume1;
	}
	return st::musicIslandIconVolume2;
}

float64 MusicIsland::volumeProgressFor(QPoint point) const {
	const auto layout = computeLayout(expandProgress());
	if (layout.volume.width() <= 0.) {
		return 0.;
	}
	return std::clamp(
		(point.x() - layout.volume.left()) / layout.volume.width(),
		0.,
		1.);
}

MusicIsland::Part MusicIsland::partAt(QPoint point) const {
	const auto expand = expandProgress();
	const auto layout = computeLayout(expand);
	if (!layout.island.contains(QPointF(point))) {
		return Part::None;
	}
	if (expand > 0.5) {
		if (layout.close.contains(QPointF(point))) {
			return Part::Close;
		} else if (profileMusicState() >= 0
			&& layout.saveProfile.contains(QPointF(point))) {
			return Part::SaveProfile;
		} else if (!layout.lyricsButton.isNull()
			&& layout.lyricsButton.contains(QPointF(point))) {
			return Part::LyricsButton;
		} else if (lyricsPanelVisible(expand)
			&& layout.lyricsBox.contains(QPointF(point))) {
			return Part::LyricsZone;
		} else if (layout.titleZone.contains(QPointF(point))) {
			// Title/artist strip is intentionally NOT a control: it reuses the
			// Body semantics so a tap can either expand the pill (collapsed)
			// or jump to the message when already expanded.
			return Part::Body;
		} else if (layout.volumeToggle.contains(QPointF(point))) {
			return Part::VolumeToggle;
		} else if (layout.playPause.contains(QPointF(point))) {
			return Part::PlayPause;
		} else if (layout.previous.contains(QPointF(point))) {
			return Part::Previous;
		} else if (layout.next.contains(QPointF(point))) {
			return Part::Next;
		} else if (!layout.speed.isEmpty()
			&& layout.speed.contains(QPointF(point))) {
			return Part::Speed;
		} else if (layout.repeat.contains(QPointF(point))) {
			return Part::Repeat;
		} else if (layout.order.contains(QPointF(point))) {
			return Part::Order;
		}
		const auto extend = float64(st::musicIslandSeekKnob);
		const auto seekArea = layout.seek.marginsAdded(
			QMarginsF(0., extend, 0., extend));
		if (seekArea.contains(QPointF(point))) {
			return Part::Seek;
		}
		const auto volumeExtend = float64(st::musicIslandVolumeKnob);
		const auto volumeArea = layout.volume.marginsAdded(
			QMarginsF(0., volumeExtend, 0., volumeExtend));
		if (volumeArea.contains(QPointF(point))) {
			return Part::Volume;
		}
	}
	if (layout.cover.contains(QPointF(point))) {
		return Part::Cover;
	}
	return Part::Body;
}

float64 MusicIsland::seekProgressFor(QPoint point) const {
	const auto layout = computeLayout(expandProgress());
	if (layout.seek.width() <= 0.) {
		return 0.;
	}
	return std::clamp(
		(point.x() - layout.seek.left()) / layout.seek.width(),
		0.,
		1.);
}

void MusicIsland::paintEvent(QPaintEvent *e) {
	const auto shown = shownProgress();
	if (shown <= 0.) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto expand = expandProgress();
	const auto layout = computeLayout(expand);

	p.setOpacity(shown);
	if (shown < 1.) {
		const auto scale = 0.88 + 0.12 * shown;
		const auto center = layout.island.center();
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}

	paintShadow(p, layout);
	paintBackground(p, layout);
	paintCoverArt(p, layout);
	paintTexts(p, layout, expand);
	if (expand > 0.) {
		paintControls(p, layout, expand);
		paintModes(p, layout, std::clamp((expand - 0.4) / 0.6, 0., 1.));
		paintVolume(p, layout, expand);
		paintSeek(p, layout, expand);
		paintTimes(p, layout, expand);
		if (lyricsPanelVisible(expand)) {
			paintLyrics(p, layout, expand);
		}
	}
	if (expand < 1.) {
		if (AyuSettings::getInstance().musicIslandEqualizer()) {
			paintEqualizer(p, layout, 1. - expand);
		}
		paintCollapsedProgress(p, layout, 1. - expand);
	}
}

void MusicIsland::paintShadow(QPainter &p, const Layout &layout) const {
	const auto opacity = p.opacity();
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0));
	for (auto i = kShadowLayers; i >= 1; --i) {
		const auto grow = float64(i) * st::musicIslandShadow / kShadowLayers;
		const auto rect = layout.island.marginsAdded(
			QMarginsF(grow, grow * 0.6, grow, grow));
		p.setOpacity(opacity * 0.05);
		p.drawRoundedRect(rect, layout.radius + grow, layout.radius + grow);
	}
	p.setOpacity(opacity);
}

void MusicIsland::paintBackground(QPainter &p, const Layout &layout) const {
	const auto opacity = p.opacity();
	const auto art = artMode();

	auto path = QPainterPath();
	path.addRoundedRect(layout.island, layout.radius, layout.radius);

	p.save();
	p.setClipPath(path);
	if (art) {
		// Soft diagonal wash in the cover's accent colour, deepened toward
		// the bottom so white text and controls stay readable.
		auto gradient = QLinearGradient(
			layout.island.topLeft(),
			layout.island.bottomLeft());
		gradient.setColorAt(0., AdjustLightness(_cover.accent, 1.28));
		gradient.setColorAt(0.55, AdjustLightness(_cover.accent, 1.0));
		gradient.setColorAt(1., AdjustLightness(_cover.accent, 0.62));
		p.fillRect(layout.island, gradient);

		if (!_cover.blurred.isNull()
			&& AyuSettings::getInstance().musicIslandBlurCover()) {
			p.setOpacity(opacity * 0.38);
			DrawToFill(p, layout.island, _cover.blurred);
			p.setOpacity(opacity);
		}

		// Gentle bottom vignette for depth, no harsh gloss.
		auto shade = QLinearGradient(
			layout.island.topLeft(),
			layout.island.bottomLeft());
		shade.setColorAt(0., QColor(0, 0, 0, 0));
		shade.setColorAt(0.6, QColor(0, 0, 0, 0));
		shade.setColorAt(1., QColor(0, 0, 0, 46));
		p.fillRect(layout.island, shade);
	} else {
		p.fillRect(layout.island, st::windowBg);
		auto card = QLinearGradient(
			layout.island.topLeft(),
			layout.island.bottomLeft());
		card.setColorAt(0., QColor(255, 255, 255, 26));
		card.setColorAt(0.45, QColor(255, 255, 255, 0));
		card.setColorAt(1., QColor(0, 0, 0, 26));
		p.fillRect(layout.island, card);
	}
	const auto &settings = AyuSettings::getInstance();
	if (!_customBackground.isNull()
		&& settings.musicIslandCustomBackgroundEnabled()) {
		p.setOpacity(opacity
			* settings.musicIslandCustomBackgroundOpacity() / 100.);
		const auto source = QRectF(QPointF(), QSizeF(_customBackground.size()));
		switch (settings.musicIslandCustomBackgroundMode()) {
		case 1: {
			const auto target = source.size().scaled(
				layout.island.size(),
				Qt::KeepAspectRatio);
			p.drawPixmap(QRectF(
				layout.island.center()
					- QPointF(target.width() / 2., target.height() / 2.),
				target), _customBackground, source);
		} break;
		case 2:
			p.drawPixmap(layout.island, _customBackground, source);
			break;
		default:
			DrawToFill(p, layout.island, _customBackground);
			break;
		}
		p.setOpacity(opacity);
		const auto dim = settings.musicIslandCustomBackgroundDim();
		if (dim > 0) {
			p.fillRect(layout.island, QColor(0, 0, 0, 255 * dim / 100));
		}
	}
	p.restore();

	const auto line = float64(st::lineWidth);
	auto border = art ? QColor(255, 255, 255, 40) : st::shadowFg->c;
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(border, line));
	p.drawRoundedRect(
		layout.island.marginsRemoved(
			QMarginsF(line / 2., line / 2., line / 2., line / 2.)),
		layout.radius,
		layout.radius);
}

void MusicIsland::paintCoverArt(QPainter &p, const Layout &layout) const {
	const auto expand = expandProgress();
	const auto radius = st::musicIslandCollapsedCoverRadius
		+ (st::musicIslandExpandedCoverRadius
			- st::musicIslandCollapsedCoverRadius) * expand;
	const auto art = artMode();

	auto path = QPainterPath();
	path.addRoundedRect(layout.cover, radius, radius);

	p.save();
	p.setClipPath(path);
	if (!_cover.art.isNull()) {
		DrawToFill(p, layout.cover, _cover.art);
	} else {
		p.fillRect(
			layout.cover,
			art ? QColor(255, 255, 255, 28) : st::windowBgOver->c);
	}
	p.restore();

	if (_cover.art.isNull()) {
		PaintNoCover(p, layout.cover, subFg());
	}

	const auto line = float64(st::lineWidth);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(
		art ? QColor(255, 255, 255, 44) : st::shadowFg->c,
		line));
	p.drawRoundedRect(layout.cover, radius, radius);
}

void MusicIsland::paintTexts(
		QPainter &p,
		const Layout &layout,
		float64 expand) const {
	const auto width = layout.textWidth;
	if (width <= 0) {
		return;
	}
	const auto now = crl::now();
	const auto left = int(base::SafeRound(layout.title.x()));
	const auto titleTop = int(base::SafeRound(layout.title.y()));
	const auto full = _title.maxWidth();

	p.setPen(titleFg());
	if (full > width && expand > 0.99) {
		const auto offset = MarqueeOffset(full - width, now - _marqueeStart);
		p.save();
		p.setClipRect(
			QRectF(left, titleTop, width, TitleStyle().font->height + 2),
			Qt::IntersectClip);
		_title.draw(p, {
			.position = QPoint(left - int(base::SafeRound(offset)), titleTop),
			.availableWidth = full,
			.now = now,
		});
		p.restore();
	} else {
		_title.draw(p, {
			.position = QPoint(left, titleTop),
			.availableWidth = width,
			.now = now,
			.elisionLines = 1,
		});
	}

	const auto floating = floatingLineText();
	const auto showFloating = !floating.isEmpty()
		&& AyuSettings::getInstance().musicLyricsEnabled()
		&& AyuSettings::getInstance().musicLyricsFloatingLine()
		&& expand < 0.5;
	if (showFloating) {
		// Collapsed pill shows the current lyric line instead of the
		// performer: readable, non-intrusive, fades on line change.
		const auto fade = anim::easeOutCubic(1., std::clamp(
			float64(crl::now() - _lyricsLineChangedAt) / 340.,
			0.,
			1.));
		p.save();
		p.setOpacity(p.opacity() * (0.35 + 0.65 * fade));
		p.setPen(titleFg());
		p.setFont(PerformerStyle().font);
		const auto top = int(base::SafeRound(layout.performer.y()));
		p.drawText(
			QRect(left, top, width, PerformerStyle().font->height + 2),
			Qt::AlignLeft | Qt::AlignVCenter,
			p.fontMetrics().elidedText(floating, Qt::ElideRight, width));
		p.restore();
		return;
	}

	if (_performer.isEmpty()) {
		return;
	}
	p.setPen(subFg());
	_performer.draw(p, {
		.position = QPoint(left, int(base::SafeRound(layout.performer.y()))),
		.availableWidth = width,
		.now = now,
		.elisionLines = 1,
	});
}

void MusicIsland::paintControls(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto base = p.opacity();
	p.setOpacity(base * opacity);

	const auto art = artMode();
	const auto player = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	const auto paintButton = [&](
			const QRectF &rect,
			const style::icon &icon,
			Part part,
			bool enabled,
			bool filled) {
		const auto over = enabled && (_hoveredPart == part);
		const auto pressed = enabled && (_pressedPart == part);
		auto alpha = 0;
		if (filled) {
			alpha = pressed ? 56 : (over ? 44 : 28);
		} else if (pressed) {
			alpha = 40;
		} else if (over) {
			alpha = 26;
		}
		if (alpha > 0) {
			auto background = art ? QColor(255, 255, 255) : st::windowBoldFg->c;
			background.setAlpha(alpha);
			p.setPen(Qt::NoPen);
			p.setBrush(background);
			p.drawEllipse(rect);
		}
		auto color = iconFg();
		if (!enabled) {
			color.setAlpha(color.alpha() / 3);
		}
		icon.paintInCenter(p, rect.toRect(), color);
	};

	paintButton(
		layout.previous,
		st::musicIslandIconPrevious,
		Part::Previous,
		player->previousAvailable(type),
		false);
	paintButton(
		layout.playPause,
		_playing ? st::musicIslandIconPause : st::musicIslandIconPlay,
		Part::PlayPause,
		true,
		true);
	paintButton(
		layout.next,
		st::musicIslandIconNext,
		Part::Next,
		player->nextAvailable(type),
		false);
	paintButton(
		layout.close,
		st::musicIslandIconClose,
		Part::Close,
		true,
		false);
	const auto hoverDisc = [&](const QRectF &rect, Part part) {
		const auto alpha = (_pressedPart == part) ? 40 : (_hoveredPart == part) ? 26 : 0;
		if (alpha > 0) {
			auto background = art ? QColor(255, 255, 255) : st::windowBoldFg->c;
			background.setAlpha(alpha);
			p.setPen(Qt::NoPen);
			p.setBrush(background);
			p.drawEllipse(rect);
		}
	};
	const auto stroke = std::max(1.4, float64(st::lineWidth) * 1.6);
	const auto profile = profileMusicState();
	if (profile >= 0) {
		const auto &rect = layout.saveProfile;
		hoverDisc(rect, Part::SaveProfile);
		auto star = QPainterPath();
		const auto center = rect.center();
		const auto outer = rect.width() * 0.34;
		const auto inner = outer * 0.46;
		for (auto i = 0; i != 10; ++i) {
			const auto angle = -1.5707963267948966 + i * 0.6283185307179586;
			const auto r = (i % 2) ? inner : outer;
			const auto point = center + QPointF(std::cos(angle) * r, std::sin(angle) * r);
			if (i) {
				star.lineTo(point);
			} else {
				star.moveTo(point);
			}
		}
		star.closeSubpath();
		if (profile > 0) {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 200, 70));
		} else {
			p.setPen(QPen(iconFg(), stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.setBrush(Qt::NoBrush);
		}
		p.drawPath(star);
	}
	if (!layout.lyricsButton.isNull()) {
		const auto &rect = layout.lyricsButton;
		hoverDisc(rect, Part::LyricsButton);
		const auto c = rect.center();
		const auto w = rect.width();
		const auto body = QRectF(c.x() - w * 0.28, c.y() - w * 0.25, w * 0.56, w * 0.4);
		const auto radius = w * 0.12;
		auto bubble = QPainterPath();
		bubble.moveTo(body.left() + radius, body.top());
		bubble.lineTo(body.right() - radius, body.top());
		bubble.quadTo(body.topRight(), QPointF(body.right(), body.top() + radius));
		bubble.lineTo(body.right(), body.bottom() - radius);
		bubble.quadTo(body.bottomRight(), QPointF(body.right() - radius, body.bottom()));
		bubble.lineTo(c.x() - w * 0.04, body.bottom());
		bubble.lineTo(c.x() - w * 0.17, body.bottom() + w * 0.13);
		bubble.lineTo(c.x() - w * 0.14, body.bottom());
		bubble.lineTo(body.left() + radius, body.bottom());
		bubble.quadTo(body.bottomLeft(), QPointF(body.left(), body.bottom() - radius));
		bubble.lineTo(body.left(), body.top() + radius);
		bubble.quadTo(body.topLeft(), QPointF(body.left() + radius, body.top()));
		bubble.closeSubpath();
		p.setPen(QPen(iconFg(), stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(bubble);
		p.setPen(Qt::NoPen);
		p.setBrush(iconFg());
		for (const auto dx : { -0.075, 0.075 }) {
			const auto dot = QPointF(body.center().x() + w * dx, body.center().y() - w * 0.02);
			p.drawEllipse(dot, w * 0.045, w * 0.045);
			auto tail = QPainterPath();
			tail.moveTo(dot + QPointF(w * 0.045, 0.));
			tail.quadTo(dot + QPointF(w * 0.05, w * 0.09), dot + QPointF(-w * 0.03, w * 0.12));
			tail.lineTo(dot + QPointF(-w * 0.01, w * 0.03));
			tail.closeSubpath();
			p.drawPath(tail);
		}
	}

	p.setOpacity(base);
}

void MusicIsland::paintModes(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	if (opacity <= 0.) {
		return;
	}
	const auto base = p.opacity();
	p.setOpacity(base * opacity);

	const auto art = artMode();
	const auto &settings = Core::App().settings();
	const auto activeColor = art
		? QColor(255, 255, 255)
		: st::windowBgActive->c;
	const auto hoverBackground = [&](Part part) {
		const auto over = (_hoveredPart == part);
		const auto pressed = (_pressedPart == part);
		auto background = art ? QColor(255, 255, 255) : st::windowBoldFg->c;
		background.setAlpha(pressed ? 40 : over ? 26 : 0);
		return background;
	};
	const auto paintMode = [&](
			const QRectF &rect,
			const style::icon &icon,
			Part part,
			bool active) {
		const auto background = hoverBackground(part);
		if (background.alpha() > 0) {
			p.setPen(Qt::NoPen);
			p.setBrush(background);
			p.drawEllipse(rect);
		}
		auto color = active ? activeColor : iconFg();
		if (!active) {
			color.setAlpha((_hoveredPart == part) ? 190 : 110);
		}
		icon.paintInCenter(p, rect.toRect(), color);
		if (active) {
			const auto dot = 3.;
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawEllipse(QRectF(
				rect.center().x() - dot / 2.,
				rect.bottom() - dot - 1.,
				dot,
				dot));
		}
	};

	const auto repeat = settings.playerRepeatMode();
	paintMode(
		layout.repeat,
		(repeat == Media::RepeatMode::One)
			? st::musicIslandIconRepeatOne
			: st::musicIslandIconRepeat,
		Part::Repeat,
		repeat != Media::RepeatMode::None);

	const auto order = settings.playerOrderMode();
	paintMode(
		layout.order,
		(order == Media::OrderMode::Shuffle)
			? st::musicIslandIconShuffle
			: st::musicIslandIconOrder,
		Part::Order,
		order != Media::OrderMode::Default);

	if (!layout.speed.isEmpty()) {
		const auto speed = currentSpeed();
		const auto changed = !Media::EqualSpeeds(speed, 1.);
		const auto rect = layout.speed;
		const auto radius = rect.height() / 2.;
		auto fill = hoverBackground(Part::Speed);
		if (changed) {
			auto tint = activeColor;
			tint.setAlpha((art ? 56 : 40) + fill.alpha() / 2);
			fill = tint;
		}
		auto border = changed ? activeColor : iconFg();
		border.setAlpha(changed ? 150 : 70);
		p.setPen(QPen(border, float64(st::lineWidth)));
		if (fill.alpha() > 0) {
			p.setBrush(fill);
		} else {
			p.setBrush(Qt::NoBrush);
		}
		p.drawRoundedRect(
			rect.marginsRemoved(QMarginsF(0.5, 0.5, 0.5, 0.5)),
			radius,
			radius);
		auto color = changed ? activeColor : iconFg();
		if (!changed) {
			color.setAlpha((_hoveredPart == Part::Speed) ? 220 : 170);
		}
		p.setPen(color);
		p.setFont(st::musicIslandSpeedFont);
		p.drawText(
			rect,
			Qt::AlignCenter,
			QString::number(speed, 'g', 3) + QChar('x'));
	}

	p.setOpacity(base);
}

bool MusicIsland::hasSpeedControl() const {
	return _songId.changeablePlaybackSpeed()
		&& Media::Audio::SupportsSpeedControl();
}

float64 MusicIsland::currentSpeed() const {
	return Core::App().settings().audioPlaybackSpeed();
}

void MusicIsland::applySpeed(float64 speed) {
	Core::App().settings().setAudioPlaybackSpeed(speed);
	Core::App().saveSettingsDelayed();
	Media::Player::instance()->updatePlaybackSpeed();
	update();
}

void MusicIsland::toggleRepeat() {
	auto &settings = Core::App().settings();
	settings.setPlayerRepeatMode([&] {
		switch (settings.playerRepeatMode()) {
		case Media::RepeatMode::None: return Media::RepeatMode::One;
		case Media::RepeatMode::One: return Media::RepeatMode::All;
		case Media::RepeatMode::All: return Media::RepeatMode::None;
		}
		return Media::RepeatMode::None;
	}());
	Core::App().saveSettingsDelayed();
	update();
}

void MusicIsland::showMenuBelow(const QRectF &anchor) {
	_menu->setDestroyedCallback(crl::guard(this, [=] {
		crl::on_main(this, [=] {
			updateExpanded();
			update();
		});
	}));
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(mapToGlobal(QPoint(
		int(base::SafeRound(anchor.right())),
		int(base::SafeRound(anchor.bottom())) + st::musicIslandModeSkip)));
	updateExpanded();
	update();
}

void MusicIsland::showOrderMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto current = Core::App().settings().playerOrderMode();
	const auto menu = _menu->menu();
	const auto add = [&](Media::OrderMode mode) {
		const auto active = (current == mode);
		const auto reverse = (mode == Media::OrderMode::Reverse);
		const auto label = reverse
			? tr::lng_audio_player_reverse(tr::now)
			: tr::lng_audio_player_shuffle(tr::now);
		const auto icon = reverse
			? (active
				? &st::mediaPlayerOrderIconReverseActive
				: &st::mediaPlayerOrderIconReverse)
			: (active
				? &st::mediaPlayerOrderIconShuffleActive
				: &st::mediaPlayerOrderIconShuffle);
		const auto callback = [=] {
			Core::App().settings().setPlayerOrderMode(active
				? Media::OrderMode::Default
				: mode);
			Core::App().saveSettingsDelayed();
		};
		_menu->addAction(base::make_unique_q<Ui::Menu::Action>(
			menu.get(),
			active ? st::mediaPlayerOrderMenuActive : st::mediaPlayerOrderMenu,
			Ui::Menu::CreateAction(menu.get(), label, callback),
			icon,
			icon));
	};
	add(Media::OrderMode::Reverse);
	add(Media::OrderMode::Shuffle);
	showMenuBelow(computeLayout(expandProgress()).order);
}

void MusicIsland::showSpeedMenu() {
	if (!hasSpeedControl()) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	Media::Player::FillSpeedMenu(
		_menu->menu(),
		st::mediaPlayerSpeedMenu,
		rpl::single(
			currentSpeed()
		) | rpl::then(
			Core::App().settings().audioPlaybackSpeedChanges(
			) | rpl::map([] {
				return Core::App().settings().audioPlaybackSpeed();
			})
		),
		crl::guard(this, [=](float64 speed) { applySpeed(speed); }));
	showMenuBelow(computeLayout(expandProgress()).speed);
}

void MusicIsland::paintVolume(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto base = p.opacity();
	p.setOpacity(base * opacity);

	const auto art = artMode();
	const auto progress = _volumeDragging
		? _volumeDragProgress
		: songVolume();
	auto inactive = art ? QColor(255, 255, 255, 56) : st::windowBgOver->c;
	auto active = art ? QColor(255, 255, 255, 235) : st::windowBgActive->c;
	FillProgress(p, layout.volume, progress, inactive, active);

	if (_hoveredPart == Part::Volume
		|| _hoveredPart == Part::VolumeToggle
		|| _volumeDragging) {
		const auto knob = float64(st::musicIslandVolumeKnob);
		const auto x = layout.volume.left() + layout.volume.width() * progress;
		const auto y = layout.volume.center().y();
		p.setPen(Qt::NoPen);
		p.setBrush(art ? QColor(255, 255, 255) : st::windowBgActive->c);
		p.drawEllipse(QRectF(x - knob / 2., y - knob / 2., knob, knob));
	}

	const auto over = (_hoveredPart == Part::VolumeToggle)
		|| (_pressedPart == Part::VolumeToggle);
	if (over) {
		auto background = art ? QColor(255, 255, 255) : st::windowBoldFg->c;
		background.setAlpha(26);
		p.setPen(Qt::NoPen);
		p.setBrush(background);
		p.drawEllipse(layout.volumeToggle);
	}
	volumeIcon().paintInCenter(
		p,
		layout.volumeToggle.toRect(),
		iconFg());

	p.setOpacity(base);
}

void MusicIsland::paintSeek(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto base = p.opacity();
	p.setOpacity(base * opacity);

	const auto art = artMode();
	const auto progress = displayedProgress();
	auto inactive = art ? QColor(255, 255, 255, 56) : st::windowBgOver->c;
	auto active = art ? QColor(255, 255, 255, 235) : st::windowBgActive->c;
	FillProgress(p, layout.seek, progress, inactive, active);

	if (_hoveredPart == Part::Seek || _seeking) {
		const auto knob = float64(st::musicIslandSeekKnob);
		const auto x = layout.seek.left() + layout.seek.width() * progress;
		const auto y = layout.seek.center().y();
		p.setPen(Qt::NoPen);
		p.setBrush(art ? QColor(255, 255, 255) : st::windowBgActive->c);
		p.drawEllipse(QRectF(x - knob / 2., y - knob / 2., knob, knob));
	}

	p.setOpacity(base);
}

void MusicIsland::paintTimes(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto base = p.opacity();
	p.setOpacity(base * opacity);

	const auto font = TimeStyle().font;
	const auto baseline = layout.timesTop + font->ascent;
	p.setFont(font);
	p.setPen(subFg());
	p.drawText(QPointF(layout.seek.left(), baseline), _playedText);
	p.drawText(
		QPointF(
			layout.seek.right() - font->width(_durationText),
			baseline),
		_durationText);

	p.setOpacity(base);
}

void MusicIsland::paintEqualizer(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto base = p.opacity();
	p.setOpacity(base * opacity);

	const auto art = artMode();
	const auto width = float64(st::musicIslandEqualizerWidth);
	const auto skip = float64(st::musicIslandEqualizerSkip);
	const auto full = layout.equalizer.height();
	const auto now = float64(crl::now());

	p.setPen(Qt::NoPen);
	p.setBrush(art ? QColor(255, 255, 255, 224) : st::windowBgActive->c);
	for (auto i = 0; i != st::musicIslandEqualizerCount; ++i) {
		auto ratio = 0.3;
		if (_playing) {
			const auto speed = 0.0024 + 0.0008 * i;
			const auto phase = 1.7 * i;
			ratio = 0.28 + 0.72 * std::abs(std::sin(now * speed + phase));
		}
		const auto height = full * ratio;
		const auto rect = QRectF(
			layout.equalizer.left() + i * (width + skip),
			layout.equalizer.bottom() - height,
			width,
			height);
		p.drawRoundedRect(rect, width / 2., width / 2.);
	}

	p.setOpacity(base);
}

void MusicIsland::paintCollapsedProgress(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto base = p.opacity();
	p.setOpacity(base * opacity);

	const auto art = artMode();
	auto inactive = art ? QColor(255, 255, 255, 48) : st::windowBgOver->c;
	auto active = art ? QColor(255, 255, 255, 216) : st::windowBgActive->c;
	FillProgress(
		p,
		layout.collapsedProgress,
		displayedProgress(),
		inactive,
		active);

	p.setOpacity(base);
}

void MusicIsland::mousePressEvent(QMouseEvent *e) {
	if (!_shown || e->button() != Qt::LeftButton) {
		return;
	}
	_pressedPart = partAt(e->pos());
	if (_pressedPart == Part::None) {
		return;
	}
	_down = true;
	_downPoint = e->pos();
	_downGlobalPoint = e->globalPos();
	_lastMousePos = e->globalPos();
	_dragStartPosition = pos();
	if (_pressedPart == Part::Seek) {
		_seeking = true;
		_keepExpandedAfterSeek = true;
		_seekProgress = seekProgressFor(e->pos());
		_playedText = Ui::FormatDurationText(
			int64(base::SafeRound(_seekProgress * _durationSeconds)));
		Media::Player::instance()->startSeeking(AudioMsgId::Type::Song);
		grabMouse();
		updateExpanded();
	} else if (_pressedPart == Part::Volume) {
		_volumeDragging = true;
		_volumeDragProgress = volumeProgressFor(e->pos());
		applySongVolume(_volumeDragProgress);
		grabMouse();
	}
	update();
}

void MusicIsland::mouseMoveEvent(QMouseEvent *e) {
	if (_volumeDragging) {
		_volumeDragProgress = volumeProgressFor(e->pos());
		applySongVolume(_volumeDragProgress);
		return;
	} else if (_seeking) {
		_seekProgress = seekProgressFor(e->pos());
		_playedText = Ui::FormatDurationText(
			int64(base::SafeRound(_seekProgress * _durationSeconds)));
		update();
		return;
	}

	if (!_drag
		&& _down
		&& ((_pressedPart == Part::Body) || (_pressedPart == Part::Cover))
		&& ((e->pos() - _downPoint).manhattanLength()
			> QApplication::startDragDistance())) {
		if (AyuSettings::getInstance().musicIslandLocked()) {
			hintLockedPosition();
			updateHovered(e->pos());
			return;
		}
		// Grab the pointer for the whole drag: without it the island loses
		// the mouse as soon as the cursor outruns it (the widget itself
		// moves), the drag then dead-ends until a fresh click.
		grabMouse();
		_mouseGrabbed = true;
		_drag = true;
	}

	if (_drag) {
		// Drag the island by its grabbed point, not by its top-left: the
		// widget can resize (expand) mid-drag, and the anchor keeps the pill
		// under the finger instead of leaping under the mouse.
		auto target = _dragStartPosition + (e->globalPos() - _downGlobalPoint);
		if (const auto parent = parentWidget()) {
			const auto margin = std::max(0, int(st::musicIslandShadow / 2));
			target.setX(std::clamp(
				target.x(),
				margin,
				std::max(margin, parent->width() - width() - margin)));
			target.setY(std::clamp(
				target.y(),
				margin,
				std::max(margin, parent->height() - height() - margin)));
		}
		move(target);
		return;
	}

	updateHovered(e->pos());
}

void MusicIsland::updateHovered(QPoint point) {
	const auto part = partAt(point);
	if (_hoveredPart != part) {
		_hoveredPart = part;
		updateCursor();
		update();
	}
	const auto hovered = (part != Part::None) || _keepExpandedAfterSeek;
	if (_hovered != hovered) {
		_hovered = hovered;
		updateExpanded();
	}
}

void MusicIsland::mouseReleaseEvent(QMouseEvent *e) {
	const auto part = _pressedPart;
	const auto was = _down;
	_down = false;
	_pressedPart = Part::None;

	if (_volumeDragging) {
		_volumeDragging = false;
		releaseMouse();
		applySongVolume(_volumeDragProgress);
		updateExpanded();
		updateHovered(e->pos());
		update();
		return;
	} else if (_seeking) {
		Media::Player::instance()->finishSeeking(
			AudioMsgId::Type::Song,
			_seekProgress);
		const auto releasePoint = mapFromGlobal(e->globalPos());
		_seeking = false;
		_hovered = true;
		_hoveredPart = partAt(releasePoint);
		updateCursor();
		updateExpanded();
		update();
		releaseMouse();
		updateHovered(releasePoint);
		return;
	} else if (_drag) {
		_drag = false;
		if (_mouseGrabbed) {
			_mouseGrabbed = false;
			releaseMouse();
		}
		saveFreePosition();
		updatePosition();
		updateHovered(mapFromGlobal(QCursor::pos()));
		return;
	}
	if (was && (part != Part::None) && (partAt(e->pos()) == part)) {
		activate(part, e->pos());
	}
	update();
}

void MusicIsland::enterEventHook(QEnterEvent *e) {
	_keepExpandedAfterSeek = false;
	updateHovered(e->pos());
	return RpWidget::enterEventHook(e);
}

void MusicIsland::leaveEventHook(QEvent *e) {
	if (_seeking || _volumeDragging || _drag) {
		return RpWidget::leaveEventHook(e);
	}
	const auto global = QCursor::pos();
	const auto local = mapFromGlobal(global);
	if (rect().contains(local)) {
		return RpWidget::leaveEventHook(e);
	}
	if (_hoveredPart != Part::None) {
		_hoveredPart = Part::None;
		updateCursor();
		update();
	}
	if (_keepExpandedAfterSeek) {
		_keepExpandedAfterSeek = false;
	}
	if (_hovered) {
		_hovered = false;
		updateExpanded();
	}
	return RpWidget::leaveEventHook(e);
}

void MusicIsland::requestLyrics() {
	const auto &s = AyuSettings::getInstance();
	if (!s.musicLyricsEnabled() || _titleRaw.isEmpty()) {
		_lyrics = Ayu::Ui::Lyrics::Data();
		_lyricsActive = -1;
		_lyricsLoading = false;
		pushLyricsToWindow();
		return;
	}
	_lyrics = Ayu::Ui::Lyrics::Data();
	_lyricsLoading = true;
	_lyricsActive = -1;
	const auto generation = ++_lyricsGeneration;
	const auto performer = _performerRaw;
	const auto title = _titleRaw;
	const auto duration = int(_durationSeconds);
	const auto weak = base::make_weak(this);
	crl::async([=] {
		auto data = Ayu::Ui::Lyrics::FetchCachedSync(performer, title, duration);
		crl::on_main([=, data = std::move(data)]() mutable {
			if (const auto strong = weak.get()) {
				strong->applyLyrics(std::move(data), generation);
			}
		});
	});
	pushLyricsToWindow();
	updatePosition();
}

void MusicIsland::applyLyrics(Ayu::Ui::Lyrics::Data data, int generation) {
	if (generation != _lyricsGeneration) {
		return;
	}
	_lyricsLoading = false;
	_lyrics = std::move(data);
	_lyricsAligned = false;
	_lyricsActive = -1;
	_lyricsProgress = -1.;
	_lyricsLineChangedAt = crl::now();
	_lyricsFade.start([=] { update(); }, 0., 1., 280, anim::easeOutCubic);
	updateLyricsPosition();
	updatePosition();
	if (_lyrics.hasData
		&& AyuSettings::getInstance().musicLyricsKaraokeButton()) {
		ensureLyricsWindow();
	}
	pushLyricsToWindow();
	maybeStartAnalysis();
	alignLyrics();
	update();
}

void MusicIsland::alignLyrics() {
	if (_lyricsAligned || !_lyrics.hasData || !_analysis || !_analysis->ok) {
		return;
	}
	_lyricsAligned = true;
	const auto analysis = _analysis;
	const auto generation = _lyricsGeneration;
	const auto weak = base::make_weak(this);
	crl::async([=, data = _lyrics]() mutable {
		const auto report = Ayu::Ui::Lyrics::Align(data, *analysis);
		crl::on_main([=, data = std::move(data)]() mutable {
			const auto strong = weak.get();
			if (!strong || generation != strong->_lyricsGeneration || !report.changed) {
				return;
			}
			LOG(("Karaoke sync: %1, offset %2 ms, %3 lines placed by voice").arg(
				report.autoSynced ? u"built from audio"_q : u"fitted to audio"_q,
				QString::number(report.offsetMs),
				QString::number(report.refinedLines)));
			strong->_lyrics = std::move(data);
			strong->_lyricsActive = -1;
			strong->updateLyricsPosition();
			strong->pushLyricsToWindow();
			strong->update();
		});
	});
}

int64 MusicIsland::playbackPositionMs() const {
	if (!_playing || !_positionAt) {
		return _positionBaseMs;
	}
	const auto elapsed = std::min<crl::time>(crl::now() - _positionAt, 2000);
	return _positionBaseMs + int64(elapsed * currentSpeed());
}

float64 MusicIsland::lyricsPulse(int64 position) const {
	if (!_analysis
		|| !_analysis->ok
		|| !AyuSettings::getInstance().musicLyricsBassPulse()) {
		return 0.;
	}
	return _analysis->pulseAt(position) * (_analysis->inChorus(position) ? 1. : 0.5);
}

void MusicIsland::updateLyricsPosition() {
	if (!hasLyrics()) {
		_lyricsActive = -1;
		_lyricsProgress = -1.;
		return;
	}
	_lyricsPositionMs = playbackPositionMs();
	auto active = -1;
	if (_lyrics.synced) {
		active = Ayu::Ui::Lyrics::ActiveLineIndex(_lyrics, _lyricsPositionMs + 120);
	} else {
		const auto n = int(_lyrics.lines.size());
		active = std::clamp(int(std::clamp(displayedProgress(), 0., 1.) * n), 0, n - 1);
	}
	if (active != _lyricsActive) {
		_lyricsDirection = (active < _lyricsActive) ? -1 : 1;
		_lyricsActive = active;
		_lyricsLineChangedAt = crl::now();
		_lyricsFade.start([=] { update(); }, 0., 1., 340, anim::easeOutCubic);
	}
	_lyricsProgress = (_lyrics.synced && active >= 0)
		? Ayu::Ui::Lyrics::LineProgress(_lyrics, active, _lyricsPositionMs)
		: -1.;
}

void MusicIsland::pushPlaybackToWindow() {
	if (!_lyricsWindow) {
		return;
	}
	_lyricsWindow->setPlayback(
		playbackPositionMs(),
		_durationSeconds * 1000,
		_playing,
		currentSpeed());
}

void MusicIsland::pushLyricsToWindow() {
	if (!_lyricsWindow) {
		return;
	}
	_lyricsWindow->setTrack(_performerRaw, _titleRaw);
	_lyricsWindow->setCover(_cover.art, _cover.blurred, _cover.accent);
	_lyricsWindow->setLyrics(_lyrics, _lyricsLoading);
	_lyricsWindow->setAnalysis(_analysis);
	pushPlaybackToWindow();
}

void MusicIsland::resetAnalysis() {
	++_analysisGeneration;
	_analysis = nullptr;
	_analysisStarted = false;
	_analysisWaiting = false;
	_analysisLifetime.destroy();
	if (_lyricsWindow) {
		_lyricsWindow->setAnalysis(nullptr);
	}
}

void MusicIsland::maybeStartAnalysis() {
	const auto &s = AyuSettings::getInstance();
	const auto wanted = s.musicLyricsEnabled();
	const auto document = _songId.audio();
	if (_analysisStarted || !wanted || _lyricsLoading || !document || !_mediaView) {
		return;
	}
	if (!_mediaView->loaded()) {
		if (!_analysisWaiting) {
			_analysisWaiting = true;
			document->save(Data::FileOrigin(_contextId), QString());
			document->session().downloaderTaskFinished(
			) | rpl::filter([=] {
				return _mediaView && _mediaView->loaded();
			}) | rpl::take(1) | rpl::on_next([=] {
				_analysisWaiting = false;
				maybeStartAnalysis();
			}, _analysisLifetime);
		}
		return;
	}
	_analysisStarted = true;
	const auto location = document->location(true);
	const auto bytes = _mediaView->bytes();
	auto repeats = std::vector<Ayu::Ui::Analysis::Section>();
	if (s.musicLyricsChorus()) {
		for (const auto &[start, end] : Ayu::Ui::Lyrics::RepeatedBlocks(_lyrics)) {
			repeats.push_back({ start, end });
		}
	}
	const auto generation = _analysisGeneration;
	const auto weak = base::make_weak(this);
	crl::async([=] {
		auto result = std::make_shared<Ayu::Ui::Analysis::Result>(
			Ayu::Ui::Analysis::Analyze(location, bytes, repeats));
		if (!AyuSettings::getInstance().musicLyricsChorus()) {
			result->chorus.clear();
		}
		crl::on_main([=, result = std::move(result)]() mutable {
			const auto strong = weak.get();
			if (!strong || strong->_analysisGeneration != generation) {
				return;
			}
			strong->_analysis = std::move(result);
			if (strong->_lyricsWindow) {
				strong->_lyricsWindow->setAnalysis(strong->_analysis);
			}
			strong->alignLyrics();
			strong->update();
		});
	});
}

void MusicIsland::seekToMs(int64 ms) {
	const auto frequency = _trackState.frequency
		? _trackState.frequency
		: Media::Player::kDefaultFrequency;
	const auto lengthMs = (_trackState.length > 0)
		? (_trackState.length * 1000 / frequency)
		: (_durationSeconds * 1000);
	if (lengthMs <= 0) {
		return;
	}
	const auto type = AudioMsgId::Type::Song;
	Media::Player::instance()->startSeeking(type);
	Media::Player::instance()->finishSeeking(
		type,
		std::clamp(float64(ms) / lengthMs, 0., 1.));
	_positionBaseMs = ms;
	_positionAt = crl::now();
	updateLyricsPosition();
	pushPlaybackToWindow();
}

QString MusicIsland::floatingLineText() const {
	if (!hasLyrics()) {
		return QString();
	}
	if (_lyricsActive >= 0 && _lyricsActive < int(_lyrics.lines.size())) {
		return _lyrics.lines[_lyricsActive].text;
	}
	if (_lyrics.synced
		&& !_lyrics.lines.empty()
		&& _lyricsPositionMs < _lyrics.lines.front().ms) {
		return _lyrics.lines.front().text;
	}
	return QString();
}

void MusicIsland::openLyricsWindow() {
	ensureLyricsWindow();
	if (!_lyricsWindow) {
		return;
	}
	pushLyricsToWindow();
	_lyricsWindow->toggle(true);
}

void MusicIsland::ensureLyricsWindow() {
	if (!_lyricsWindow) {
		const auto body = _controller->widget()->bodyWidget();
		if (!body) {
			return;
		}
		_lyricsWindow = Ui::CreateChild<LyricsWindow>(body);
		const auto weak = base::make_weak(this);
		_lyricsWindow->setSeekCallback([=](std::int64_t ms) {
			if (const auto strong = weak.get()) {
				strong->seekToMs(ms);
			}
		});
		_lyricsWindow->setSaveCallbacks([=] {
			const auto strong = weak.get();
			return strong ? strong->profileMusicState() : -1;
		}, [=] {
			if (const auto strong = weak.get()) {
				strong->toggleProfileMusic();
			}
		});
	}
}

void MusicIsland::paintFloatingLine(QPainter &p, const Layout &layout) const {
	Q_UNUSED(p);
	Q_UNUSED(layout);
}

namespace {

void PaintReadableText(
		QPainter &p,
		const QRectF &rect,
		const QString &text,
		const QFont &font,
		const QColor &color) {
	p.setFont(font);
	const auto elided = QFontMetrics(font).elidedText(
		text,
		Qt::ElideRight,
		int(rect.width()));
	p.setPen(QColor(0, 0, 0, int(color.alpha() * 0.6)));
	p.drawText(rect.translated(0., 1.), Qt::AlignCenter, elided);
	p.setPen(color);
	p.drawText(rect, Qt::AlignCenter, elided);
}

[[nodiscard]] QColor IslandWordColor(bool keyword, float64 lit) {
	auto result = keyword ? QColor(255, 46, 66) : QColor(255, 255, 255);
	const auto dim = keyword ? 0.45 : 0.38;
	result.setAlphaF(std::clamp(dim + (1. - dim) * lit, 0., 1.));
	return result;
}

} // namespace

void MusicIsland::paintLyrics(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto box = layout.lyricsBox;
	if (box.isEmpty() || !hasLyrics()) {
		return;
	}
	const auto base = p.opacity();
	p.setOpacity(base * opacity);
	const auto &s = AyuSettings::getInstance();
	const auto fontScale = s.musicLyricsFontScale() / 100.;
	const auto keywordOn = s.musicLyricsKeyword();
	const auto position = playbackPositionMs();
	const auto beat = lyricsPulse(position);

	const auto radius = float64(st::musicIslandLyricsRadius);
	auto path = QPainterPath();
	path.addRoundedRect(box, radius, radius);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(8, 8, 12, 150));
	p.drawPath(path);
	if (beat > 0.01) {
		auto glow = QRadialGradient(
			QPointF(box.center().x(), box.bottom() + box.height() * 0.3),
			box.width() * 0.7);
		glow.setColorAt(0., QColor(205, 22, 40, int(150 * beat)));
		glow.setColorAt(1., QColor(205, 22, 40, 0));
		p.save();
		p.setClipPath(path);
		p.fillRect(box, glow);
		p.restore();
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 28), float64(st::lineWidth)));
	p.drawPath(path);

	const auto fade = anim::easeOutCubic(1., std::clamp(
		float64(crl::now() - _lyricsLineChangedAt) / 340.,
		0.,
		1.));
	const auto slide = (1. - fade) * 8. * _lyricsDirection;
	const auto n = int(_lyrics.lines.size());
	const auto active = _lyricsActive;
	const auto lineText = [&](int i) {
		return (i >= 0 && i < n) ? _lyrics.lines[i].text : QString();
	};

	auto sub = st::normalFont->f;
	sub.setPixelSize(int(12 * fontScale));
	const auto prevRect = QRectF(box.left() + 12, box.top() + 6, box.width() - 24, 18);
	const auto nextRect = QRectF(box.left() + 12, box.bottom() - 24, box.width() - 24, 18);
	p.save();
	p.setOpacity(p.opacity() * (0.55 + 0.45 * fade));
	PaintReadableText(p, prevRect, lineText(active - 1), sub, QColor(200, 200, 208, 150));
	PaintReadableText(
		p,
		nextRect,
		lineText((active >= 0) ? (active + 1) : 1),
		sub,
		QColor(200, 200, 208, 170));
	p.restore();

	const auto index = (active >= 0) ? active : 0;
	if (index >= n) {
		p.setOpacity(base);
		return;
	}
	const auto &line = _lyrics.lines[index];
	auto words = line.words;
	if (words.empty()) {
		const auto parts = line.text.split(QChar(u' '), Qt::SkipEmptyParts);
		for (auto i = 0; i != parts.size(); ++i) {
			auto word = Ayu::Ui::Lyrics::Word();
			word.text = parts[i] + ((i + 1 < parts.size()) ? u" "_q : QString());
			words.push_back(std::move(word));
		}
	}
	auto font = st::normalFont->f;
	font.setPixelSize(int(15 * fontScale));
	font.setWeight(QFont::Bold);
	const auto available = box.width() - 24.;
	auto total = 0.;
	for (const auto &word : words) {
		total += QFontMetricsF(font).horizontalAdvance(word.text);
	}
	if (total > available && total > 0.) {
		font.setPixelSize(std::max(11, int(font.pixelSize() * available / total)));
		total = 0.;
		for (const auto &word : words) {
			total += QFontMetricsF(font).horizontalAdvance(word.text);
		}
	}
	const auto metrics = QFontMetricsF(font);
	const auto lit = (active >= 0) ? 1. : 0.;
	auto x = box.left() + std::max(12., (box.width() - total) / 2.);
	const auto baselineY = box.center().y() + metrics.ascent() / 2. - 1. + slide;
	p.save();
	p.setClipRect(box.adjusted(8, 0, -8, 0), Qt::IntersectClip);
	p.setOpacity(p.opacity() * fade);
	p.setFont(font);
	for (const auto &word : words) {
		const auto text = word.text.trimmed();
		const auto width = metrics.horizontalAdvance(text);
		const auto keyword = word.keyword && keywordOn;
		const auto progress = (_lyrics.synced && active >= 0)
			? Ayu::Ui::Lyrics::WordProgress(word, position)
			: lit;
		const auto baseline = QPointF(x, baselineY - 1.5 * progress * lit);
		if (keyword && progress > 0.) {
			const auto shift = 1. + 1.5 * beat;
			p.setPen(QColor(0, 230, 255, int(90 * (0.4 + beat))));
			p.drawText(baseline + QPointF(-shift, 0.), text);
			p.setPen(QColor(255, 0, 70, int(110 * (0.4 + beat))));
			p.drawText(baseline + QPointF(shift, 0.), text);
		}
		if (progress > 0. && progress < 1.) {
			auto gradient = QLinearGradient(QPointF(x, 0.), QPointF(x + width, 0.));
			gradient.setColorAt(0., IslandWordColor(keyword, 1.));
			gradient.setColorAt(std::max(0., progress - 0.2), IslandWordColor(keyword, 1.));
			gradient.setColorAt(std::min(1., progress + 0.2), IslandWordColor(keyword, 0.));
			gradient.setColorAt(1., IslandWordColor(keyword, 0.));
			p.setPen(QPen(QBrush(gradient), 1.));
		} else {
			p.setPen(IslandWordColor(keyword, progress * lit));
		}
		p.drawText(baseline, text);
		x += metrics.horizontalAdvance(word.text);
	}
	p.restore();
	p.setOpacity(base);
}

void MusicIsland::pollHover() {
	if (!_shown || _seeking || _volumeDragging || _drag || _menu
		|| (_lyricsWindow && _lyricsWindow->shown())) {
		return;
	}
	// A periodic pointer check with hysteresis. Native enter events can be
	// missed when the island appears or grows underneath a stationary cursor,
	// which used to make "expand on hover" look broken (only a click would
	// expand it). The generous trail margin while hovered keeps the expanded
	// state from flapping at the edges.
	const auto point = mapFromGlobal(QCursor::pos());
	const auto progress = expandProgress();
	const auto collapsed = computeLayout(0.).island.marginsAdded(
		QMarginsF(10., 8., 10., 8.));
	auto current = computeLayout(progress).island;
	if (_hovered) {
		current = current.marginsAdded(QMarginsF(28., 20., 28., 20.));
	}
	const auto hovered = _keepExpandedAfterSeek
		|| collapsed.contains(QPointF(point))
		|| current.contains(QPointF(point));
	if (_hovered == hovered) {
		return;
	}
	_hovered = hovered;
	if (!hovered
		&& _hoveredPart != Part::None
		&& !computeLayout(progress).island.contains(QPointF(point))) {
		_hoveredPart = Part::None;
		updateCursor();
	}
	if (!hovered) {
		_keepExpandedAfterSeek = false;
	}
	updateExpanded();
	update();
}

} // namespace AyuUi
