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
#include "media/player/media_player_instance.h"
#include "media/view/media_view_playback_progress.h"
#include "styles/palette.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "styles/style_menu_icons.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_music_island.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"

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

MusicIsland::~MusicIsland() = default;

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
		settings->musicLyricsAnimationChanges() | rpl::to_empty,
		settings->musicLyricsArtScatterChanges() | rpl::to_empty,
		settings->musicLyricsFontScaleChanges() | rpl::to_empty,
		settings->musicLyricsKaraokeButtonChanges() | rpl::to_empty,
		settings->musicLyricsBlurChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		if (AyuSettings::getInstance().musicLyricsEnabled()
			&& !hasLyrics()
			&& !_lyricsLoading
			&& !_titleRaw.isEmpty()) {
			requestLyrics();
		}
		updatePosition();
		update();
		if (_lyricsWindow && _lyricsWindow->isVisible()) {
			_lyricsWindow->setLyrics(_lyrics);
			_lyricsWindow->update();
		}
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
	} else if (_songId == id) {
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

	updateLyricsPosition();
	if (_lyricsWindow) {
		_lyricsWindow->setPosition(
			_lyricsPositionMs,
			_lyricsActive,
			_lyricsProgress);
	}
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
	if (_seeking || _volumeDragging || _keepExpandedAfterSeek) {
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
	const auto placementWidth = std::max(1, parent->width() - 2 * margin);
	const auto placementHeight = std::max(1, parent->height() - 2 * margin);
	const auto &settings = AyuSettings::getInstance();
	const auto normX = std::clamp(settings.musicIslandFreeX(), 0, 1000) / 1000.;
	const auto normY = std::clamp(settings.musicIslandFreeY(), 0, 1000) / 1000.;
	// The stored point is the island's center (see saveFreePosition), so
	// offset by half the current size: the island then stays visually in
	// place when it grows or shrinks by expanding or collapsing.
	return QPoint(
		margin + int(base::SafeRound(normX * placementWidth))
			- outer.width() / 2,
		margin + int(base::SafeRound(normY * placementHeight))
			- outer.height() / 2);
}

void MusicIsland::saveFreePosition() {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	const auto margin = std::max(0, int(st::musicIslandShadow / 2));
	const auto placementWidth = std::max(1, parent->width() - 2 * margin - width());
	const auto placementHeight = std::max(1, parent->height() - 2 * margin - height());
	auto &islandSettings = AyuSettings::getInstance();
	islandSettings.setMusicIslandPosition(4);
	// Store the island's center, not its top-left: the widget grows from the
	// top-left when it expands, so a corner-anchored position visibly jumps
	// while animating. The center is what the user actually placed.
	islandSettings.setMusicIslandFreeX(PercentNominal(
		pos().x() - margin + width() / 2,
		placementWidth));
	islandSettings.setMusicIslandFreeY(PercentNominal(
		pos().y() - margin + height() / 2,
		placementHeight));
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
	const auto headerButtonY = st::musicIslandCloseSkip;
	const auto closeSize = float64(st::musicIslandCloseSize);
	const auto saveSize = float64(st::musicIslandSaveSize);
	const auto controlGap = float64(st::musicIslandControlSkip);
	const auto canSave = canSaveToProfile();

	// Right-aligned top rail packed flush-right: [lyrics?] [save?] [close].
	const auto lyricsSize = float64(st::musicIslandLyricsButtonSize);
	const auto showLyricsBtn = AyuSettings::getInstance().musicLyricsEnabled()
		&& AyuSettings::getInstance().musicLyricsKaraokeButton()
		&& hasLyrics();
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
		&& settings.adaptiveCoverColor()
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
		saveToProfile();
		break;
	case Part::LyricsButton:
	case Part::LyricsZone:
		openLyricsWindow();
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
		} else if (canSaveToProfile()
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
	if (canSaveToProfile()) {
		paintButton(
			layout.saveProfile,
			st::musicIslandIconSaveProfile,
			Part::SaveProfile,
			true,
			false);
	}
	if (!layout.lyricsButton.isNull()) {
		paintButton(
			layout.lyricsButton,
			st::musicIslandIconLyrics,
			Part::LyricsButton,
			true,
			false);
	}

	p.setOpacity(base);
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
		return;
	}
	_lyricsLoading = true;
	_lyricsActive = -1;
	const auto generation = ++_lyricsGeneration;
	const auto performer = _performerRaw;
	const auto title = _titleRaw;
	const auto duration = int(_durationSeconds);
	const auto weak = base::make_weak(this);
	crl::async([=]() mutable {
		const auto data = Ayu::Ui::Lyrics::FetchCachedSync(
			performer,
			title,
			duration);
		crl::on_main([=]() mutable {
			if (const auto strong = weak.get()) {
				strong->applyLyrics(std::move(data), generation);
			}
		});
	});
	updatePosition();
}

