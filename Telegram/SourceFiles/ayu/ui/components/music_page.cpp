/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/ui/components/music_page.h"

#include "ayu/ui/components/lyrics_window.h"
#include "ayu/ui/components/music_island.h"
#include "base/event_filter.h"
#include "base/random.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/boxes/confirm_box.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtGui/QDesktopServices>
#include <QtGui/QPainterPath>
#include <QtWidgets/QLineEdit>

#include <array>

namespace AyuUi {
namespace {

using namespace Whitegram::Music;

constexpr auto kSearchDelay = crl::time(450);
constexpr auto kShowDuration = crl::time(220);
constexpr auto kSectionDuration = crl::time(180);
constexpr auto kScrollDuration = crl::time(160);
constexpr auto kHomeRows = 8;
constexpr auto kHomeCards = 4;

const auto kNavLabels = std::array<const char*, 4>{
	"Главная",
	"Поиск",
	"Библиотека",
	"Плейлисты",
};
const auto kChipLabels = std::array<const char*, 5>{
	"Все",
	"SoundCloud",
	"Audius",
	"Jamendo",
	"Telegram",
};

[[nodiscard]] double Unit() {
	return style::Scale() / 100.;
}

[[nodiscard]] QColor White(int alpha) {
	return QColor(255, 255, 255, alpha);
}

[[nodiscard]] QString Utf(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QColor SourceColor(const QString &source) {
	if (source == u"soundcloud"_q) {
		return QColor(255, 85, 0);
	} else if (source == u"audius"_q) {
		return QColor(204, 15, 224);
	} else if (source == u"jamendo"_q) {
		return QColor(233, 37, 95);
	}
	return QColor(42, 171, 238);
}

[[nodiscard]] QString SourceLabel(const QString &source) {
	if (source == u"soundcloud"_q) {
		return u"SoundCloud"_q;
	} else if (source == u"audius"_q) {
		return u"Audius"_q;
	} else if (source == u"jamendo"_q) {
		return u"Jamendo"_q;
	}
	return u"Telegram"_q;
}

void OpenLink(const QString &link) {
	const auto url = QUrl(link);
	if (url.isValid() && url.scheme() == u"https"_q) {
		QDesktopServices::openUrl(url);
	}
}

[[nodiscard]] QString Duration(int64 ms) {
	return (ms > 0) ? Ui::FormatDurationText(ms / 1000) : QString();
}

[[nodiscard]] QString Subtitle(const Track &track) {
	if (track.soundcloud() || track.source == u"audius"_q) {
		if (track.uploader.isEmpty() || track.uploader == track.artist) {
			return track.artist;
		}
		return track.artist + Utf(" · ") + track.uploader;
	}
	return track.artist;
}

[[nodiscard]] QString TracksCount(int count) {
	const auto mod10 = count % 10;
	const auto mod100 = count % 100;
	const auto word = (mod10 == 1 && mod100 != 11)
		? "трек"
		: (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
		? "трека"
		: "треков";
	return QString::number(count) + u' ' + Utf(word);
}

[[nodiscard]] std::vector<Track> Merge(
		const std::vector<Track> &a,
		const std::vector<Track> &b) {
	auto result = std::vector<Track>();
	result.reserve(a.size() + b.size());
	for (auto i = size_t(); i < std::max(a.size(), b.size()); ++i) {
		if (i < a.size()) {
			result.push_back(a[i]);
		}
		if (i < b.size()) {
			result.push_back(b[i]);
		}
	}
	return result;
}

void PaintPlay(QPainter &p, QRectF rect, QColor color) {
	const auto side = rect.width();
	auto path = QPainterPath();
	path.moveTo(rect.x() + side * 0.34, rect.y() + side * 0.24);
	path.lineTo(rect.x() + side * 0.80, rect.y() + side * 0.50);
	path.lineTo(rect.x() + side * 0.34, rect.y() + side * 0.76);
	path.closeSubpath();
	p.fillPath(path, color);
}

void PaintPause(QPainter &p, QRectF rect, QColor color) {
	const auto side = rect.width();
	const auto bar = side * 0.13;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(
		QRectF(rect.x() + side * 0.30, rect.y() + side * 0.26, bar, side * 0.48),
		bar / 3,
		bar / 3);
	p.drawRoundedRect(
		QRectF(rect.x() + side * 0.57, rect.y() + side * 0.26, bar, side * 0.48),
		bar / 3,
		bar / 3);
}

void PaintSkip(QPainter &p, QRectF rect, QColor color, bool forward) {
	const auto side = rect.width();
	auto path = QPainterPath();
	const auto x = [&](double v) {
		return rect.x() + side * (forward ? v : (1. - v));
	};
	path.moveTo(x(0.28), rect.y() + side * 0.28);
	path.lineTo(x(0.62), rect.y() + side * 0.50);
	path.lineTo(x(0.28), rect.y() + side * 0.72);
	path.closeSubpath();
	p.fillPath(path, color);
	p.fillRect(
		QRectF(
			forward ? x(0.64) : x(0.72),
			rect.y() + side * 0.28,
			side * 0.08,
			side * 0.44),
		color);
}

void PaintCloud(QPainter &p, QRectF rect, QColor color) {
	auto path = QPainterPath();
	const auto w = rect.width();
	const auto h = rect.height();
	path.addEllipse(QRectF(rect.x() + w * 0.42, rect.y(), w * 0.46, h * 0.86));
	path.addEllipse(QRectF(rect.x() + w * 0.22, rect.y() + h * 0.30, w * 0.36, h * 0.56));
	path.addRoundedRect(
		QRectF(rect.x() + w * 0.30, rect.y() + h * 0.40, w * 0.70, h * 0.60),
		h * 0.3,
		h * 0.3);
	p.fillPath(path.simplified(), color);
	for (auto i = 0; i != 3; ++i) {
		const auto bar = w * 0.05;
		p.fillRect(
			QRectF(
				rect.x() + i * w * 0.08,
				rect.y() + h * (0.55 - i * 0.12),
				bar,
				h * (0.45 + i * 0.12)),
			color);
	}
}

} // namespace

MusicPage::MusicPage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _session(&controller->session())
, _searchTimer([=] { runSearch(); }) {
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setGeometry(parent->rect());
	base::install_event_filter(this, parent, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Resize) {
			setGeometry(parentWidget()->rect());
		}
		return base::EventFilterResult::Continue;
	});

	_searchField = Ui::CreateChild<QLineEdit>(this);
	_searchField->setFrame(false);
	_searchField->setAttribute(Qt::WA_MacShowFocusRect, false);
	_searchField->setFont(font(15.));
	_searchField->setPlaceholderText(Utf("Трек, исполнитель или альбом"));
	_searchField->setStyleSheet(u"QLineEdit { background: transparent;"
		" border: none; color: rgba(255, 255, 255, 235);"
		" selection-background-color: rgba(255, 255, 255, 60); }"_q);
	auto palette = _searchField->palette();
	palette.setColor(QPalette::PlaceholderText, White(110));
	_searchField->setPalette(palette);
	_searchField->hide();
	connect(_searchField, &QLineEdit::textChanged, [=] {
		_searchTimer.callOnce(kSearchDelay);
	});
	connect(_searchField, &QLineEdit::returnPressed, [=] {
		_searchTimer.cancel();
		runSearch();
	});

	_saved = SavedTracks(_session);
	SavedTracksChanges(_session) | rpl::on_next([=] {
		_saved = SavedTracks(_session);
		update();
	}, lifetime());

