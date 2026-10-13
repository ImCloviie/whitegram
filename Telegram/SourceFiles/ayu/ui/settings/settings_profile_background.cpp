// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_profile_background.h"

#include "ayu/features/custom_profile_background/custom_profile_background.h"
#include "ayu/ui/settings/settings_main.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "ui/effects/animation_value.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

namespace Settings {
namespace {

namespace Background = AyuFeatures::CustomProfileBackground;
using Zone = Background::Zone;
using namespace Builder;

// The preview card, in style pixels.
constexpr auto kCardMaxWidth = 320;
constexpr auto kCardHeight = 236;
constexpr auto kCardTop = 16;
constexpr auto kCardRadius = 14;
constexpr auto kHeaderHeight = 112;
constexpr auto kUserpicTop = 14;
constexpr auto kUserpicSize = 48;
constexpr auto kContentSkip = 12;
constexpr auto kRowHeight = 28;
constexpr auto kRowsCount = 2;
constexpr auto kTileSkip = 2;

[[nodiscard]] rpl::producer<> BackgroundRefreshes(uint64 peerId) {
	return rpl::single(
		rpl::empty
	) | rpl::then(
		Background::BackgroundChanges(peerId)
	);
}

[[nodiscard]] rpl::producer<bool> HasBackgroundValue(
		uint64 peerId,
		Zone zone) {
	return BackgroundRefreshes(
		peerId
	) | rpl::map([=] {
		return Background::HasBackground(peerId, zone);
	}) | rpl::distinct_until_changed();
}

enum class Kind {
	None,
	Photo,
	Video,
};

[[nodiscard]] rpl::producer<Kind> KindValue(uint64 peerId, Zone zone) {
	return BackgroundRefreshes(
		peerId
	) | rpl::map([=] {
		return !Background::HasBackground(peerId, zone)
			? Kind::None
			: Background::BackgroundIsVideo(peerId, zone)
			? Kind::Video
			: Kind::Photo;
	}) | rpl::distinct_until_changed();
}

struct Mode {
	bool full = false;
	bool top = false;
	bool bottom = false;

	friend inline bool operator==(const Mode &, const Mode &) = default;
};

[[nodiscard]] rpl::producer<Mode> ModeValue(uint64 peerId) {
	return BackgroundRefreshes(
		peerId
	) | rpl::map([=] {
		return Mode{
			.full = Background::HasBackground(peerId, Zone::Full),
			.top = Background::HasBackground(peerId, Zone::Top),
			.bottom = Background::HasBackground(peerId, Zone::Bottom),
		};
	}) | rpl::distinct_until_changed();
}

// A small profile with the backgrounds painted where they are on the real
// one, so the effect of every zone is seen before leaving the page.
class Preview final : public Ui::BoxContentDivider {
public:
	Preview(QWidget *parent, not_null<UserData*> self);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Layout {
		QRect card;
		QRect header;
		QRect info;
	};

	[[nodiscard]] Layout countLayout() const;
	void updateCard();
	void reload();
	void paintCard(QPainter &p, const Layout &layout);
	void paintHeader(QPainter &p, const QRect &header);
	void paintInfo(QPainter &p, const QRect &info);
	void paintPosts(QPainter &p, const Layout &layout);

	const not_null<UserData*> _self;
	const uint64 _peerId = 0;
	const not_null<Ui::FlatLabel*> _about;
	Ui::PeerUserpicView _userpic;
	Background::Renderer _full;
	Background::Renderer _top;
	Background::Renderer _bottom;
	bool _resizing = false;

};

Preview::Preview(QWidget *parent, not_null<UserData*> self)
: BoxContentDivider(parent)
, _self(self)
, _peerId(self->id.value)
, _about(Ui::CreateChild<Ui::FlatLabel>(
	this,
	trWhitegram(
		QString::fromUtf8(
			"Подходят фото и видео меньше 32 МБ. Фон видят все, кто "
			"пользуется Whitegram. Лучше всего смотрятся вертикальные "
			"изображения, например 1080×2800."),
		QString(
			"Photos and videos under 32 MB work. Everyone who uses "
			"Whitegram sees your background. Vertical images, like "
			"1080×2800, look best.")),
	st::defaultDividerLabel.label))
, _full([=] { updateCard(); })
, _top([=] { updateCard(); })
, _bottom([=] { updateCard(); }) {
	_about->show();
	_about->heightValue(
	) | rpl::on_next([=] {
		if (!_resizing && width() > 0) {
			resizeToWidth(width());
		}
	}, lifetime());

	_self->loadUserpic();
	rpl::merge(
		_self->session().downloaderTaskFinished() | rpl::filter([=] {
			return Ui::PeerUserpicLoading(_userpic);
		}),
		_self->session().changes().peerUpdates(
			_self,
			(Data::PeerUpdate::Flag::Name | Data::PeerUpdate::Flag::Photo)
		) | rpl::to_empty,
		Lang::GetInstance().idChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		updateCard();
	}, lifetime());

