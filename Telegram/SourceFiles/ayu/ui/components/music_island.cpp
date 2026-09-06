// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/music_island.h"

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
constexpr auto kSnapDuration = crl::time(360);
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
					_snapAnimation.stop();
					updatePosition();
				}
				checkVisibility();
			}
			return base::EventFilterResult::Continue;
		});
	}

	setupPlayer();
	setupSettings();

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
		if (!_snapAnimation.animating()) {
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
		settings->musicIslandEqualizerChanges()
	) | rpl::on_next([=](bool) {
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
		_snapAnimation.stop();
		_marqueeStart = crl::now();
		updatePosition();
		show();
		raise();
	} else {
		_down = false;
		_pressedPart = Part::None;
		_hoveredPart = Part::None;
		_drag = false;
		_seeking = false;
		_volumeDragging = false;
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

void MusicIsland::updateExpanded() {
	const auto mode = AyuSettings::getInstance().musicIslandExpandMode();
	const auto expand = _seeking
		|| _volumeDragging
		|| _forceExpanded
		|| ((mode == 0 || mode == 2) && _hovered);
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
	auto position = cornerPosition(outer);
	if (_snapAnimation.animating()) {
		const auto progress = _snapAnimation.value(1.);
		position = QPoint(
			anim::interpolate(_dragFrom.x(), position.x(), progress),
			anim::interpolate(_dragFrom.y(), position.y(), progress));
	}
	setGeometry(QRect(position, outer));

	const auto global = QCursor::pos();
	const auto local = mapFromGlobal(global);
	const auto inside = rect().contains(local);
	if (_hovered != inside) {
		_hovered = inside;
		updateExpanded();
	}

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

void MusicIsland::snapToCorner() {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	const auto center = geometry().center();
	const auto left = (center.x() * 2 < parent->width());
	const auto top = (center.y() * 2 < parent->height());
	const auto position = (top ? 0 : 2) + (left ? 0 : 1);

	_dragFrom = pos();
	_snapAnimation.stop();
	_snapAnimation.start([=] {
		updatePosition();
	}, 0., 1., kSnapDuration, anim::easeOutCubic);

	AyuSettings::getInstance().setMusicIslandPosition(position);
	updatePosition();
}

float64 MusicIsland::expandProgress() const {
	return _expandAnimation.value(_expanded ? 1. : 0.);
}

float64 MusicIsland::shownProgress() const {
	return _shownAnimation.value(_shown ? 1. : 0.);
}

QSize MusicIsland::islandSize(float64 expand) const {
	const auto collapsed = CollapsedSize();
	const auto expanded = ExpandedSize();
	return QSize(
		anim::interpolate(collapsed.width(), expanded.width(), expand),
		anim::interpolate(collapsed.height(), expanded.height(), expand));
}

QPoint MusicIsland::cornerPosition(QSize outer) const {
	const auto parent = parentWidget();
	if (!parent) {
		return QPoint();
	}
	const auto margin = st::musicIslandScreenMargin;
	const auto shadow = st::musicIslandShadow;
	const auto position = std::clamp(
		AyuSettings::getInstance().musicIslandPosition(),
		0,
		3);
	const auto left = (position == 0) || (position == 2);
	const auto top = (position < 2);
	return QPoint(
		left
			? (margin - shadow)
			: (parent->width() - outer.width() - margin + shadow),
		top
			? (margin - shadow)
			: (parent->height() - outer.height() - margin + shadow));
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
	result.close = QRectF(
		islandWidth - st::musicIslandCloseSkip - st::musicIslandCloseSize,
		headerButtonY,
		st::musicIslandCloseSize,
		st::musicIslandCloseSize).translated(origin);
	if (canSaveToProfile()) {
		result.saveProfile = QRectF(
			islandWidth
				- st::musicIslandCloseSkip
				- st::musicIslandCloseSize
				- st::musicIslandSaveSkip
				- st::musicIslandSaveSize,
			headerButtonY,
			st::musicIslandSaveSize,
			st::musicIslandSaveSize).translated(origin);
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
	const auto timesTop = islandHeight
		- st::musicIslandExpandedPaddingBottom
		- timeHeight;
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
	const auto expandedTextWidth = islandWidth
		- padding
		- textLocalX
		- (canSaveToProfile()
			? (st::musicIslandSaveSize + st::musicIslandSaveSkip)
			: 0)
		- st::musicIslandCloseSize
		- st::musicIslandCloseSkip;
	result.textWidth = std::max(0, int(base::SafeRound(
		lerp(collapsedTextWidth, expandedTextWidth))));

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

void MusicIsland::activate(Part part) {
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
		const auto expanded = expandProgress() > 0.5;
		const auto clickExpands = (mode == 1 || mode == 2);

		if (clickExpands && expanded && _forceExpanded) {
			_forceExpanded = false;
			updateExpanded();
		} else if (clickExpands && !expanded) {
			_forceExpanded = true;
			updateExpanded();
		} else if (expanded && settings.musicIslandClickOpensTrack()) {
			showTrackMessage();
		}
	} break;
	default:
		break;
	}
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
		auto gradient = QLinearGradient(
			layout.island.topLeft(),
			layout.island.bottomRight());
		gradient.setColorAt(0., AdjustLightness(_cover.accent, 1.35));
		gradient.setColorAt(1., AdjustLightness(_cover.accent, 0.7));
		p.fillRect(layout.island, gradient);

		if (!_cover.blurred.isNull()
			&& AyuSettings::getInstance().musicIslandBlurCover()) {
			p.setOpacity(opacity * 0.5);
			DrawToFill(p, layout.island, _cover.blurred);
			p.setOpacity(opacity);
		}

		auto highlight = QLinearGradient(
			layout.island.topLeft(),
			layout.island.bottomLeft());
		highlight.setColorAt(0., QColor(255, 255, 255, 32));
		highlight.setColorAt(1., QColor(255, 255, 255, 0));
		p.fillRect(layout.island, highlight);
	} else {
		p.fillRect(layout.island, st::windowBg);
		auto highlight = QLinearGradient(
			layout.island.topLeft(),
			layout.island.bottomLeft());
		auto top = st::windowBgOver->c;
		top.setAlpha(160);
		highlight.setColorAt(0., top);
		highlight.setColorAt(1., QColor(0, 0, 0, 0));
		p.fillRect(layout.island, highlight);
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
	if (_pressedPart == Part::Seek) {
		_seeking = true;
		_seekProgress = seekProgressFor(e->pos());
		_playedText = Ui::FormatDurationText(
			int64(base::SafeRound(_seekProgress * _durationSeconds)));
		Media::Player::instance()->startSeeking(AudioMsgId::Type::Song);
	} else if (_pressedPart == Part::Volume) {
		_volumeDragging = true;
		_volumeDragProgress = volumeProgressFor(e->pos());
		updateExpanded();
	}
	update();
}

void MusicIsland::mouseMoveEvent(QMouseEvent *e) {
	if (_volumeDragging) {
		_volumeDragProgress = volumeProgressFor(e->pos());
		update();
		return;
	} else if (_seeking) {
		_seekProgress = seekProgressFor(e->pos());
		_playedText = Ui::FormatDurationText(
			int64(base::SafeRound(_seekProgress * _durationSeconds)));
		update();
		return;
	} else if (_drag) {
		auto target = pos() + (e->pos() - _dragLocalPoint);
		if (const auto parent = parentWidget()) {
			const auto margin = st::musicIslandScreenMargin
				- st::musicIslandShadow;
			target.setX(std::clamp(
				target.x(),
				margin,
				parent->width() - width() - margin));
			target.setY(std::clamp(
				target.y(),
				margin,
				parent->height() - height() - margin));
		}
		move(target);
		return;
	} else if (_down
		&& ((_pressedPart == Part::Body) || (_pressedPart == Part::Cover))
		&& ((e->pos() - _downPoint).manhattanLength()
			> QApplication::startDragDistance())) {
		_drag = true;
		_dragLocalPoint = e->pos();
		_snapAnimation.stop();
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
	const auto hovered = (part != Part::None);
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
		applySongVolume(_volumeDragProgress);
		updateExpanded();
		update();
		return;
	} else if (_seeking) {
		_seeking = false;
		Media::Player::instance()->finishSeeking(
			AudioMsgId::Type::Song,
			_seekProgress);
		updateExpanded();
		update();
		return;
	} else if (_drag) {
		_drag = false;
		snapToCorner();
		updateHovered(mapFromGlobal(QCursor::pos()));
		return;
	}
	if (was && (part != Part::None) && (partAt(e->pos()) == part)) {
		activate(part);
	}
	update();
}

void MusicIsland::enterEventHook(QEnterEvent *e) {
	updateHovered(e->pos());
	return RpWidget::enterEventHook(e);
}

void MusicIsland::leaveEventHook(QEvent *e) {
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
	if (_hovered) {
		_hovered = false;
		updateExpanded();
	}
	return RpWidget::leaveEventHook(e);
}

} // namespace AyuUi