	Library::Instance().load(_session);
	rpl::merge(
		Library::Instance().changes(),
		Queue::Instance().changes(),
		CoverLoaded() | rpl::to_empty,
		_session->downloaderTaskFinished()
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	const auto shownError = std::make_shared<QString>();
	Queue::Instance().changes() | rpl::on_next([=] {
		const auto error = Queue::Instance().lastError();
		if (!error.isEmpty() && error != *shownError) {
			Ui::Toast::Show(this, error);
		}
		*shownError = error;
	}, lifetime());

	Media::Player::instance()->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &) {
		if (stripHeight() > 0.) {
			update(0, height() - int(stripHeight()), width(), int(stripHeight()));
		}
	}, lifetime());

	_spinner.init([=] {
		if (!_searchPending && !Queue::Instance().preparing()) {
			_spinner.stop();
		}
		update();
	});
}

MusicPage::~MusicPage() = default;

void MusicPage::Show(not_null<Window::SessionController*> controller) {
	const auto host = controller->content();
	auto page = host->findChild<MusicPage*>(
		QString(),
		Qt::FindDirectChildrenOnly);
	if (!page) {
		page = Ui::CreateChild<MusicPage>(host.get(), controller);
	}
	page->appear();
}

void MusicPage::appear() {
	_shown = true;
	setGeometry(parentWidget()->rect());
	show();
	raise();
	raiseIsland();
	setFocus();
	updateSearchField();
	_showAnimation.start(
		[=] { update(); },
		0.,
		1.,
		kShowDuration,
		anim::easeOutCubic);
}

void MusicPage::disappear() {
	if (!_shown) {
		return;
	}
	_shown = false;
	_searchField->hide();
	_menu = nullptr;
	_showAnimation.start([=] {
		update();
		if (!_showAnimation.animating()) {
			hide();
			deleteLater();
		}
	}, 1., 0., kShowDuration, anim::easeOutCubic);
}

void MusicPage::raiseIsland() {
	for (const auto island : parentWidget()->findChildren<MusicIsland*>(
			QString(),
			Qt::FindDirectChildrenOnly)) {
		island->raise();
	}
}

void MusicPage::setSection(Section section, const QString &playlistId) {
	if (_section == section && _playlistId == playlistId) {
		return;
	}
	_section = section;
	_playlistId = playlistId;
	_hover = Hit();
	scrollTo(0., false);
	if (section == Section::Playlists || section == Section::Playlist) {
		Library::Instance().load(_session);
	}
	updateSearchField();
	if (section == Section::Search) {
		_searchField->setFocus();
	} else {
		setFocus();
	}
	_sectionAnimation.start(
		[=] { update(); },
		0.,
		1.,
		kSectionDuration,
		anim::easeOutCubic);
}

void MusicPage::updateSearchField() {
	const auto visible = _shown && (_section == Section::Search);
	if (visible) {
		const auto capsule = searchCapsule();
		const auto u = Unit();
		_searchField->setGeometry(QRect(
			int(capsule.x() + 48 * u),
			int(capsule.y()),
			int(capsule.width() - 64 * u),
			int(capsule.height())));
	}
	_searchField->setVisible(visible);
}

void MusicPage::runSearch() {
	const auto query = _searchField->text().trimmed();
	_query = query;
	const auto generation = ++_searchGeneration;
	_externalResults.clear();
	_telegramResults.clear();
	_searchError = QString();
	_searchPending = 0;
	scrollTo(0., false);
	if (query.isEmpty()) {
		update();
		return;
	}
	const auto external = (_source != Source::Telegram);
	const auto telegram = (_source == Source::All)
		|| (_source == Source::Telegram);
	if (external) {
		++_searchPending;
		Search(_session, query, _source, crl::guard(this, [=](
				SearchResult result) {
			applySearch(generation, false, std::move(result));
		}));
	}
	if (telegram) {
		++_searchPending;
		Search(_session, query, Source::Telegram, crl::guard(this, [=](
				SearchResult result) {
			applySearch(generation, true, std::move(result));
		}));
	}
	if (!_spinner.animating()) {
		_spinner.start();
	}
	update();
}

void MusicPage::applySearch(
		int generation,
		bool telegram,
		SearchResult result) {
	if (generation != _searchGeneration) {
		return;
	}
	--_searchPending;
	(telegram ? _telegramResults : _externalResults) = std::move(result.tracks);
	if (!result.error.isEmpty() && (!telegram || _source == Source::Telegram)) {
		_searchError = result.error;
	}
	update();
}

const Playlist *MusicPage::openedPlaylist() const {
	return (_section == Section::Playlist)
		? Library::Instance().find(_playlistId)
		: nullptr;
}

std::vector<Track> MusicPage::visibleTracks() const {
	switch (_section) {
	case Section::Search:
		return Merge(_externalResults, _telegramResults);
	case Section::Home:
		return (int(_saved.size()) > kHomeRows)
			? std::vector<Track>(begin(_saved), begin(_saved) + kHomeRows)
			: _saved;
	case Section::Library:
		return _saved;
	case Section::Playlist:
		if (const auto playlist = openedPlaylist()) {
			return playlist->tracks;
		}
		return {};
	default:
		return {};
	}
}

std::optional<Track> MusicPage::nowPlaying() const {
	const auto &queue = Queue::Instance();
	if (const auto preparing = queue.preparing()) {
		return *preparing;
	} else if (const auto current = queue.current()) {
		return *current;
	}
	const auto id = Media::Player::instance()->current(AudioMsgId::Type::Song);
	const auto document = id.audio();
	if (!document) {
		return std::nullopt;
	}
	const auto state = Media::Player::instance()->getState(
		AudioMsgId::Type::Song);
	if (Media::Player::IsStoppedOrStopping(state.state)) {
		return std::nullopt;
	}
	if (const auto external = ExternalTrack(document)) {
		return *external;
	}
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	auto result = Track();
	result.source = u"telegram"_q;
	result.id = QString::number(document->id);
	result.title = name.title;
	result.artist = name.performer;
	result.durationMs = document->duration();
	result.document = document;
	result.context = id.contextId();
	return result;
}

void MusicPage::playFrom(int index, bool shuffle) {
	auto tracks = visibleTracks();
	if (index < 0 || index >= int(tracks.size())) {
		return;
	}
	Queue::Instance().play(_session, std::move(tracks), index, shuffle);
	if (!_spinner.animating()) {
		_spinner.start();
	}
	raiseIsland();
}

double MusicPage::railWidth() const {
	return 232 * Unit();
}

double MusicPage::stripHeight() const {
	return nowPlaying() ? 96 * Unit() : 0.;
}

QRectF MusicPage::searchCapsule() const {
	const auto u = Unit();
	const auto left = railWidth() + 40 * u;
	const auto w = std::min(width() - left - 40 * u, 640 * u);
	return QRectF(left, 104 * u, w, 48 * u);
}

double MusicPage::scrollValue() const {
	return _scrollAnimation.value(_scroll);
}

void MusicPage::scrollTo(double value, bool animated) {
	value = std::clamp(value, 0., _scrollMax);
	const auto from = scrollValue();
	_scroll = value;
	if (animated) {
		_scrollAnimation.start(
			[=] { update(); },
			from,
			value,
			kScrollDuration,
			anim::easeOutCubic);
	} else {
		_scrollAnimation.stop();
		update();
	}
}