void MusicIsland::applyLyrics(Ayu::Ui::Lyrics::Data data, int generation) {
	if (generation != _lyricsGeneration) {
		return;
	}
	_lyricsLoading = false;
	_lyrics = std::move(data);
	_lyricsActive = -1;
	_lyricsProgress = -1.;
	_lyricsLineChangedAt = crl::now();
	_lyricsFade.start([=] { update(); }, 0., 1., 280, anim::easeOutCubic);
	updateLyricsPosition();
	updatePosition();
	if (_lyricsWindow) {
		_lyricsWindow->setTrack(_performerRaw, _titleRaw);
		_lyricsWindow->setLyrics(_lyrics);
		_lyricsWindow->setPosition(
			_lyricsPositionMs,
			_lyricsActive,
			_lyricsProgress);
	}
	update();
}

void MusicIsland::updateLyricsPosition() {
	if (!hasLyrics()) {
		_lyricsActive = -1;
		_lyricsProgress = -1.;
		return;
	}
	const auto freq = _trackState.frequency
		? _trackState.frequency
		: Media::Player::kDefaultFrequency;
	_lyricsPositionMs = (freq > 0)
		? int64(_trackState.position * 1000 / freq)
		: 0;
	auto active = -1;
	auto progress = -1.;
	if (_lyrics.synced) {
		active = Ayu::Ui::Lyrics::ActiveLineIndex(_lyrics, _lyricsPositionMs);
		if (active >= 0) {
			progress = Ayu::Ui::Lyrics::LineProgress(
				_lyrics,
				active,
				_lyricsPositionMs);
		}
	} else {
		// No timestamps: spread lines evenly across track progress.
		const auto n = int(_lyrics.lines.size());
		if (n > 0) {
			const auto p = std::clamp(displayedProgress(), 0., 1.);
			active = std::clamp(int(p * n), 0, n - 1);
		}
	}
	if (active != _lyricsActive) {
		if (active > _lyricsActive) {
			_lyricsDirection = 1;
		} else if (active < _lyricsActive) {
			_lyricsDirection = -1;
		}
		_lyricsActive = active;
		_lyricsLineChangedAt = crl::now();
		_lyricsFade.start([=] { update(); }, 0., 1., 340, anim::easeOutCubic);
	}
	_lyricsProgress = progress;
}

QString MusicIsland::floatingLineText() const {
	if (!hasLyrics()) {
		return QString();
	}
	if (_lyrics.synced) {
		if (_lyricsActive >= 0 && _lyricsActive < int(_lyrics.lines.size())) {
			return _lyrics.lines[_lyricsActive].text;
		}
		// Before the first line: show it dimmed as a teaser.
		if (!_lyrics.lines.empty() && _lyricsPositionMs < _lyrics.lines.front().ms) {
			return _lyrics.lines.front().text;
		}
		return QString();
	}
	if (_lyricsActive >= 0 && _lyricsActive < int(_lyrics.lines.size())) {
		return _lyrics.lines[_lyricsActive].text;
	}
	return QString();
}