	Background::BackgroundChanges(
		_peerId
	) | rpl::on_next([=] {
		reload();
	}, lifetime());
	reload();

	// The backgrounds set on another device are known only from the server.
	Background::EnsureRemoteBackgrounds(_peerId);
}

void Preview::updateCard() {
	update(countLayout().card);
}

void Preview::reload() {
	// Only the zones the profile would show are loaded, a whole profile
	// background hides the other two.
	if (Background::HasBackground(_peerId, Zone::Full)) {
		_full.setPeer(_peerId, Zone::Full);
		_top.clear();
		_bottom.clear();
	} else {
		_full.clear();
		_top.setPeer(_peerId, Zone::Top);
		_bottom.setPeer(_peerId, Zone::Bottom);
	}
	update();
}

int Preview::resizeGetHeight(int newWidth) {
	const auto &padding = st::defaultBoxDividerLabelPadding;
	_resizing = true;
	_about->resizeToWidth(std::max(
		newWidth - padding.left() - padding.right(),
		1));
	_resizing = false;
	const auto aboutTop = style::ConvertScale(kCardTop)
		+ style::ConvertScale(kCardHeight)
		+ padding.top();
	_about->moveToLeft(padding.left(), aboutTop, newWidth);
	return aboutTop + _about->height() + padding.bottom();
}

Preview::Layout Preview::countLayout() const {
	const auto margin = st::defaultBoxDividerLabelPadding.left();
	const auto width = std::min(
		std::max(this->width() - 2 * margin, 1),
		style::ConvertScale(kCardMaxWidth));
	const auto card = QRect(
		(this->width() - width) / 2,
		style::ConvertScale(kCardTop),
		width,
		style::ConvertScale(kCardHeight));
	const auto header = QRect(
		card.x(),
		card.y(),
		card.width(),
		style::ConvertScale(kHeaderHeight));
	const auto skip = style::ConvertScale(kContentSkip) / 2;
	const auto info = QRect(
		card.x(),
		header.y() + header.height(),
		card.width(),
		(2 * skip)
			+ (kRowsCount + 1) * style::ConvertScale(kRowHeight));
	return { card, header, info };
}

void Preview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), color());
	paintTop(p);
	paintBottom(p);

	const auto layout = countLayout();
	if (!e->rect().intersects(layout.card)) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	auto path = QPainterPath();
	const auto radius = style::ConvertScale(kCardRadius);
	path.addRoundedRect(QRectF(layout.card), radius, radius);
	p.setClipPath(path);
	paintCard(p, layout);
}

void Preview::paintCard(QPainter &p, const Layout &layout) {
	const auto &card = layout.card;
	p.fillRect(card, st::windowBg);

	// Painted the same way and with the same tints as on the profile.
	if (_full.paint(p, card, card.height(), 0, 0)) {
		Background::PaintFullOverlay(
			p,
			card,
			0,
			card.height(),
			layout.header.height(),
			st::windowBg->c);
	} else {
		if (!_top.paint(p, layout.header, layout.header.height(), 0)) {
			p.fillRect(layout.header, st::windowBgOver);
		}
		if (_bottom.paint(p, layout.info, layout.info.height(), 0, 0)) {
			Background::PaintInfoOverlay(p, layout.info, st::windowBg->c);
		}
	}
	paintHeader(p, layout.header);
	paintInfo(p, layout.info);
	paintPosts(p, layout);
}

void Preview::paintHeader(QPainter &p, const QRect &header) {
	const auto size = style::ConvertScale(kUserpicSize);
	const auto userpicTop = header.y() + style::ConvertScale(kUserpicTop);
	_self->paintUserpic(
		p,
		_userpic,
		header.x() + (header.width() - size) / 2,
		userpicTop,
		size);

	// The theme colors, as the profile itself keeps them over a background.
	const auto available = header.width() - 2 * style::ConvertScale(16);
	const auto paintLine = [&](
			const QString &text,
			const style::font &font,
			const style::color &color,
			int top) {
		const auto elided = font->elided(text, available);
		const auto line = QRect(header.x(), top, header.width(), font->height);
		p.setFont(font);
		p.setPen(color);
		p.drawText(line, Qt::AlignHCenter, elided);
	};
	const auto nameTop = userpicTop + size + style::ConvertScale(6);
	paintLine(_self->name(), st::semiboldFont, st::windowBoldFg, nameTop);
	paintLine(
		tr::lng_status_online(tr::now),
		st::normalFont,
		st::windowActiveTextFg,
		nameTop + st::semiboldFont->height + style::ConvertScale(1));
}