QFont MusicPage::font(double size, int weight) const {
	const auto pixel = std::max(8, int(std::round(size * Unit())));
	const auto key = (pixel << 10) | (weight & 0x3FF);
	if (const auto i = _fonts.find(key); i != end(_fonts)) {
		return i->second;
	}
	auto result = st::normalFont->f;
	result.setFamilies({
		u"SF Pro Display"_q,
		u"Segoe UI"_q,
		u"Helvetica Neue"_q,
		result.family(),
	});
	result.setPixelSize(pixel);
	result.setWeight(QFont::Weight(weight));
	_fonts.emplace(key, result);
	return result;
}

QImage MusicPage::coverImage(const Track &track) {
	if (!track.telegram()) {
		return Cover(track.artworkUrl);
	}
	const auto document = track.document;
	if (!document || !document->isSongWithCover()) {
		return QImage();
	}
	auto &view = _media[document];
	if (!view) {
		view = document->createMediaView();
		view->thumbnailWanted(Data::FileOrigin(track.context));
	}
	const auto image = view->thumbnail();
	return image ? image->original() : QImage();
}

QPixmap MusicPage::roundedCover(const Track &track, int size, double radius) {
	const auto key = track.key() + u'@' + QString::number(size);
	if (const auto i = _covers.find(key); i != end(_covers)) {
		return i->second;
	}
	const auto image = coverImage(track);
	if (image.isNull()) {
		return QPixmap();
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		auto path = QPainterPath();
		path.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
		p.setClipPath(path);
		const auto scaled = image.scaled(
			QSize(size, size) * ratio,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		p.drawImage(
			QRectF(0, 0, size, size),
			scaled,
			QRectF(
				(scaled.width() - size * ratio) / 2.,
				(scaled.height() - size * ratio) / 2.,
				size * ratio,
				size * ratio));
	}
	auto pixmap = QPixmap::fromImage(std::move(result));
	if (_covers.size() > 600) {
		_covers.clear();
	}
	_covers.emplace(key, pixmap);
	return pixmap;
}

std::pair<QColor, QColor> MusicPage::toneFor(const Track &track) {
	const auto key = track.key();
	if (const auto i = _tones.find(key); i != end(_tones)) {
		return i->second;
	}
	const auto image = coverImage(track);
	if (image.isNull()) {
		return {};
	}
	const auto result = CoverTones(image);
	_tones.emplace(key, result);
	return result;
}

void MusicPage::addHit(QRectF rect, Action action, int index) {
	const auto clipped = (action == Action::Close
		|| action == Action::Nav
		|| action == Action::NowPrevious
		|| action == Action::NowPlay
		|| action == Action::NowNext
		|| action == Action::NowLink)
		? rect
		: rect.intersected(_clip);
	if (!clipped.isEmpty()) {
		_hits.push_back({ clipped, action, index });
	}
}

MusicPage::Hit MusicPage::hitAt(QPointF point) const {
	for (auto i = _hits.rbegin(); i != _hits.rend(); ++i) {
		if (i->rect.contains(point)) {
			return *i;
		}
	}
	return Hit();
}

void MusicPage::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	_hits.clear();
	_paintedTracks = visibleTracks();
	const auto shown = _showAnimation.value(_shown ? 1. : 0.);
	const auto u = Unit();
	p.setOpacity(shown);
	paintBackground(p);
	paintRail(p);

	const auto strip = stripHeight();
	_clip = QRectF(railWidth(), 0, width() - railWidth(), height() - strip);
	const auto section = _sectionAnimation.value(1.);
	p.save();
	p.setClipRect(_clip);
	p.setOpacity(shown * section);
	p.translate(0, (1. - section) * 10 * u);
	const auto top = 36 * u;
	auto bottom = top;
	auto scrollTop = top;
	switch (_section) {
	case Section::Home: bottom = paintHome(p, top - scrollValue()); break;
	case Section::Search:
		scrollTop = searchCapsule().bottom() + 64 * u;
		bottom = paintSearch(p, top);
		break;
	case Section::Library: bottom = paintLibrary(p, top - scrollValue()); break;
	case Section::Playlists:
		bottom = paintPlaylists(
			p,
			paintTitle(
				p,
				top - scrollValue(),
				Utf("Плейлисты"),
				Utf("Сохраняются в вашем аккаунте Whitegram")),
			-1);
		break;
	case Section::Playlist: bottom = paintPlaylist(p, top - scrollValue()); break;
	}
	p.restore();
	const auto content = bottom + scrollValue() - scrollTop;
	const auto viewport = _clip.height() - scrollTop;
	_scrollMax = std::max(0., content - viewport + 32 * u);
	if (_scroll > _scrollMax) {
		_scroll = _scrollMax;
	}

	paintNowPlaying(p);
	paintClose(p);
	if (_hover.action != Action::None) {
		const auto again = hitAt(mapFromGlobal(QCursor::pos()));
		if (again.action != _hover.action || again.index != _hover.index) {
			_hover = again;
			setCursor(_hover.action != Action::None
				? style::cur_pointer
				: style::cur_default);
		}
	}
}

void MusicPage::paintBackground(QPainter &p) {
	p.fillRect(rect(), QColor(12, 12, 16));
	auto tone = std::pair<QColor, QColor>();
	if (const auto playlist = openedPlaylist(); playlist && !playlist->tracks.empty()) {
		for (const auto &track : playlist->tracks) {
			tone = toneFor(track);
			if (tone.first.isValid()) {
				break;
			}
		}
	}
	if (!tone.first.isValid()) {
		if (const auto now = nowPlaying()) {
			tone = toneFor(*now);
		}
	}
	const auto main = tone.first.isValid() ? tone.first : QColor(96, 72, 180);
	const auto second = tone.second.isValid() ? tone.second : QColor(40, 90, 170);
	const auto paintGlow = [&](QPointF center, double radius, QColor color, int alpha) {
		auto gradient = QRadialGradient(center, radius);
		color.setAlpha(alpha);
		gradient.setColorAt(0., color);
		color.setAlpha(0);
		gradient.setColorAt(1., color);
		p.fillRect(rect(), gradient);
	};
	const auto big = std::max(width(), height());
	paintGlow(QPointF(width() * 0.32, -height() * 0.1), big * 0.75, main, 92);
	paintGlow(QPointF(width() * 1.05, height() * 0.95), big * 0.6, second, 54);
}

void MusicPage::paintRail(QPainter &p) {
	const auto u = Unit();
	const auto rail = QRectF(0, 0, railWidth(), height());
	p.fillRect(rail, QColor(0, 0, 0, 70));
	p.fillRect(QRectF(rail.right() - u, 0, u, height()), White(14));

	p.setPen(White(245));
	p.setFont(font(24., QFont::Bold));
	p.drawText(QRectF(28 * u, 28 * u, rail.width(), 36 * u), Utf("Музыка"));

	const auto active = (_section == Section::Playlist)
		? int(Section::Playlists)
		: int(_section);
	for (auto i = 0; i != int(kNavLabels.size()); ++i) {
		const auto item = QRectF(14 * u, 88 * u + i * 48 * u, rail.width() - 28 * u, 42 * u);
		const auto hovered = (_hover.action == Action::Nav && _hover.index == i);
		if (i == active || hovered) {
			p.setPen(Qt::NoPen);
			p.setBrush(White(i == active ? 26 : 12));
			p.drawRoundedRect(item, 12 * u, 12 * u);
		}
		const auto color = White(i == active ? 250 : 170);
		paintNavIcon(
			p,
			i,
			QRectF(item.x() + 14 * u, item.y() + 11 * u, 20 * u, 20 * u),
			color);
		p.setPen(color);
		p.setFont(font(14.5, i == active ? QFont::DemiBold : QFont::Normal));
		p.drawText(
			item.adjusted(48 * u, 0, 0, 0),
			Qt::AlignVCenter | Qt::AlignLeft,
			Utf(kNavLabels[i]));
		addHit(item, Action::Nav, i);
	}

	p.setPen(White(90));
	p.setFont(font(11.));
	p.drawText(
		QRectF(28 * u, height() - stripHeight() - 64 * u, rail.width() - 40 * u, 48 * u),
		Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap,
		Utf("SoundCloud, Audius, Jamendo и Telegram"));
}