void MusicIsland::openLyricsWindow() {
	if (!_lyricsWindow) {
		_lyricsWindow = new LyricsWindow(window());
		_lyricsWindow->setWindowFlags(
			_lyricsWindow->windowFlags() | Qt::Window);
		_lyricsWindow->resize(400, 520);
	}
	_lyricsWindow->setTrack(_performerRaw, _titleRaw);
	_lyricsWindow->setLyrics(_lyrics);
	_lyricsWindow->setPosition(
		_lyricsPositionMs,
		_lyricsActive,
		_lyricsProgress);
	_lyricsWindow->show();
	_lyricsWindow->raise();
	_lyricsWindow->activateWindow();
}

void MusicIsland::paintFloatingLine(QPainter &p, const Layout &layout) const {
	// Floating collapsed line is painted inside paintTexts() by replacing
	// the performer row — kept as a no-op for layout symmetry.
	Q_UNUSED(p);
	Q_UNUSED(layout);
}

namespace {

float64 LyricsHash01(int seed, int salt) {
	auto h = uint32(seed * 2654435761u + salt * 40503u + 0x9e3779b9u);
	h ^= h >> 15;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	return (h % 1000) / 1000.;
}

void PaintReadableText(
		QPainter &p,
		const QRectF &rect,
		const QString &text,
		const QFont &font,
		const QBrush &color,
		Qt::Alignment align = Qt::AlignCenter) {
	p.save();
	p.setFont(font);
	// Soft shadow first for readability on any cover.
	p.setPen(QColor(0, 0, 0, int(color.color().alpha() * 0.6)));
	for (const auto &off : { QPointF(0, 1), QPointF(1, 0) }) {
		p.drawText(rect.translated(off), align | Qt::TextWordWrap, text);
	}
	p.setPen(QPen(color, 1.));
	p.drawText(rect, align | Qt::TextWordWrap, text);
	p.restore();
}

} // namespace