void Preview::paintInfo(QPainter &p, const QRect &info) {
	const auto left = info.x() + style::ConvertScale(18);
	const auto width = info.width() - 2 * (left - info.x());
	const auto row = style::ConvertScale(kRowHeight);
	const auto bar = style::ConvertScale(7);
	const auto small = style::ConvertScale(5);
	const auto value = anim::with_alpha(st::windowFg->c, 0.22);
	const auto label = anim::with_alpha(st::windowFg->c, 0.12);
	constexpr auto widths = std::array{ 0.55, 0.4 };
	static_assert(widths.size() == kRowsCount);
	p.setPen(Qt::NoPen);
	auto top = info.y() + style::ConvertScale(kContentSkip) / 2;
	for (auto i = 0; i != kRowsCount; ++i) {
		const auto valueTop = top + (row - bar - small) / 2 - small / 2;
		p.setBrush(value);
		p.drawRoundedRect(
			QRect(left, valueTop, int(width * widths[i]), bar),
			bar / 2.,
			bar / 2.);
		p.setBrush(label);
		p.drawRoundedRect(
			QRect(left, valueTop + bar + small, int(width * 0.24), small),
			small / 2.,
			small / 2.);
		top += row;
	}
	// The "Add to contacts" button, which is a part of this block.
	p.setBrush(anim::with_alpha(st::windowActiveTextFg->c, 0.55));
	p.drawRoundedRect(
		QRect(left, top + (row - bar) / 2, int(width * 0.42), bar),
		bar / 2.,
		bar / 2.);
}

void Preview::paintPosts(QPainter &p, const Layout &layout) {
	const auto &card = layout.card;
	const auto skip = style::ConvertScale(kTileSkip);
	const auto top = layout.info.y()
		+ layout.info.height()
		+ style::ConvertScale(kContentSkip) / 2;
	const auto size = (card.width() - 2 * skip) / 3;
	p.setPen(Qt::NoPen);
	p.setBrush(anim::with_alpha(st::windowFg->c, 0.1));
	for (auto i = 0; i != 3; ++i) {
		p.drawRect(QRect(card.x() + i * (size + skip), top, size, size));
	}
}

struct ZoneDescriptor {
	Zone zone = Zone::Full;
	QString id;
	QString titleRu;
	QString titleEn;
	QString aboutRu;
	QString aboutEn;
	bool hiddenByFull = false;
};

void AddZone(
		SectionBuilder &builder,
		uint64 peerId,
		const ZoneDescriptor &descriptor) {
	const auto zone = descriptor.zone;
	const auto controller = builder.controller();

	builder.addSubsectionTitle({
		.id = descriptor.id,
		.title = trWhitegram(descriptor.titleRu, descriptor.titleEn),
		.keywords = {
			u"background"_q,
			u"photo"_q,
			u"video"_q,
			QString::fromUtf8("фон"),
			QString::fromUtf8("фото"),
			QString::fromUtf8("видео"),
		},
	});

	auto hasValue = [=] { return HasBackgroundValue(peerId, zone); };
	builder.addButton({
		.title = rpl::combine(
			trWhitegram(
				QString::fromUtf8("Выбрать фото или видео"),
				QString("Choose photo or video")),
			trWhitegram(
				QString::fromUtf8("Заменить фото или видео"),
				QString("Replace photo or video")),
			hasValue()
		) | rpl::map([](QString choose, QString replace, bool has) {
			return has ? replace : choose;
		}),
		.icon = { &st::menuIconPhoto },
		.label = rpl::combine(
			trWhitegram(QString::fromUtf8("Не задан"), QString("Not set")),
			trWhitegram(QString::fromUtf8("Фото"), QString("Photo")),
			trWhitegram(QString::fromUtf8("Видео"), QString("Video")),
			KindValue(peerId, zone)
		) | rpl::map([](
				QString none,
				QString photo,
				QString video,
				Kind kind) {
			return (kind == Kind::Video)
				? video
				: (kind == Kind::Photo)
				? photo
				: none;
		}),
		.onClick = [=] {
			Background::PickBackground(controller, peerId, zone);
		},
	});
	builder.addButton({
		.title = trWhitegram(
			QString::fromUtf8("Удалить фон"),
			QString("Remove background")),
		.st = &st::settingsAttentionButtonWithIcon,
		.icon = { &st::menuIconDeleteAttention },
		.onClick = [=] {
			Background::ConfirmRemoveBackground(controller, peerId, zone);
		},
		.shown = hasValue(),
	});
	builder.addSkip();

	auto about = trWhitegram(descriptor.aboutRu, descriptor.aboutEn);
	if (descriptor.hiddenByFull) {
		about = rpl::combine(
			std::move(about),
			trWhitegram(
				QString::fromUtf8(
					"Не показывается, пока задан фон всего профиля."),
				QString(
					"Hidden while a whole profile background is set.")),
			HasBackgroundValue(peerId, Zone::Full)
		) | rpl::map([](QString about, QString hidden, bool full) {
			return full ? (about + u"\n\n"_q + hidden) : about;
		});
	}
	builder.addDividerText(std::move(about));
	builder.addSkip();
}

} // namespace