void MusicPage::paintNavIcon(QPainter &p, int index, QRectF rect, QColor color) {
	const auto u = Unit();
	auto pen = QPen(color, 1.8 * u);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto x = rect.x();
	const auto y = rect.y();
	const auto s = rect.width();
	switch (index) {
	case 0: {
		auto path = QPainterPath();
		path.moveTo(x + s * 0.12, y + s * 0.48);
		path.lineTo(x + s * 0.5, y + s * 0.14);
		path.lineTo(x + s * 0.88, y + s * 0.48);
		path.moveTo(x + s * 0.24, y + s * 0.40);
		path.lineTo(x + s * 0.24, y + s * 0.86);
		path.lineTo(x + s * 0.76, y + s * 0.86);
		path.lineTo(x + s * 0.76, y + s * 0.40);
		p.drawPath(path);
	} break;
	case 1:
		p.drawEllipse(QRectF(x + s * 0.12, y + s * 0.12, s * 0.56, s * 0.56));
		p.drawLine(QPointF(x + s * 0.62, y + s * 0.62), QPointF(x + s * 0.88, y + s * 0.88));
		break;
	case 2:
		p.drawLine(QPointF(x + s * 0.2, y + s * 0.14), QPointF(x + s * 0.2, y + s * 0.86));
		p.drawLine(QPointF(x + s * 0.42, y + s * 0.14), QPointF(x + s * 0.42, y + s * 0.86));
		p.drawLine(QPointF(x + s * 0.62, y + s * 0.18), QPointF(x + s * 0.84, y + s * 0.84));
		break;
	default:
		p.drawLine(QPointF(x + s * 0.12, y + s * 0.24), QPointF(x + s * 0.7, y + s * 0.24));
		p.drawLine(QPointF(x + s * 0.12, y + s * 0.46), QPointF(x + s * 0.7, y + s * 0.46));
		p.drawLine(QPointF(x + s * 0.12, y + s * 0.68), QPointF(x + s * 0.44, y + s * 0.68));
		p.drawEllipse(QRectF(x + s * 0.56, y + s * 0.60, s * 0.22, s * 0.22));
		p.drawLine(QPointF(x + s * 0.78, y + s * 0.70), QPointF(x + s * 0.78, y + s * 0.30));
		break;
	}
}

double MusicPage::paintTitle(
		QPainter &p,
		double top,
		const QString &title,
		const QString &subtitle) {
	const auto u = Unit();
	const auto left = railWidth() + 40 * u;
	p.setPen(White(250));
	p.setFont(font(32., QFont::Bold));
	p.drawText(QRectF(left, top, width() - left, 44 * u), Qt::AlignLeft | Qt::AlignVCenter, title);
	top += 46 * u;
	if (!subtitle.isEmpty()) {
		p.setPen(White(140));
		p.setFont(font(13.5));
		p.drawText(QRectF(left, top, width() - left, 22 * u), Qt::AlignLeft | Qt::AlignVCenter, subtitle);
		top += 28 * u;
	}
	return top + 18 * u;
}

void MusicPage::paintHint(QPainter &p, double top, const QString &text) {
	const auto u = Unit();
	const auto left = railWidth() + 40 * u;
	const auto box = QRectF(left, top, width() - left - 40 * u, 72 * u);
	p.setPen(Qt::NoPen);
	p.setBrush(White(10));
	p.drawRoundedRect(box, 16 * u, 16 * u);
	p.setPen(White(150));
	p.setFont(font(13.5));
	p.drawText(box.adjusted(20 * u, 0, -20 * u, 0), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text);
}

double MusicPage::paintHome(QPainter &p, double top) {
	const auto u = Unit();
	top = paintTitle(
		p,
		top,
		Utf("Главная"),
		Utf("Ищите треки, собирайте плейлисты и слушайте с текстом в караоке"));
	const auto left = railWidth() + 40 * u;
	const auto heading = [&](const QString &text) {
		p.setPen(White(235));
		p.setFont(font(19., QFont::Bold));
		p.drawText(QRectF(left, top, width() - left, 28 * u), Qt::AlignLeft | Qt::AlignVCenter, text);
		top += 40 * u;
	};
	heading(Utf("Плейлисты"));
	top = paintPlaylists(p, top, kHomeCards) + 12 * u;
	heading(Utf("Моя музыка в Telegram"));
	if (_saved.empty()) {
		paintHint(p, top, Utf("Добавьте треки в музыку профиля Telegram, и они появятся здесь."));
		return top + 72 * u;
	}
	return paintRows(p, _paintedTracks, top);
}

double MusicPage::paintSearch(QPainter &p, double top) {
	const auto u = Unit();
	paintTitle(p, top, Utf("Поиск"), QString());
	const auto capsule = searchCapsule();
	p.setPen(QPen(White(_searchField->hasFocus() ? 60 : 24), u));
	p.setBrush(White(16));
	p.drawRoundedRect(capsule, capsule.height() / 2, capsule.height() / 2);
	paintNavIcon(
		p,
		1,
		QRectF(capsule.x() + 18 * u, capsule.y() + 14 * u, 20 * u, 20 * u),
		White(150));

	auto chipLeft = capsule.x();
	const auto chipTop = capsule.bottom() + 16 * u;
	for (auto i = 0; i != int(kChipLabels.size()); ++i) {
		chipLeft = paintPill(
			p,
			QPointF(chipLeft, chipTop),
			Utf(kChipLabels[i]),
			int(_source) == i,
			Action::Chip,
			i) + 8 * u;
	}

	const auto rowsTop = chipTop + 48 * u;
	const auto clip = _clip;
	_clip = QRectF(_clip.x(), rowsTop - 8 * u, _clip.width(), _clip.bottom() - rowsTop + 8 * u);
	p.save();
	p.setClipRect(_clip);
	const auto tracks = _paintedTracks;
	auto bottom = rowsTop;
	if (_query.isEmpty()) {
		paintHint(p, rowsTop, Utf("Введите название трека или имя исполнителя."));
		bottom += 72 * u;
	} else if (tracks.empty() && _searchPending > 0) {
		paintSpinner(p, QRectF(capsule.x(), rowsTop + 8 * u, 28 * u, 28 * u), White(200));
		p.setPen(White(150));
		p.setFont(font(13.5));
		p.drawText(QRectF(capsule.x() + 40 * u, rowsTop + 8 * u, 400 * u, 28 * u), Qt::AlignVCenter, Utf("Ищем…"));
		bottom += 44 * u;
	} else if (tracks.empty()) {
		paintHint(p, rowsTop, _searchError.isEmpty()
			? Utf("Ничего не нашлось. Попробуйте другой запрос или источник.")
			: (Utf("Поиск не удался: ") + _searchError));
		if (!_searchError.isEmpty()) {
			paintPill(p, QPointF(capsule.x(), rowsTop + 84 * u), Utf("Повторить"), true, Action::Retry);
			bottom += 48 * u;
		}
		bottom += 72 * u;
	} else {
		bottom = paintRows(p, tracks, rowsTop - scrollValue());
	}
	p.restore();
	_clip = clip;
	return bottom;
}