void MusicIsland::paintLyrics(
		QPainter &p,
		const Layout &layout,
		float64 opacity) const {
	const auto base = p.opacity();
	p.setOpacity(base * opacity);
	const auto box = layout.lyricsBox;
	if (box.isEmpty() || !hasLyrics()) {
		p.setOpacity(base);
		return;
	}
	const auto &s = AyuSettings::getInstance();
	const auto mode = s.musicLyricsAnimation();
	const auto scatter = s.musicLyricsArtScatter();
	const auto fontScale = s.musicLyricsFontScale() / 100.;

	// Readable dark pill regardless of the island theme.
	const auto radius = float64(st::musicIslandLyricsRadius);
	auto bg = QColor(8, 8, 12, s.musicLyricsBlur() ? 168 : 132);
	p.setPen(QPen(QColor(255, 255, 255, 28), float64(st::lineWidth)));
	p.setBrush(bg);
	p.drawRoundedRect(box, radius, radius);

	const auto now = crl::now();
	// Eased fade-in for the newly active line: a plain linear ramp reads as
	// a blink, cubic-out melts the line in.
	const auto fade = anim::easeOutCubic(1., std::clamp(
		float64(now - _lyricsLineChangedAt) / 340.,
		0.,
		1.));
	// The line slides in from the direction the lyrics are moving in, so
	// backward seeks slide up instead of down.
	const auto slide = (1. - fade) * 10. * _lyricsDirection;
	const auto n = int(_lyrics.lines.size());
	const auto active = _lyricsActive;

	auto lineText = [&](int i) -> QString {
		return (i >= 0 && i < n) ? _lyrics.lines[i].text : QString();
	};

	// Three-row karaoke: prev / active / next.
	const auto cx = box.center().x();
	auto sub = QFont();
	sub.setPixelSize(int(12 * fontScale));
	auto act = QFont();
	act.setPixelSize(int(15 * fontScale));
	act.setBold(true);

	const auto prevRect = QRectF(
		box.left() + 12, box.top() + 6, box.width() - 24, 18);
	const auto nextRect = QRectF(
		box.left() + 12, box.bottom() - 24, box.width() - 24, 18);
	const auto activeRect = QRectF(
		box.left() + 10,
		box.top() + 24 + slide,
		box.width() - 20,
		box.height() - 48);

	p.save();
	p.setOpacity(p.opacity() * (0.55 + 0.45 * fade));
	if (active > 0) {
		PaintReadableText(
			p, prevRect, lineText(active - 1), sub,
			QColor(200, 200, 208, 170));
	} else if (active == -1 && n > 0) {
		PaintReadableText(p, prevRect, lineText(0), sub, QColor(200, 200, 208, 150));
	}
	if (active >= 0 && active + 1 < n) {
		PaintReadableText(
			p, nextRect, lineText(active + 1), sub,
			QColor(200, 200, 208, 170));
	} else if (active == -1 && n > 1) {
		PaintReadableText(p, nextRect, lineText(1), sub, QColor(200, 200, 208, 150));
	}
	p.restore();

	const auto activeText = (active >= 0) ? lineText(active) : lineText(0);
	if (activeText.isEmpty()) {
		p.setOpacity(base);
		return;
	}

	if (!scatter) {
		// Clean centered modes without per-word rotation.
		if (mode == 0 && _lyricsProgress >= 0. && active >= 0) {
			// Karaoke fill with a soft gradient edge instead of a hard clip:
			// the fill "melts" over the line instead of a razor step.
			PaintReadableText(p, activeRect, activeText, act, QColor(190, 190, 198, int(255 * fade)));
			const auto fm = QFontMetrics(act);
			const auto w = fm.horizontalAdvance(
				fm.elidedText(activeText, Qt::ElideRight, int(activeRect.width())));
			const auto left = activeRect.left() + (activeRect.width() - w) / 2.;
			const auto filled = w * std::clamp(_lyricsProgress, 0., 1.);
			const auto feather = std::min(24., w / 4.);
			auto fill = QLinearGradient(
				QPointF(left, 0.),
				QPointF(left + filled, 0.));
			fill.setColorAt(0., QColor(255, 255, 255, int(255 * fade)));
			if (filled > feather) {
				fill.setColorAt((filled - feather) / filled, QColor(255, 255, 255, int(255 * fade)));
			}
			fill.setColorAt(1., QColor(255, 255, 255, 0));
			p.save();
			p.setClipRect(QRectF(left, activeRect.top(), filled + feather, activeRect.height()), Qt::IntersectClip);
			PaintReadableText(p, activeRect, activeText, act, fill);
			p.restore();
		} else if (mode == 1) {
			// Soft bloom that breathes with the fade rather than static
			// offset copies: the glow swells in as the line appears.
			const auto glow = 0.5 + 0.5 * std::sin(now / 900. + 1.2);
			const auto glowAlpha = int((18. + 14. * glow) * fade);
			for (auto i = 0; i < 3; ++i) {
				PaintReadableText(
					p,
					activeRect.translated((i - 1) * 1.4, (i % 2) * 1.4),
					activeText,
					act,
					QColor(120, 180, 255, glowAlpha));
			}
			PaintReadableText(p, activeRect, activeText, act, QColor(255, 255, 255, int(255 * fade)));
		} else if (mode == 2) {
			// Beat pulse: eased scale driven by the line fade, so the line
			// springs in on change and keeps a gentle breathing.
			const auto t = now / 1000.;
			const auto intro = 0.02 * (1. - fade);
			const auto sc = 1.
				+ intro
				+ 0.022 * std::sin(t * 3.4)
				+ 0.008 * std::sin(t * 8.1);
			p.save();
			p.translate(cx, activeRect.center().y());
			p.scale(sc, sc);
			p.translate(-cx, -activeRect.center().y());
			PaintReadableText(p, activeRect, activeText, act, QColor(255, 236, 175, int(255 * fade)));
			p.restore();
		} else {
			PaintReadableText(p, activeRect, activeText, act, QColor(255, 255, 255, int(255 * fade)));
		}
		p.setOpacity(base);
		return;
	}

	// Art-scatter: words slightly separated, each with own angle/offset.
	const auto words = activeText.split(u' ', Qt::SkipEmptyParts);
	if (words.isEmpty()) {
		p.setOpacity(base);
		return;
	}
	const auto fm = QFontMetrics(act);
	const auto spaceW = fm.horizontalAdvance(QChar(u' ')) + 4;
	auto totalW = 0.;
	auto widths = std::vector<int>();
	widths.reserve(words.size());
	for (const auto &w : words) {
		const auto ww = fm.horizontalAdvance(w);
		widths.push_back(ww);
		totalW += ww;
	}
	totalW += spaceW * std::max(0, int(words.size()) - 1);
	auto x = cx - totalW / 2.;
	const auto cy = activeRect.center().y();
	const auto seedBase = (active >= 0 ? active : 0) * 131 + 7;

	auto drawScatter = [&](const QBrush &color, const QRectF *clip) {
		p.save();
		if (clip) p.setClipRect(*clip, Qt::IntersectClip);
		p.setFont(act);
		auto xx = x;
		for (auto i = 0; i < int(words.size()); ++i) {
			const auto a = (LyricsHash01(seedBase, i * 2 + 1) - 0.5) * 10.;
			const auto dy = (LyricsHash01(seedBase, i * 2 + 2) - 0.5) * 6.;
			p.save();
			p.translate(xx + widths[i] / 2., cy + dy);
			if (a != 0.) p.rotate(a);
			p.setPen(QColor(0, 0, 0, int(color.color().alpha() * 0.6)));
			p.drawText(QRectF(-widths[i] / 2. + 1, -12, widths[i], 26), Qt::AlignCenter, words[i]);
			p.setPen(QPen(color, 1.));
			p.drawText(QRectF(-widths[i] / 2., -12, widths[i], 26), Qt::AlignCenter, words[i]);
			p.restore();
			xx += widths[i] + spaceW;
		}
		p.restore();
	};

	if (mode == 0 && _lyricsProgress >= 0. && active >= 0) {
		drawScatter(QColor(185, 185, 193, int(255 * fade)), nullptr);
		// Soft gradient edge on the fill, same as the clean mode.
		const auto filled = totalW * std::clamp(_lyricsProgress, 0., 1.);
		const auto feather = std::min(24., totalW / 4.);
		auto fill = QLinearGradient(QPointF(x, 0.), QPointF(x + filled, 0.));
		fill.setColorAt(0., QColor(255, 255, 255, int(255 * fade)));
		if (filled > feather) {
			fill.setColorAt((filled - feather) / filled, QColor(255, 255, 255, int(255 * fade)));
		}
		fill.setColorAt(1., QColor(255, 255, 255, 0));
		auto clip = QRectF(x, activeRect.top(), filled + feather, activeRect.height());
		drawScatter(fill, &clip);
	} else if (mode == 1) {
		const auto glow = 0.5 + 0.5 * std::sin(now / 900. + 1.2);
		const auto glowAlpha = int((16. + 12. * glow) * fade);
		for (auto k = 0; k < 3; ++k) {
			p.save();
			p.translate((k - 1) * 1.4, (k % 2) * 1.4);
			drawScatter(QColor(120, 180, 255, glowAlpha), nullptr);
			p.restore();
		}
		drawScatter(QColor(255, 255, 255, int(255 * fade)), nullptr);
	} else if (mode == 2) {
		const auto t = now / 1000.;
		const auto sc = 1.
			+ 0.02 * (1. - fade)
			+ 0.022 * std::sin(t * 3.4)
			+ 0.008 * std::sin(t * 8.1);
		p.save();
		p.translate(cx, cy);
		p.scale(sc, sc);
		p.translate(-cx, -cy);
		drawScatter(QColor(255, 236, 175, int(255 * fade)), nullptr);
		p.restore();
	} else {
		drawScatter(QColor(255, 255, 255, int(255 * fade)), nullptr);
	}
	p.setOpacity(base);
}

void MusicIsland::pollHover() {
	if (!_shown || _seeking || _volumeDragging || _drag) {
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