void BuildProfileBackgroundSection(SectionBuilder &builder) {
	const auto peerId = builder.session()->userPeerId().value;

	builder.add([=](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = object_ptr<Preview>(
				ctx.container,
				ctx.controller->session().user()),
		};
	});
	builder.addSkip();

	AddZone(builder, peerId, {
		.zone = Zone::Full,
		.id = u"edit/profile-background/full"_q,
		.titleRu = QString::fromUtf8("Весь профиль"),
		.titleEn = QString("Whole profile"),
		.aboutRu = QString::fromUtf8(
			"Покрывает весь профиль сверху донизу: шапку, информацию, "
			"кнопки, публикации и подарки. Заменяет фоны шапки и "
			"информации."),
		.aboutEn = QString(
			"Covers your whole profile from top to bottom: the header, "
			"info, buttons, posts and gifts. Replaces the header and info "
			"backgrounds."),
	});
	AddZone(builder, peerId, {
		.zone = Zone::Top,
		.id = u"edit/profile-background/header"_q,
		.titleRu = QString::fromUtf8("Шапка"),
		.titleEn = QString("Header"),
		.aboutRu = QString::fromUtf8(
			"Фон за фотографией, именем и статусом."),
		.aboutEn = QString(
			"The background behind your photo, name and status."),
		.hiddenByFull = true,
	});
	AddZone(builder, peerId, {
		.zone = Zone::Bottom,
		.id = u"edit/profile-background/info"_q,
		.titleRu = QString::fromUtf8("Информация"),
		.titleEn = QString("Info"),
		.aboutRu = QString::fromUtf8(
			"Фон за номером телефона, именем пользователя, разделом "
			"«О себе», ID и кнопкой «Добавить в контакты»."),
		.aboutEn = QString(
			"The background behind your phone number, username, bio, ID "
			"and the Add to Contacts button."),
		.hiddenByFull = true,
	});
}

void AddProfileBackgroundEntry(SectionBuilder &builder) {
	const auto peerId = builder.session()->userPeerId().value;
	const auto showOther = builder.showOther();
	if (builder.container()) {
		// The label shows a background set on another device too.
		Background::EnsureRemoteBackgrounds(peerId);
	}

	// The page registers itself in search, as every settings page does, so
	// the row has no search entry of its own.
	builder.addSkip();
	builder.addButton({
		.title = trWhitegram(
			QString::fromUtf8("Фон профиля"),
			QString("Profile background")),
		.icon = { &st::menuIconPhotoSet },
		.label = rpl::combine(
			trWhitegram(QString::fromUtf8("Не задан"), QString("Not set")),
			trWhitegram(
				QString::fromUtf8("Весь профиль"),
				QString("Whole profile")),
			trWhitegram(QString::fromUtf8("Шапка"), QString("Header")),
			trWhitegram(QString::fromUtf8("Информация"), QString("Info")),
			ModeValue(peerId)
		) | rpl::map([](
				QString none,
				QString full,
				QString header,
				QString info,
				Mode mode) {
			return mode.full
				? full
				: (mode.top && mode.bottom)
				? (header + u", "_q + info.toLower())
				: mode.top
				? header
				: mode.bottom
				? info
				: none;
		}),
		.onClick = [=] {
			if (showOther) {
				showOther(WhitegramProfileBackgroundId());
			}
		},
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Фото или видео за вашим профилем. Его увидят все, кто "
			"пользуется Whitegram."),
		QString(
			"A photo or video behind your profile. Everyone who uses "
			"Whitegram will see it.")));
}

} // namespace Settings