double MusicPage::paintLibrary(QPainter &p, double top) {
	const auto u = Unit();
	top = paintTitle(
		p,
		top,
		Utf("Библиотека"),
		Utf("Музыка из вашего профиля Telegram"));
	if (_saved.empty()) {
		paintHint(p, top, Utf("Здесь пусто. Сохраните трек в профиль из острова или из чата."));
		return top + 72 * u;
	}
	return paintRows(p, _paintedTracks, top);
}

double MusicPage::paintPlaylists(QPainter &p, double top, int limit) {
	const auto u = Unit();
	const auto &library = Library::Instance();
	const auto &playlists = library.playlists();
	const auto left = railWidth() + 40 * u;
	const auto available = width() - left - 40 * u;
	const auto card = 168 * u;
	const auto gap = 20 * u;
	const auto columns = std::max(1, int((available + gap) / (card + gap)));
	const auto count = (limit < 0)
		? int(playlists.size()) + 1
		: std::min(int(playlists.size()) + 1, std::min(limit, columns));
	const auto cardHeight = card + 56 * u;
	for (auto i = 0; i != count; ++i) {
		const auto column = i % columns;
		const auto row = i / columns;
		const auto x = left + column * (card + gap);
		const auto y = top + row * (cardHeight + gap);
		const auto art = QRectF(x, y, card, card);
		const auto creating = (i == 0);
		const auto index = i - 1;
		const auto hovered = creating
			? (_hover.action == Action::NewPlaylist)
			: (_hover.action == Action::Card && _hover.index == index);
		const auto lift = hovered ? 3 * u : 0.;
		const auto shifted = art.translated(0, -lift);
		if (creating) {
			p.setPen(QPen(White(hovered ? 90 : 40), 1.5 * u, Qt::DashLine));
			p.setBrush(White(hovered ? 16 : 8));
			p.drawRoundedRect(shifted, 18 * u, 18 * u);
			p.setPen(QPen(White(200), 2.4 * u, Qt::SolidLine, Qt::RoundCap));
			const auto c = shifted.center();
			p.drawLine(QPointF(c.x() - 14 * u, c.y()), QPointF(c.x() + 14 * u, c.y()));
			p.drawLine(QPointF(c.x(), c.y() - 14 * u), QPointF(c.x(), c.y() + 14 * u));
			p.setPen(White(235));
			p.setFont(font(14., QFont::DemiBold));
			p.drawText(QRectF(x, y + card + 10 * u, card, 20 * u), Qt::AlignLeft, Utf("Новый плейлист"));
			addHit(QRectF(x, y, card, cardHeight), Action::NewPlaylist);
			continue;
		}
		const auto &playlist = playlists[index];
		paintCollage(p, playlist, shifted, 18 * u);
		if (hovered) {
			p.setPen(QPen(White(70), u));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(shifted, 18 * u, 18 * u);
		}
		p.setPen(White(240));
		p.setFont(font(14., QFont::DemiBold));
		p.drawText(
			QRectF(x, y + card + 10 * u, card, 20 * u),
			Qt::AlignLeft,
			QFontMetrics(p.font()).elidedText(playlist.title, Qt::ElideRight, int(card)));
		p.setPen(White(130));
		p.setFont(font(12.));
		p.drawText(QRectF(x, y + card + 32 * u, card, 18 * u), Qt::AlignLeft, TracksCount(int(playlist.tracks.size())));
		addHit(QRectF(x, y, card, cardHeight), Action::Card, index);
	}
	auto bottom = top + ((count + columns - 1) / columns) * (cardHeight + gap);
	if (library.loading() && playlists.empty()) {
		paintSpinner(p, QRectF(left, bottom, 24 * u, 24 * u), White(180));
		bottom += 36 * u;
	} else if (!library.error().isEmpty() && limit < 0) {
		paintHint(p, bottom, Utf("Плейлисты не синхронизировались: ") + library.error());
		bottom += 84 * u;
	}
	return bottom;
}

double MusicPage::paintPlaylist(QPainter &p, double top) {
	const auto u = Unit();
	const auto left = railWidth() + 40 * u;
	const auto playlist = openedPlaylist();
	const auto back = QRectF(left - 8 * u, top - 6 * u, 140 * u, 30 * u);
	p.setPen(White(_hover.action == Action::Back ? 240 : 160));
	p.setFont(font(13.5, QFont::DemiBold));
	p.drawText(back.adjusted(8 * u, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, Utf("← Плейлисты"));
	addHit(back, Action::Back);
	top += 36 * u;
	if (!playlist) {
		paintHint(p, top, Utf("Плейлист не найден."));
		return top + 72 * u;
	}
	const auto header = QRectF(left, top, width() - left - 40 * u, 236 * u);
	auto tone = std::pair<QColor, QColor>();
	for (const auto &track : playlist->tracks) {
		tone = toneFor(track);
		if (tone.first.isValid()) {
			break;
		}
	}
	auto main = tone.first.isValid() ? tone.first : QColor(96, 72, 180);
	auto gradient = QLinearGradient(header.topLeft(), header.bottomRight());
	main.setAlpha(150);
	gradient.setColorAt(0., main);
	main.setAlpha(30);
	gradient.setColorAt(1., main);
	p.setPen(QPen(White(20), u));
	p.setBrush(gradient);
	p.drawRoundedRect(header, 24 * u, 24 * u);

	const auto art = QRectF(header.x() + 24 * u, header.y() + 24 * u, 188 * u, 188 * u);
	paintCollage(p, *playlist, art, 18 * u);
	const auto textLeft = art.right() + 28 * u;
	const auto textWidth = header.right() - textLeft - 24 * u;
	p.setPen(White(170));
	p.setFont(font(11.5, QFont::DemiBold));
	p.drawText(QRectF(textLeft, art.y() + 8 * u, textWidth, 18 * u), Qt::AlignLeft, Utf("ПЛЕЙЛИСТ"));
	p.setPen(White(250));
	p.setFont(font(34., QFont::Bold));
	p.drawText(
		QRectF(textLeft, art.y() + 30 * u, textWidth, 48 * u),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(p.font()).elidedText(playlist->title, Qt::ElideRight, int(textWidth)));
	auto total = int64();
	for (const auto &track : playlist->tracks) {
		total += track.durationMs;
	}
	p.setPen(White(180));
	p.setFont(font(13.5));
	p.drawText(
		QRectF(textLeft, art.y() + 84 * u, textWidth, 22 * u),
		Qt::AlignLeft,
		TracksCount(int(playlist->tracks.size()))
			+ (total > 0 ? (Utf(" · ") + QString::number(total / 60000) + Utf(" мин")) : QString()));
	auto x = textLeft;
	const auto buttons = art.bottom() - 40 * u;
	if (!playlist->tracks.empty()) {
		x = paintPill(p, QPointF(x, buttons), Utf("▶  Слушать"), true, Action::PlayAll) + 10 * u;
		x = paintPill(p, QPointF(x, buttons), Utf("Перемешать"), false, Action::Shuffle) + 10 * u;
	}
	x = paintPill(p, QPointF(x, buttons), Utf("Переименовать"), false, Action::Rename) + 10 * u;
	paintPill(p, QPointF(x, buttons), Utf("Удалить"), false, Action::Delete);
	top = header.bottom() + 20 * u;
	if (playlist->tracks.empty()) {
		paintHint(p, top, Utf("Пока пусто. Найдите трек в поиске и добавьте его через меню «⋯»."));
		return top + 72 * u;
	}
	return paintRows(p, _paintedTracks, top);
}

double MusicPage::paintPill(
		QPainter &p,
		QPointF topLeft,
		const QString &text,
		bool filled,
		Action action,
		int index) {
	const auto u = Unit();
	p.setFont(font(13., QFont::DemiBold));
	const auto width = QFontMetrics(p.font()).horizontalAdvance(text) + 32 * u;
	const auto rect = QRectF(topLeft, QSizeF(width, 36 * u));
	const auto hovered = (_hover.action == action && _hover.index == index);
	p.setPen(filled ? Qt::NoPen : QPen(White(hovered ? 90 : 40), u));
	p.setBrush(filled ? White(hovered ? 255 : 235) : White(hovered ? 22 : 10));
	p.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
	p.setPen(filled ? QColor(16, 16, 20) : White(230));
	p.drawText(rect, Qt::AlignCenter, text);
	addHit(rect, action, index);
	return rect.right();
}

double MusicPage::paintRows(
		QPainter &p,
		const std::vector<Track> &tracks,
		double top,
		int limit) {
	const auto u = Unit();
	const auto left = railWidth() + 28 * u;
	const auto rowHeight = 64 * u;
	const auto count = (limit < 0) ? int(tracks.size()) : std::min(limit, int(tracks.size()));
	for (auto i = 0; i != count; ++i) {
		const auto rect = QRectF(left, top + i * rowHeight, width() - left - 28 * u, rowHeight);
		if (rect.bottom() < _clip.top() || rect.top() > _clip.bottom()) {
			continue;
		}
		paintRow(p, tracks[i], i, rect);
	}
	return top + count * rowHeight;
}

void MusicPage::paintRow(QPainter &p, const Track &track, int index, QRectF rect) {
	const auto u = Unit();
	const auto rowAction = [](Action action) {
		return action == Action::Row
			|| action == Action::RowPlay
			|| action == Action::RowMore
			|| action == Action::RowLink;
	};
	const auto hovered = rowAction(_hover.action) && (_hover.index == index);
	const auto &queue = Queue::Instance();
	const auto current = queue.current();
	const auto preparing = queue.preparing();
	const auto isCurrent = current && current->key() == track.key();
	const auto isPreparing = preparing && preparing->key() == track.key();
	if (hovered || isCurrent) {
		p.setPen(Qt::NoPen);
		p.setBrush(White(hovered ? 18 : 10));
		p.drawRoundedRect(rect, 12 * u, 12 * u);
	}
	addHit(rect, Action::Row, index);

	const auto cover = QRectF(rect.x() + 8 * u, rect.y() + 8 * u, 48 * u, 48 * u);
	paintCoverBox(p, track, cover, 10 * u);
	if (isPreparing) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 120));
		p.drawRoundedRect(cover, 10 * u, 10 * u);
		paintSpinner(p, cover.marginsRemoved(QMarginsF(14 * u, 14 * u, 14 * u, 14 * u)), White(230));
	} else if (hovered && track.streamable) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 110));
		p.drawRoundedRect(cover, 10 * u, 10 * u);
		const auto playing = isCurrent
			&& Media::Player::ShowPauseIcon(
				Media::Player::instance()->getState(AudioMsgId::Type::Song).state);
		const auto icon = cover.marginsRemoved(QMarginsF(12 * u, 12 * u, 12 * u, 12 * u));
		if (playing) {
			PaintPause(p, icon, White(250));
		} else {
			PaintPlay(p, icon, White(250));
		}
		addHit(cover, Action::RowPlay, index);
	}

	const auto textLeft = cover.right() + 14 * u;
	const auto right = rect.right() - 12 * u;
	const auto more = QRectF(right - 32 * u, rect.center().y() - 16 * u, 32 * u, 32 * u);
	const auto duration = QRectF(more.x() - 60 * u, rect.y(), 52 * u, rect.height());
	p.setFont(font(11.5, QFont::DemiBold));
	const auto badgeWidth = QFontMetrics(p.font()).horizontalAdvance(SourceLabel(track.source))
		+ (track.soundcloud() ? 40 * u : 20 * u);
	const auto badge = QRectF(duration.x() - badgeWidth - 12 * u, rect.center().y() - 11 * u, badgeWidth, 22 * u);
	const auto textWidth = std::max(40., badge.x() - textLeft - 16 * u);

	const auto dim = track.streamable ? 1. : 0.45;
	p.setOpacity(p.opacity() * dim);
	p.setPen(isCurrent ? QColor(196, 176, 255) : White(242));
	p.setFont(font(14.5, QFont::DemiBold));
	p.drawText(
		QRectF(textLeft, rect.y() + 12 * u, textWidth, 20 * u),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(p.font()).elidedText(track.title, Qt::ElideRight, int(textWidth)));
	p.setPen(White(140));
	p.setFont(font(12.5));
	p.drawText(
		QRectF(textLeft, rect.y() + 34 * u, textWidth, 18 * u),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(p.font()).elidedText(Subtitle(track), Qt::ElideRight, int(textWidth)));
	p.setOpacity(p.opacity() / dim);

	paintBadge(p, track, badge, index);
	p.setPen(White(140));
	p.setFont(font(12.5));
	p.drawText(duration, Qt::AlignVCenter | Qt::AlignRight, Duration(track.durationMs));

	if (hovered) {
		const auto overMore = (_hover.action == Action::RowMore);
		p.setPen(Qt::NoPen);
		p.setBrush(White(overMore ? 30 : 14));
		p.drawEllipse(more);
		p.setBrush(White(220));
		for (auto i = -1; i <= 1; ++i) {
			p.drawEllipse(QPointF(more.center().x() + i * 6 * u, more.center().y()), 1.8 * u, 1.8 * u);
		}
		addHit(more, Action::RowMore, index);
	}
}

void MusicPage::paintBadge(QPainter &p, const Track &track, QRectF rect, int index) {
	const auto u = Unit();
	const auto color = SourceColor(track.source);
	const auto hovered = (_hover.action == Action::RowLink && _hover.index == index);
	p.setPen(Qt::NoPen);
	if (track.soundcloud()) {
		p.setBrush(hovered ? color.lighter(115) : color);
	} else {
		auto fill = color;
		fill.setAlpha(hovered ? 70 : 42);
		p.setBrush(fill);
	}
	p.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
	auto textRect = rect;
	if (track.soundcloud()) {
		PaintCloud(p, QRectF(rect.x() + 9 * u, rect.y() + 6 * u, 20 * u, 10 * u), White(255));
		textRect.setLeft(rect.x() + 30 * u);
		textRect.setRight(rect.right() - 10 * u);
	}
	p.setPen(track.soundcloud() ? White(255) : color.lighter(150));
	p.setFont(font(11.5, QFont::DemiBold));
	p.drawText(textRect, Qt::AlignCenter, SourceLabel(track.source));
	if (!track.permalink.isEmpty()) {
		addHit(rect, Action::RowLink, index);
	}
}

void MusicPage::paintCoverBox(QPainter &p, const Track &track, QRectF rect, double radius) {
	const auto size = int(std::round(rect.width()));
	const auto pixmap = roundedCover(track, size, radius);
	if (!pixmap.isNull()) {
		p.drawPixmap(rect.topLeft(), pixmap);
		return;
	}
	const auto hue = int(qHash(track.key()) % 360);
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., QColor::fromHsv(hue, 120, 150));
	gradient.setColorAt(1., QColor::fromHsv((hue + 50) % 360, 140, 80));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(rect, radius, radius);
	p.setPen(White(200));
	p.setFont(font(rect.width() / Unit() * 0.42, QFont::Bold));
	p.drawText(rect, Qt::AlignCenter, Utf("♪"));
}

void MusicPage::paintCollage(QPainter &p, const Playlist &playlist, QRectF rect, double radius) {
	auto covers = std::vector<const Track*>();
	for (const auto &track : playlist.tracks) {
		if (!coverImage(track).isNull()) {
			covers.push_back(&track);
			if (covers.size() == 4) {
				break;
			}
		}
	}
	p.save();
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.setClipPath(path, Qt::IntersectClip);
	if (covers.empty()) {
		auto fake = Track();
		fake.source = u"playlist"_q;
		fake.id = playlist.id;
		paintCoverBox(p, fake, rect, radius);
	} else if (covers.size() < 4) {
		paintCoverBox(p, *covers.front(), rect, 0.);
	} else {
		const auto half = rect.width() / 2.;
		for (auto i = 0; i != 4; ++i) {
			paintCoverBox(
				p,
				*covers[i],
				QRectF(rect.x() + (i % 2) * half, rect.y() + (i / 2) * half, half, half),
				0.);
		}
	}
	p.restore();
}

void MusicPage::paintSpinner(QPainter &p, QRectF rect, QColor color) {
	const auto u = Unit();
	const auto angle = int((crl::now() % 1000) * 360 / 1000);
	p.setPen(QPen(color, 2.4 * u, Qt::SolidLine, Qt::RoundCap));
	p.setBrush(Qt::NoBrush);
	p.drawArc(rect, -angle * 16, 260 * 16);
	if (!_spinner.animating()) {
		_spinner.start();
	}
}

void MusicPage::paintNowPlaying(QPainter &p) {
	const auto now = nowPlaying();
	if (!now) {
		return;
	}
	const auto u = Unit();
	const auto strip = stripHeight();
	const auto box = QRectF(
		railWidth() + 16 * u,
		height() - strip + 10 * u,
		width() - railWidth() - 32 * u,
		strip - 20 * u);
	p.setPen(QPen(White(22), u));
	p.setBrush(QColor(26, 26, 34, 238));
	p.drawRoundedRect(box, 20 * u, 20 * u);

	const auto &queue = Queue::Instance();
	const auto preparing = queue.preparing() != nullptr;
	const auto cover = QRectF(box.x() + 12 * u, box.y() + 12 * u, box.height() - 24 * u, box.height() - 24 * u);
	paintCoverBox(p, *now, cover, 12 * u);
	if (preparing) {
		paintSpinner(p, cover.marginsRemoved(QMarginsF(16 * u, 16 * u, 16 * u, 16 * u)), White(230));
	}

	const auto controls = 160 * u;
	const auto center = box.center().x();
	const auto textLeft = cover.right() + 14 * u;
	const auto textWidth = std::max(60., center - controls / 2 - textLeft - 16 * u);
	p.setPen(White(245));
	p.setFont(font(14.5, QFont::DemiBold));
	p.drawText(
		QRectF(textLeft, cover.y(), textWidth, 20 * u),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(p.font()).elidedText(now->title, Qt::ElideRight, int(textWidth)));
	p.setPen(White(150));
	p.setFont(font(12.5));
	p.drawText(
		QRectF(textLeft, cover.y() + 21 * u, textWidth, 18 * u),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(p.font()).elidedText(
			preparing ? Utf("Загрузка…") : now->artist,
			Qt::ElideRight,
			int(textWidth)));
	if (!now->telegram() && !now->permalink.isEmpty()) {
		const auto by = now->uploader.isEmpty() ? QString() : (Utf(" · ") + now->uploader);
		const auto text = SourceLabel(now->source) + by + Utf("  ↗");
		p.setFont(font(11.5, QFont::DemiBold));
		const auto linkWidth = std::min(
			textWidth,
			double(QFontMetrics(p.font()).horizontalAdvance(text)) + (now->soundcloud() ? 26 * u : 0.));
		const auto link = QRectF(textLeft, cover.bottom() - 16 * u, linkWidth, 16 * u);
		auto color = SourceColor(now->source);
		if (_hover.action == Action::NowLink) {
			color = color.lighter(125);
		}
		auto textRect = link;
		if (now->soundcloud()) {
			PaintCloud(p, QRectF(link.x(), link.y() + 3 * u, 20 * u, 10 * u), color);
			textRect.setLeft(link.x() + 26 * u);
		}
		p.setPen(color);
		p.drawText(
			textRect,
			Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(p.font()).elidedText(text, Qt::ElideRight, int(textRect.width())));
		addHit(link, Action::NowLink);
	}

	const auto player = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	const auto state = player->getState(type);
	const auto playing = Media::Player::ShowPauseIcon(state.state);
	const auto play = QRectF(center - 22 * u, box.center().y() - 22 * u, 44 * u, 44 * u);
	const auto prev = QRectF(play.x() - 52 * u, box.center().y() - 18 * u, 36 * u, 36 * u);
	const auto next = QRectF(play.right() + 16 * u, box.center().y() - 18 * u, 36 * u, 36 * u);
	p.setPen(Qt::NoPen);
	p.setBrush(White(_hover.action == Action::NowPlay ? 255 : 238));
	p.drawEllipse(play);
	if (playing) {
		PaintPause(p, play, QColor(16, 16, 20));
	} else {
		PaintPlay(p, play, QColor(16, 16, 20));
	}
	const auto paintSkipButton = [&](QRectF rect, Action action, bool forward) {
		if (_hover.action == action) {
			p.setPen(Qt::NoPen);
			p.setBrush(White(20));
			p.drawEllipse(rect);
		}
		PaintSkip(p, rect.marginsRemoved(QMarginsF(6 * u, 6 * u, 6 * u, 6 * u)), White(220), forward);
	};
	paintSkipButton(prev, Action::NowPrevious, false);
	paintSkipButton(next, Action::NowNext, true);
	addHit(play, Action::NowPlay);
	addHit(prev, Action::NowPrevious);
	addHit(next, Action::NowNext);

	const auto frequency = state.frequency ? state.frequency : Media::Player::kDefaultFrequency;
	const auto length = state.length / double(frequency);
	const auto position = state.position / double(frequency);
	const auto times = QRectF(next.right() + 24 * u, box.y(), box.right() - next.right() - 40 * u, box.height());
	if (length > 0 && !preparing && times.width() > 80 * u) {
		p.setPen(White(140));
		p.setFont(font(12.));
		p.drawText(
			times,
			Qt::AlignVCenter | Qt::AlignRight,
			Ui::FormatDurationText(int64(position)) + u" / "_q + Ui::FormatDurationText(int64(length)));
		const auto line = QRectF(box.x() + 20 * u, box.bottom() - 4 * u, box.width() - 40 * u, 2 * u);
		p.setPen(Qt::NoPen);
		p.setBrush(White(26));
		p.drawRoundedRect(line, u, u);
		p.setBrush(White(200));
		p.drawRoundedRect(
			QRectF(line.x(), line.y(), line.width() * std::clamp(position / length, 0., 1.), line.height()),
			u,
			u);
	}
}

void MusicPage::paintClose(QPainter &p) {
	const auto u = Unit();
	const auto close = QRectF(width() - 60 * u, 24 * u, 36 * u, 36 * u);
	p.setPen(Qt::NoPen);
	p.setBrush(White(_hover.action == Action::Close ? 34 : 16));
	p.drawEllipse(close);
	p.setPen(QPen(White(230), 2 * u, Qt::SolidLine, Qt::RoundCap));
	const auto c = close.center();
	const auto d = 6 * u;
	p.drawLine(QPointF(c.x() - d, c.y() - d), QPointF(c.x() + d, c.y() + d));
	p.drawLine(QPointF(c.x() + d, c.y() - d), QPointF(c.x() - d, c.y() + d));
	addHit(close, Action::Close);
}

void MusicPage::resizeEvent(QResizeEvent *e) {
	updateSearchField();
}

void MusicPage::wheelEvent(QWheelEvent *e) {
	const auto delta = e->angleDelta().y()
		? e->angleDelta().y()
		: e->pixelDelta().y();
	scrollTo(_scroll - delta * 0.9 * Unit(), true);
	e->accept();
}

void MusicPage::mouseMoveEvent(QMouseEvent *e) {
	const auto hit = hitAt(e->pos());
	if (hit.action != _hover.action || hit.index != _hover.index) {
		_hover = hit;
		setCursor(hit.action != Action::None ? style::cur_pointer : style::cur_default);
		update();
	}
}

void MusicPage::leaveEventHook(QEvent *e) {
	if (_hover.action != Action::None) {
		_hover = Hit();
		setCursor(style::cur_default);
		update();
	}
}

void MusicPage::mousePressEvent(QMouseEvent *e) {
	const auto hit = hitAt(e->pos());
	if (e->button() == Qt::RightButton) {
		if (hit.action == Action::Row
			|| hit.action == Action::RowPlay
			|| hit.action == Action::RowLink
			|| hit.action == Action::RowMore) {
			showTrackMenu(hit.index);
		}
		return;
	} else if (e->button() == Qt::LeftButton) {
		activate(hit);
	}
}

void MusicPage::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (_section == Section::Playlist) {
			setSection(Section::Playlists);
		} else {
			disappear();
		}
	} else if (e->matches(QKeySequence::Find)) {
		setSection(Section::Search);
	} else {
		RpWidget::keyPressEvent(e);
	}
}

void MusicPage::activate(const Hit &hit) {
	const auto player = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	const auto tracks = _paintedTracks;
	const auto valid = (hit.index >= 0 && hit.index < int(tracks.size()));
	switch (hit.action) {
	case Action::Close: disappear(); break;
	case Action::Nav: setSection(Section(hit.index)); break;
	case Action::Chip:
		if (_source != Source(hit.index)) {
			_source = Source(hit.index);
			runSearch();
		}
		break;
	case Action::Row:
	case Action::RowPlay:
		if (valid) {
			const auto current = Queue::Instance().current();
			if (current && current->key() == tracks[hit.index].key()) {
				player->playPause(type);
			} else if (tracks[hit.index].streamable) {
				playFrom(hit.index, false);
			}
		}
		break;
	case Action::RowMore: showTrackMenu(hit.index); break;
	case Action::RowLink:
		if (valid && !tracks[hit.index].permalink.isEmpty()) {
			OpenLink(tracks[hit.index].permalink);
		}
		break;
	case Action::Card: {
		const auto &playlists = Library::Instance().playlists();
		if (hit.index >= 0 && hit.index < int(playlists.size())) {
			setSection(Section::Playlist, playlists[hit.index].id);
		}
	} break;
	case Action::NewPlaylist:
		askTitle(Utf("Новый плейлист"), QString(), [=](QString title) {
			Library::Instance().create(title, crl::guard(this, [=](QString id) {
				if (!id.isEmpty()) {
					setSection(Section::Playlist, id);
				}
			}));
		});
		break;
	case Action::Back: setSection(Section::Playlists); break;
	case Action::PlayAll: playFrom(0, false); break;
	case Action::Shuffle:
		if (!tracks.empty()) {
			playFrom(base::RandomIndex(int(tracks.size())), true);
		}
		break;
	case Action::Rename:
		if (const auto playlist = openedPlaylist()) {
			const auto id = playlist->id;
			askTitle(Utf("Переименовать плейлист"), playlist->title, [=](QString title) {
				Library::Instance().rename(id, title);
			});
		}
		break;
	case Action::Delete:
		if (const auto playlist = openedPlaylist()) {
			const auto id = playlist->id;
			_controller->show(Ui::MakeConfirmBox({
				.text = Utf("Удалить плейлист «") + playlist->title + Utf("»?"),
				.confirmed = Fn<void(Fn<void()>)>([=, weak = QPointer<MusicPage>(this)](
						Fn<void()> close) {
					Library::Instance().remove(id);
					if (weak) {
						weak->setSection(Section::Playlists);
					}
					close();
				}),
				.confirmText = Utf("Удалить"),
			}));
		}
		break;
	case Action::Retry: runSearch(); break;
	case Action::NowPrevious: player->previous(type); break;
	case Action::NowPlay: player->playPause(type); break;
	case Action::NowNext: player->next(type); break;
	case Action::NowLink:
		if (const auto now = nowPlaying(); now && !now->permalink.isEmpty()) {
			OpenLink(now->permalink);
		}
		break;
	case Action::None: break;
	}
}

void MusicPage::showTrackMenu(int index) {
	const auto tracks = _paintedTracks;
	if (index < 0 || index >= int(tracks.size())) {
		return;
	}
	const auto track = tracks[index];
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	if (track.streamable) {
		_menu->addAction(Utf("Слушать"), [=] {
			playFrom(index, false);
		}, &st::menuIconSoundOn);
	}
	auto add = std::make_unique<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	for (const auto &playlist : Library::Instance().playlists()) {
		const auto id = playlist.id;
		add->addAction(playlist.title, [=] {
			Library::Instance().add(id, track);
			Ui::Toast::Show(this, Utf("Добавлено в «") + playlist.title + Utf("»"));
		});
	}
	add->addAction(Utf("Новый плейлист…"), [=] {
		askTitle(Utf("Новый плейлист"), QString(), [=](QString title) {
			Library::Instance().create(title, crl::guard(this, [=](QString id) {
				if (!id.isEmpty()) {
					Library::Instance().add(id, track);
				}
			}));
		});
	}, &st::menuIconAdd);
	_menu->addAction(Utf("Добавить в плейлист"), std::move(add), &st::menuIconAddToFolder);
	if (const auto playlist = openedPlaylist()) {
		const auto id = playlist->id;
		const auto count = int(playlist->tracks.size());
		if (index > 0) {
			_menu->addAction(Utf("Выше"), [=] {
				Library::Instance().move(id, index, index - 1);
			}, &st::menuIconReorder);
		}
		if (index + 1 < count) {
			_menu->addAction(Utf("Ниже"), [=] {
				Library::Instance().move(id, index, index + 1);
			}, &st::menuIconReorder);
		}
		_menu->addAction(Utf("Убрать из плейлиста"), [=] {
			Library::Instance().removeTrack(id, track);
		}, &st::menuIconDelete);
	}
	if (!track.permalink.isEmpty()) {
		_menu->addAction(Utf("Открыть на ") + SourceLabel(track.source), [=] {
			OpenLink(track.permalink);
		}, &st::menuIconShare);
	}
	_menu->popup(QCursor::pos());
}

void MusicPage::askTitle(
		const QString &title,
		const QString &initial,
		Fn<void(QString)> done) {
	_controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Название")),
			initial));
		field->setMaxLength(100);
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto submit = [=] {
			const auto text = field->getLastText().trimmed();
			if (text.isEmpty()) {
				field->showError();
				return;
			}
			done(text);
			box->closeBox();
		};
		field->submits() | rpl::on_next(submit, field->lifetime());
		box->addButton(rpl::single(Utf("Сохранить")), submit);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace AyuUi
