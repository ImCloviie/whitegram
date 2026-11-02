// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "lang_auto.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session_settings.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/custom_profile_background/custom_profile_background.h"
#include "ayu/features/connection_bypass/connection_bypass.h"
#include "ayu/features/self_destruct/self_destruct.h"
#include "ayu/features/self_destruct/self_destruct_box.h"
#include "ayu/features/custom_archives/custom_archives.h"
#include "ayu/ui/ayu_logo.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/settings/custom_archives_box.h"
#include "ayu/ui/settings/settings_appearance.h"
#include "ayu/ui/settings/settings_ayu.h"
#include "ayu/ui/settings/settings_chats.h"
#include "ayu/ui/settings/settings_filters.h"
#include "ayu/ui/settings/settings_general.h"
#include "ayu/ui/settings/settings_other.h"
#include "core/version.h"
#include "core/update_checker.h"
#include "ayu/features/whitegram_backend/whitegram_backend.h"
#include "ayu/utils/telegram_helpers.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/text/format_values.h"
#include "styles/style_boxes.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/layers/generic_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_dialogs.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/checkbox.h"
#include "window/window_peer_menu.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "boxes/peer_list_box.h"
#include "styles/style_ayu_icons.h"
#include <algorithm>
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "window/window_controller.h"

#include <QDesktopServices>
#include <QFileInfo>

#include "core/application.h"
#include "core/file_utilities.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

namespace Background = AyuFeatures::CustomProfileBackground;

void BuildLogo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto logo = object_ptr<Ui::RpWidget>(ctx.container);
		const auto logoRaw = logo.data();
		logoRaw->resize(
			QSize(st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize));
		logoRaw->setNaturalWidth(st::settingsCloudPasswordIconSize);
		logoRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(logoRaw);
			const auto image = AyuAssets::currentAppLogoPad();
			if (!image.isNull()) {
				const auto size = st::settingsCloudPasswordIconSize;
				const auto scaled = image.scaled(
					size * style::DevicePixelRatio(),
					size * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				p.drawImage(QRect(0, 0, size, size), scaled);
			}
		}, logoRaw->lifetime());
		return { .widget = std::move(logo), .align = style::al_top };
	});
}

void BuildVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(
					QString("AyuGram Desktop v")
					+ QString::fromLatin1(AppVersionStr)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				tr::ayu_SettingsDescription(),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildAyuCategories(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::ayu_CategoriesHeader());

	builder.addSectionButton({
		.title = rpl::single(QString("AyuGram")),
		.targetSection = AyuGhost::Id(),
		.icon = { &st::menuIconGroupReactions },
		.keywords = { u"ghost"_q, u"ayugram"_q },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryFilters(),
		.targetSection = AyuFilters::Id(),
		.icon = { &st::menuIconTagFilter },
		.keywords = { u"filters"_q, u"regex"_q },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryGeneral(),
		.targetSection = AyuGeneral::Id(),
		.icon = { &st::menuIconShowAll },
		.keywords = { u"general"_q },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryAppearance(),
		.targetSection = AyuAppearance::Id(),
		.icon = { &st::menuIconPalette },
		.keywords = { u"appearance"_q, u"theme"_q },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryChats(),
		.targetSection = AyuChats::Id(),
		.icon = { &st::menuIconChatBubble },
		.keywords = { u"chats"_q, u"messages"_q },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryOther(),
		.targetSection = AyuOther::Id(),
		.icon = { &st::menuIconFave },
		.keywords = { u"other"_q, u"donate"_q },
	});
}

void BuildAyuLinks(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::ayu_LinksHeader());

	const auto controller = builder.controller();

	builder.addButton({
		.title = tr::ayu_LinksChannel(),
		.icon = { &st::menuIconChannel },
		.label = rpl::single(QString("@ayugram")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ayugram"),
			});
		},
	});
	builder.addButton({
		.title = tr::ayu_LinksChats(),
		.icon = { &st::menuIconChats },
		.label = rpl::single(QString("@ayugramchat")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ayugramchat"),
			});
		},
	});
	builder.addButton({
		.title = tr::ayu_LinksTranslate(),
		.icon = { &st::menuIconTranslate },
		.label = rpl::single(QString("Crowdin")),
		.onClick = [=] {
			QDesktopServices::openUrl(
				QString("https://translate.ayugram.one"));
		},
	});
	builder.addButton({
		.title = tr::ayu_LinksDocumentation(),
		.icon = { &st::menuIconIpAddress },
		.label = rpl::single(QString("docs.ayugram.one")),
		.onClick = [=] {
			QDesktopServices::openUrl(
				QString("https://docs.ayugram.one"));
		},
	});

	builder.addSkip();
}

const auto kAyuMeta = BuildHelper({
	.id = AyuMain::Id(),
	.parentId = MainId(),
	.title = &tr::ayu_AyuPreferences,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);
	builder.addSkip();
	BuildVersionInfo(builder);
	BuildAyuCategories(builder);
	BuildAyuLinks(builder);
});


// Whitegram

void BuildWhitegramVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(
					QString("Whitegram v")
					+ QString::fromLatin1(AppVersionStr)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(Whitegram::Backend::BetaBadgeText()),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				trWhitegram(
					QString::fromUtf8("Улучшенный форк AyuGram с дополнительными функциями, эксклюзивной кастомизацией и уникальными возможностями."),
					QString("An enhanced fork of AyuGram with additional features, exclusive customization, and unique capabilities.")),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildCryptoSettings(SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Курсы криптовалют"),
			QString("Cryptocurrency Rates")));

	ayu.addSettingToggle({
		.id = u"whitegram/showCryptoInDrawer"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать в боковом меню"),
			QString("Show in side menu")),
		.getter = &AyuSettings::showCryptoInDrawer,
		.setter = &AyuSettings::setShowCryptoInDrawer,
	});

	auto *settings = &AyuSettings::getInstance();

	auto getFiatOptions = [] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return std::vector<QString>{
			isRu ? QString::fromUtf8("Доллар США (USD)") : QString("US Dollar (USD)"),
			isRu ? QString::fromUtf8("Евро (EUR)") : QString("Euro (EUR)"),
			isRu ? QString::fromUtf8("Российский рубль (RUB)") : QString("Russian Ruble (RUB)"),
		};
	};

	auto currentFiat = rpl::combine(
		AyuSettings::getInstance().cryptoFiatCurrencyValue(),
		trWhitegram(u"ru"_q, u"en"_q)
	) | rpl::map([=](int val, const QString &) {
		const auto options = getFiatOptions();
		return (val >= 0 && val < int(options.size()))
			? options[val]
			: options[0];
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"whitegram/cryptoFiatCurrency"_q,
		.title = trWhitegram(
			QString::fromUtf8("Валюта отображения"),
			QString("Display Currency")),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentFiat),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						AyuSettings::getInstance().setCryptoFiatCurrency(index);
					};
					const auto options = getFiatOptions();
					SingleChoiceBox(box, {
						.title = trWhitegram(
							QString::fromUtf8("Валюта отображения"),
							QString("Display Currency")),
						.options = options,
						.initialSelection = settings->cryptoFiatCurrency(),
						.callback = save,
					});
				}));
		},
	});

	ayu.addSectionDivider();

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Отображаемые монеты"),
			QString("Displayed Coins")));

	ayu.addSettingToggle({
		.id = u"whitegram/showBTC"_q,
		.title = rpl::single(QString("Bitcoin (BTC)")),
		.getter = &AyuSettings::showBTC,
		.setter = &AyuSettings::setShowBTC,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showETH"_q,
		.title = rpl::single(QString("Ethereum (ETH)")),
		.getter = &AyuSettings::showETH,
		.setter = &AyuSettings::setShowETH,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showTON"_q,
		.title = rpl::single(QString("Gram (prev. Toncoin)")),
		.getter = &AyuSettings::showTON,
		.setter = &AyuSettings::setShowTON,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showSOL"_q,
		.title = rpl::single(QString("Solana (SOL)")),
		.getter = &AyuSettings::showSOL,
		.setter = &AyuSettings::setShowSOL,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showBNB"_q,
		.title = rpl::single(QString("BNB")),
		.getter = &AyuSettings::showBNB,
		.setter = &AyuSettings::setShowBNB,
	});

	ayu.addSectionDivider();

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Обычные валюты (к текущей)"),
			QString("Fiat Currencies (to current)")));

	ayu.addSettingToggle({
		.id = u"whitegram/showFiatUSD"_q,
		.title = trWhitegram(
			QString::fromUtf8("Доллар США (USD)"),
			QString("US Dollar (USD)")),
		.getter = &AyuSettings::showFiatUSD,
		.setter = &AyuSettings::setShowFiatUSD,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showFiatEUR"_q,
		.title = trWhitegram(
			QString::fromUtf8("Евро (EUR)"),
			QString("Euro (EUR)")),
		.getter = &AyuSettings::showFiatEUR,
		.setter = &AyuSettings::setShowFiatEUR,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showFiatRUB"_q,
		.title = trWhitegram(
			QString::fromUtf8("Российский рубль (RUB)"),
			QString("Russian Ruble (RUB)")),
		.getter = &AyuSettings::showFiatRUB,
		.setter = &AyuSettings::setShowFiatRUB,
	});

	ayu.addSectionDivider();
	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Настройки графиков"),
			QString("Chart Settings")));

	ayu.addSettingToggle({
		.id = u"whitegram/showCryptoCharts"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать мини-графики"),
			QString("Show mini charts")),
		.getter = &AyuSettings::showCryptoCharts,
		.setter = &AyuSettings::setShowCryptoCharts,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showCryptoChanges"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать изменение за 24 часа"),
			QString("Show 24-hour change")),
		.getter = &AyuSettings::showCryptoChanges,
		.setter = &AyuSettings::setShowCryptoChanges,
	});

	const auto chartDays = AyuSettings::getInstance().cryptoChartDays();
	const auto chartDaysIndex = (chartDays == 1)
		? 0
		: (chartDays == 7)
			? 1
			: (chartDays == 30)
				? 2
				: 3;
	ayu.addChooseButton({
		.id = u"whitegram/cryptoChartDays"_q,
		.title = trWhitegram(
			QString::fromUtf8("Период мини-графиков"),
			QString("Mini chart period")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Период мини-графиков"),
			QString("Mini chart period")),
		.initialSelection = chartDaysIndex,
		.options = {
			QString::fromUtf8("24 часа"),
			QString::fromUtf8("7 дней"),
			QString::fromUtf8("30 дней"),
			QString::fromUtf8("90 дней"),
		},
		.setter = [](int index) {
			static constexpr int kDays[] = { 1, 7, 30, 90 };
			if (index >= 0 && index < 4) {
				AyuSettings::getInstance().setCryptoChartDays(kDays[index]);
			}
		},
	});
}

void BuildWhitegramLinks(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::ayu_LinksHeader());

	const auto controller = builder.controller();

	builder.addButton({
		.id = u"whitegram/creator"_q,
		.title = trWhitegram(
			QString::fromUtf8("Разработчик"),
			QString("Developer")),
		.icon = { &st::menuIconChannel },
		.label = rpl::single(QString("@ImCloviie")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ImCloviie"),
			});
		},
	});
	builder.addButton({
		.id = u"whitegram/designer"_q,
		.title = trWhitegram(
			QString::fromUtf8("Дизайнер"),
			QString("Designer")),
		.icon = { &st::menuIconPalette },
		.label = rpl::single(QString("@ImKloviie")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ImKloviie"),
			});
		},
	});

	builder.addSkip();
}

const auto kWhitegramCryptoMeta = BuildHelper({
	.id = WhitegramCrypto::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Курсы криптовалют"),
	.icon = &st::menuIconTradable,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	BuildCryptoSettings(builder);
	builder.addSkip();
});

const auto kWhitegramTimeSpentMeta = BuildHelper({
	.id = WhitegramTimeSpent::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Счетчик времени"),
	.icon = &st::menuIconTimer,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	rpl::producer<QString> timeText = rpl::combine(
		AyuSettings::getInstance().totalTimeSpent(),
		AyuSettings::getInstance().timeSpentFormatValue(),
		trWhitegram(QString::fromUtf8("ru"), QString("en"))
	) | rpl::map([](int64 minutes, int format, const QString &) {
		return QString::fromUtf8("Проведено в Whitegram: ")
			+ formatTimeSpent(minutes, format);
	});
	builder.addButton({
		.id = u"whitegram/timespent"_q,
		.title = std::move(timeText),
		.icon = { &st::menuIconTimer },
	});
	
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);
	ayu.addSettingToggle({
		.id = u"whitegram/showTimeSpentInDrawer"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать в боковом меню"),
			QString("Show in side menu")),
		.getter = &AyuSettings::showTimeSpentInDrawer,
		.setter = &AyuSettings::setShowTimeSpentInDrawer,
	});

	const auto settings = &AyuSettings::getInstance();

	const auto getFormatOptions = [] {
		return std::vector<QString>{
			QString::fromUtf8("Только часы (340 ч)"),
			QString::fromUtf8("Часы и минуты (340 ч. 11 мин.)"),
			QString::fromUtf8("Только дни (14 дн.)"),
			QString::fromUtf8("Дни и часы (14 дн. 3 ч.)"),
		};
	};

	auto currentFormat = rpl::combine(
		AyuSettings::getInstance().timeSpentFormatValue(),
		trWhitegram(u"ru"_q, u"en"_q)
	) | rpl::map([=](int val, const QString &) {
		const auto options = getFormatOptions();
		return (val >= 0 && val < int(options.size()))
			? options[val]
			: options[0];
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"whitegram/timeSpentFormat"_q,
		.title = trWhitegram(
			QString::fromUtf8("Формат отображения"),
			QString("Display Format")),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentFormat),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						AyuSettings::getInstance().setTimeSpentFormat(index);
					};
					SingleChoiceBox(box, {
						.title = trWhitegram(
							QString::fromUtf8("Формат отображения"),
							QString("Display Format")),
						.options = getFormatOptions(),
						.initialSelection = settings->timeSpentFormat(),
						.callback = save,
					});
				}));
		},
	});

	// The drawer counter label template.
	const auto getLabelOptions = [] {
		return std::vector<QString>{
			QString::fromUtf8("В приложении: 356 ч"),
			QString::fromUtf8("В Whitegram: 356 ч"),
			QString::fromUtf8("Залипание: 356 ч"),
			QString::fromUtf8("Онлайн: 356 ч"),
		};
	};
	auto currentLabel = AyuSettings::getInstance().timeSpentLabelStyleValue(
	) | rpl::map([=](int val) {
		const auto options = getLabelOptions();
		return (val >= 0 && val < int(options.size()))
			? options[val]
			: options[0];
	});
	builder.addButton({
		.id = u"whitegram/timeSpentLabelStyle"_q,
		.title = trWhitegram(
			QString::fromUtf8("Текст в боковом меню"),
			QString("Side menu text")),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentLabel),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						AyuSettings::getInstance().setTimeSpentLabelStyle(index);
					};
					SingleChoiceBox(box, {
						.title = trWhitegram(
							QString::fromUtf8("Текст в боковом меню"),
							QString("Side menu text")),
						.options = getLabelOptions(),
						.initialSelection = settings->timeSpentLabelStyle(),
						.callback = save,
					});
				}));
		},
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	ayu.addSettingToggle({
		.id = u"whitegram/showTimeSpentInProfile"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать время в моем профиле"),
			QString("Show time in my profile")),
		.getter = &AyuSettings::showTimeSpentInProfile,
		.setter = &AyuSettings::setShowTimeSpentInProfile,
	});
	builder.addSkip();
});

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

class UpdatesProgressBar final : public Ui::RpWidget {
public:
	explicit UpdatesProgressBar(QWidget *parent) : Ui::RpWidget(parent) {
		resize(280, st::whitegramUpdatesProgressHeight);
	}

	void setProgress(float64 ratio) {
		const auto next = std::clamp(ratio, 0., 1.);
		if (_ratio == next) {
			return;
		}
		_ratio = next;
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::whitegramUpdatesProgressRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(rect(), radius, radius);
		if (_ratio <= 0.) {
			return;
		}
		auto gradient = QLinearGradient(0, 0, width(), 0);
		gradient.setStops({
			{ 0., st::groupCallLive1->c },
			{ 1., st::groupCallLive2->c },
		});
		p.setBrush(QBrush(std::move(gradient)));
		const auto filled = std::max(
			radius * 2,
			int(std::ceil(width() * _ratio)));
		p.drawRoundedRect(
			QRect(0, 0, filled, height()),
			radius,
			radius);
	}

private:
	float64 _ratio = 0.;

};

class BypassMirrorCard final : public Ui::RpWidget {
public:
	explicit BypassMirrorCard(QWidget *parent)
	: Ui::RpWidget(parent)
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
		_content->heightValue(
		) | rpl::on_next([=](int height) {
			const auto padding = st::whitegramBypassMirrorCardPadding;
			const auto full = padding.top() + height + padding.bottom();
			if (this->height() != full) {
				resize(width(), full);
			}
		}, lifetime());
	}

	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const {
		return _content;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::whitegramBypassMirrorCardRadius;
		const auto rect = this->rect();
		p.setPen(QPen(st::windowBgRipple, st::lineWidth));
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			rect - QMargins(st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth),
			radius,
			radius);

		auto gradient = QLinearGradient(0, 0, width(), 0);
		gradient.setStops({
			{ 0., st::groupCallLive1->c },
			{ 1., st::groupCallLive2->c },
		});
		QPainterPath clip;
		clip.addRoundedRect(rect, radius, radius);
		p.setClipPath(clip);
		p.fillRect(QRect(0, 0, width(), 3), QBrush(std::move(gradient)));
	}

	int resizeGetHeight(int newWidth) override {
		const auto padding = st::whitegramBypassMirrorCardPadding;
		const auto inner = newWidth - padding.left() - padding.right();
		if (inner > 0) {
			_content->resizeToWidth(inner);
		}
		_content->moveToLeft(padding.left(), padding.top(), newWidth);
		return padding.top() + _content->height() + padding.bottom();
	}

private:
	const not_null<Ui::VerticalLayout*> _content;

};

class ProfileBackgroundCard final : public Ui::RpWidget {
public:
	ProfileBackgroundCard(
		QWidget *parent,
		QString title,
		QString hint,
		uint64 peerId,
		::CustomProfileBackground::Zone zone,
		Fn<void()> pick,
		Fn<void()> remove)
	: Ui::RpWidget(parent)
	, _title(std::move(title))
	, _hint(std::move(hint))
	, _peerId(peerId)
	, _zone(zone)
	, _pick(std::move(pick))
	, _remove(std::move(remove))
	, _renderer([=] { update(); }) {
		setMouseTracking(true);
		AyuSettings::getInstance().customProfileBackgroundChanges(
		) | rpl::on_next([=] {
			reload();
		}, lifetime());
		Whitegram::Backend::profileBackgroundUpdates(
		) | rpl::on_next([=](uint64 userId) {
			if (userId == _peerId) {
				reload();
			}
		}, lifetime());
		AyuSettings::getInstance().profileBackgroundDimChanges(
		) | rpl::on_next([=](int) {
			update();
		}, lifetime());
		reload();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);

		const auto radius = st::profileBackgroundCardRadius;
		const auto rect = this->rect().marginsRemoved(
			st::profileBackgroundCardMargin);
		if (rect.isEmpty()) {
			return;
		}
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect, radius, radius);

		const auto inner = rect.marginsRemoved(st::profileBackgroundCardPadding);

		// Preview plate.
		const auto preview = QRect(
			inner.x(),
			inner.y(),
			inner.width(),
			st::profileBackgroundPreviewHeight);
		const auto previewRadius = st::profileBackgroundPreviewRadius;
		p.setBrush(st::windowBg);
		p.drawRoundedRect(preview, previewRadius, previewRadius);

		const auto path = BackgroundFilePath();
		if (!path.isEmpty()) {
			p.save();
			auto clip = QPainterPath();
			clip.addRoundedRect(
				QRectF(preview),
				previewRadius,
				previewRadius);
			p.setClipPath(clip);
			if (_renderer.paint(p, preview, preview.height(), 0, 0)) {
				// The same dimming as on the profile itself, so the
				// preview shows the designed look.
				Background::PaintInfoOverlay(p, preview, st::windowBg->c);
			} else {
				// A video whose first frame is not painted yet.
				p.fillRect(preview, st::windowBgOver);
			}
			p.restore();
		}

		// Title.
		p.setPen(st::windowActiveTextFg);
		p.setFont(st::semiboldFont);
		const auto textTop = preview.bottom()
			+ 1 + st::profileBackgroundZoneSkip;
		p.drawText(
			QRect(inner.x(), textTop, inner.width(), st::semiboldFont->height),
			_title,
			Qt::AlignVCenter | Qt::AlignLeft);

		// Status badge under the title.
		const auto has = !path.isEmpty();
		const auto badgeText = has
			? QString::fromUtf8("Настроен")
			: QString::fromUtf8("Не задан");
		p.setFont(st::defaultTextStyle.font);
		const auto badgeWidth = st::defaultTextStyle.font->width(badgeText)
			+ st::profileBackgroundBadgePadding.left()
			+ st::profileBackgroundBadgePadding.right();
		const auto badgeHeight = st::defaultTextStyle.font->height
			+ st::profileBackgroundBadgePadding.top()
			+ st::profileBackgroundBadgePadding.bottom();
		const auto badge = QRect(
			inner.x(),
			textTop + st::semiboldFont->height + 4,
			badgeWidth,
			badgeHeight);
		p.setBrush(has ? st::groupCallLive1 : st::windowSubTextFg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(badge, st::profileBackgroundBadgeRadius, st::profileBackgroundBadgeRadius);
		p.setPen(st::windowBg);
		p.drawText(badge, badgeText, Qt::AlignVCenter | Qt::AlignHCenter);

		// Remove button on the right of the badge, only when set.
		if (has) {
			const auto removeText = Lang::GetInstance().id().startsWith(u"ru"_q)
				? QString::fromUtf8("Удалить")
				: QString("Remove");
			const auto removeWidth = st::defaultTextStyle.font->width(removeText)
				+ st::profileBackgroundBadgePadding.left()
				+ st::profileBackgroundBadgePadding.right();
			const auto remove = QRect(
				inner.x() + inner.width() - removeWidth,
				badge.y(),
				removeWidth,
				badgeHeight);
			_removeRect = remove;
			p.setBrush(Qt::NoBrush);
			p.setPen(st::boxTextFgError->c);
			p.setFont(st::defaultTextStyle.font);
			p.drawText(remove, removeText, Qt::AlignVCenter | Qt::AlignHCenter);
		} else {
			_removeRect = QRect();
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			if (_remove && _removeRect.contains(e->pos())) {
				_remove();
			} else {
				_pick();
			}
		}
	}

	int resizeGetHeight(int newWidth) override {
		return st::profileBackgroundCardMargin.top()
			+ st::profileBackgroundCardPadding.top()
			+ st::profileBackgroundPreviewHeight
			+ st::profileBackgroundZoneSkip
			+ st::semiboldFont->height
			+ 4
			+ st::defaultTextStyle.font->height
			+ 2 * st::profileBackgroundBadgePadding.top()
			+ st::profileBackgroundCardPadding.bottom()
			+ st::profileBackgroundCardMargin.bottom();
	}

private:
	[[nodiscard]] QString BackgroundFilePath() const {
		return AyuFeatures::CustomProfileBackground::BackgroundFilePath(
			_peerId,
			_zone);
	}

	void reload() {
		if (!BackgroundFilePath().isEmpty()) {
			_renderer.setPeer(_peerId, _zone);
		} else {
			_renderer.clear();
		}
		update();
	}

	QString _title;
	QString _hint;
	uint64 _peerId = 0;
	::CustomProfileBackground::Zone _zone = ::CustomProfileBackground::Zone::Full;
	Fn<void()> _pick;
	Fn<void()> _remove;
	QRect _removeRect;
	Background::Renderer _renderer;

};

class UpdatesHeroCard final : public Ui::RpWidget {
public:
	explicit UpdatesHeroCard(QWidget *parent)
	: Ui::RpWidget(parent)
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
		_content->heightValue(
		) | rpl::on_next([=](int height) {
			const auto padding = st::whitegramUpdatesCardPadding;
			const auto full = padding.top() + height + padding.bottom();
			if (this->height() != full) {
				resize(width(), full);
			}
		}, lifetime());
	}

	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const {
		return _content;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::whitegramUpdatesCardRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect(), radius, radius);

		const auto accent = st::whitegramUpdatesCardAccentHeight;
		auto gradient = QLinearGradient(0, 0, width(), 0);
		gradient.setStops({
			{ 0., st::groupCallLive1->c },
			{ 1., st::groupCallLive2->c },
		});

		QPainterPath clip;
		clip.addRoundedRect(rect(), radius, radius);
		p.setClipPath(clip);
		p.fillRect(QRect(0, 0, width(), accent), QBrush(std::move(gradient)));
	}

	int resizeGetHeight(int newWidth) override {
		const auto padding = st::whitegramUpdatesCardPadding;
		const auto inner = newWidth - padding.left() - padding.right();
		if (inner > 0) {
			_content->resizeToWidth(inner);
		}
		_content->moveToLeft(padding.left(), padding.top(), newWidth);
		return padding.top() + _content->height() + padding.bottom();
	}

private:
	const not_null<Ui::VerticalLayout*> _content;

};

class UpdatesLogo final : public Ui::RpWidget {
public:
	explicit UpdatesLogo(QWidget *parent) : Ui::RpWidget(parent) {
		const auto size = st::whitegramUpdatesLogoSize;
		resize(size, size);
		setNaturalWidth(size);
		paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(this);
			auto hq = PainterHighQualityEnabler(p);
			const auto size = st::whitegramUpdatesLogoSize;
			const auto outer = QRectF(0, 0, size, size);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBg);
			p.drawEllipse(outer);
			const auto image = AyuAssets::currentAppLogoPad();
			if (!image.isNull()) {
				const auto inner = size - 2 * st::whitegramUpdatesLogoInnerInset;
				const auto scaled = image.scaled(
					inner * style::DevicePixelRatio(),
					inner * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				const auto pos = (size - inner) / 2;
				p.drawImage(QRectF(pos, pos, inner, inner), scaled);
			}
		}, lifetime());
	}

};

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

void BuildWhitegramUpdatesPage(SectionBuilder &builder) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::UpdaterDisabled()) {
		builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
			return {
				.widget = object_ptr<Ui::FlatLabel>(
					ctx.container,
					trWhitegram(
						QString::fromUtf8(
							"Обновления недоступны в этой сборке."),
						QString("Updates are unavailable in this build.")),
					st::centeredBoxLabel),
			};
		});
		return;
	}

	builder.addSkip();
	builder.add([controller = builder.controller()](
			const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto card = Ui::CreateChild<UpdatesHeroCard>(
			ctx.container.get());
		const auto content = card->content();

		content->add(
			object_ptr<UpdatesLogo>(content),
			style::al_center);
		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesLogoSkip));

		const auto versionBadge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(Whitegram::Backend::BetaBadgeText()),
				st::whitegramUpdatesVersionTitle),
			st::whitegramUpdatesBadgePadding);
		versionBadge->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(versionBadge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(
				versionBadge->rect(),
				st::whitegramUpdatesBadgeRadius,
				st::whitegramUpdatesBadgeRadius);
		}, versionBadge->lifetime());
		content->add(
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>::fromRaw(versionBadge),
			style::al_center);
		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesBaseVersionSkip));
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(
					QString::fromLatin1("Telegram Desktop v")
					+ QString::fromLatin1(AppVersionStr)),
				st::whitegramUpdatesBaseVersion),
			style::al_center);
		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesStatusSkip));

		const auto texts = Ui::CreateChild<rpl::event_stream<QString>>(
			content.get());
		const auto status = Ui::CreateChild<Ui::FlatLabel>(
			content,
			texts->events(),
			st::whitegramUpdatesStatus);
		status->setAttribute(Qt::WA_TransparentForMouseEvents);
		content->add(object_ptr<Ui::FlatLabel>::fromRaw(status), style::al_center);

		const auto progressWrap = content->add(
			object_ptr<Ui::SlideWrap<UpdatesProgressBar>>(
				content,
				object_ptr<UpdatesProgressBar>(content)));
		const auto progress = progressWrap->entity();
		progressWrap->show(anim::type::instant);

		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesProgressSkip));

		const auto button = Ui::CreateChild<Ui::RoundButton>(
			content,
			rpl::single(QString()),
			st::whitegramUpdatesActionButton);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		const auto pillRadius = st::whitegramUpdatesActionButton.height / 2;
		button->setCornerRadii(
			pillRadius,
			pillRadius,
			pillRadius,
			pillRadius);
		content->add(object_ptr<Ui::RoundButton>::fromRaw(button), style::al_center);

		const auto refreshButton = [=](const QString &label, bool enabled) {
			button->setText(rpl::single(label));
			button->setEnabled(enabled);
		};
		const auto showStatus = [=](const QString &text) {
			texts->fire_copy(text);
		};
		const auto setProgress = [=](int already, int total) {
			if (total > 0) {
				progress->setProgress(already / float64(total));
			} else {
				progress->setProgress(0.);
			}
		};
		const auto isRu = [] {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		};
		const auto latestText = [=] {
			return isRu()
				? u"У вас установлена последняя версия"_q
				: u"You have the latest version"_q;
		};
		const auto availableText = [=] {
			return isRu()
				? u"Доступно новое обновление"_q
				: u"A new update is available"_q;
		};
		const auto checkingText = [=] {
			return isRu()
				? u"Проверка обновлений..."_q
				: u"Checking for updates..."_q;
		};
		const auto readyText = [=] {
			return isRu()
				? u"Обновление готово к установке"_q
				: u"Update is ready to install"_q;
		};
		const auto updateLabel = [=] {
			return isRu()
				? u"Обновить Whitegram"_q
				: u"Update Whitegram"_q;
		};
		const auto installLabel = [=] {
			return isRu()
				? u"Установить и перезапустить"_q
				: u"Install and restart"_q;
		};
		const auto checkLabel = [=] {
			return isRu()
				? u"Проверить обновления"_q
				: u"Check for updates"_q;
		};
		const auto checkingButtonLabel = [=] {
			return isRu()
				? u"Проверка..."_q
				: u"Checking..."_q;
		};
		const auto applyState = [=] {
			Core::UpdateChecker checker;
			using State = Core::UpdateChecker::State;
			const auto state = checker.state();
			if (state == State::Ready) {
				showStatus(readyText());
				setProgress(0, 0);
				refreshButton(installLabel(), true);
			} else if (state == State::Download) {
				const auto done = checker.already();
				const auto total = checker.size();
				setProgress(done, total);
				if (total > 0) {
					showStatus(Ui::FormatDownloadText(done, total));
				} else {
					showStatus(checkingText());
				}
				refreshButton(updateLabel(), false);
			} else if (Whitegram::Backend::IsUpdateAvailable()) {
				showStatus(availableText());
				setProgress(0, 0);
				refreshButton(updateLabel(), true);
			} else {
				showStatus(latestText());
				setProgress(0, 0);
				refreshButton(checkLabel(), true);
			}
		};

		applyState();
		Whitegram::Backend::FetchClientInfo();
		Whitegram::Backend::CheckUpdateAvailability();

		Core::UpdateChecker checker;
		std::move(checker.checking()) | rpl::on_next([=] {
			showStatus(checkingText());
			setProgress(0, 0);
			refreshButton(checkingButtonLabel(), false);
		}, card->lifetime());
		std::move(checker.isLatest()) | rpl::on_next([=] {
			Whitegram::Backend::RefreshUpdateAvailability();
			applyState();
		}, card->lifetime());
		std::move(checker.failed()) | rpl::on_next([=] {
			applyState();
			controller->showToast(isRu()
				? u"Не удалось проверить обновления"_q
				: u"Failed to check for updates"_q);
		}, card->lifetime());
		std::move(checker.ready()) | rpl::on_next([=] {
			applyState();
		}, card->lifetime());
		std::move(checker.progress()) | rpl::on_next([=](
				Core::UpdateChecker::Progress value) {
			setProgress(value.already, value.size);
			if (value.size > 0) {
				showStatus(Ui::FormatDownloadText(
					value.already,
					value.size));
			}
			refreshButton(updateLabel(), false);
		}, card->lifetime());
		std::move(Whitegram::Backend::updateAvailableValue(
		)) | rpl::on_next([=] {
			applyState();
		}, card->lifetime());

		button->setClickedCallback([=] {
			Core::UpdateChecker checker;
			if (checker.state() == Core::UpdateChecker::State::Ready) {
				Whitegram::Backend::InstallReadyUpdate();
				return;
			}
			showStatus(checkingText());
			setProgress(0, 0);
			refreshButton(checkingButtonLabel(), false);
			Whitegram::Backend::FetchClientInfo();
			Whitegram::Backend::CheckUpdateAvailability();
			Whitegram::Backend::TriggerUpdateDownload();
		});

		content->widthValue() | rpl::on_next([=](int inner) {
			if (inner > 0) {
				button->setFullWidth(inner);
				progress->resize(inner, st::whitegramUpdatesProgressHeight);
			}
		}, card->lifetime());

		return {
			.widget = object_ptr<UpdatesHeroCard>::fromRaw(card),
			.margin = st::whitegramUpdatesCardMargin,
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);
	ayu.addSettingToggle({
		.id = u"whitegram/autoUpdate"_q,
		.title = trWhitegram(
			QString::fromUtf8("Автообновление"),
			QString("Automatic updates")),
		.getter = &AyuSettings::whitegramAutoUpdate,
		.setter = &AyuSettings::setWhitegramAutoUpdate,
	});

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Whitegram будет автоматически проверять и скачивать "
			"обновления при запуске."),
		QString(
			"Whitegram will automatically check and download "
			"updates on startup.")));
	builder.addSkip();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

void BuildWhitegramSelfDestructPage(SectionBuilder &builder) {
	const auto controller = builder.controller();

	// =========================================================================
	// SECTION 1: Локальное самоуничтожение (Whitegram)
	// =========================================================================
	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Локальное самоуничтожение Whitegram"),
		QString("Local Whitegram self-destruct")));
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Удаляет всю папку с Whitegram на этом компьютере — клиент, "
			"кэш, модули, tdata и логи. Ваши аккаунты Telegram останутся "
			"нетронутыми на серверах. Для выполнения требуется пароль."),
		QString(
			"Removes the entire Whitegram folder on this computer — client, "
			"cache, modules, tdata, and logs. Your Telegram accounts remain "
			"untouched on servers. Requires a password to execute.")));
	builder.addSkip();

	builder.addButton({
		.id = u"whitegram/localSelfDestructPassword"_q,
		.title = trWhitegram(
			QString::fromUtf8("Пароль"),
			QString("Password")),
		.icon = { &st::menuIconPermissions },
		.label = rpl::combine(
			Whitegram::SelfDestruct::HasPasswordValue(),
			rpl::single(rpl::empty) | rpl::then(
				Lang::GetInstance().idChanges() | rpl::to_empty)
		) | rpl::map([](bool has, auto) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
			return has
				? (isRu ? QString::fromUtf8("Задан") : QString("Configured"))
				: (isRu ? QString::fromUtf8("Не задан") : QString("Not set"));
		}),
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowSettings(controller);
		},
	});

	builder.addSkip();

	builder.addButton({
		.id = u"whitegram/localSelfDestructExecute"_q,
		.title = trWhitegram(
			QString::fromUtf8("Уничтожить Whitegram"),
			QString("Destroy Whitegram")),
		.st = &st::settingsAttentionButtonWithIcon,
		.icon = { &st::menuIconDeleteAttention },
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowExecuteBox(controller);
		},
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	// =========================================================================
	// SECTION 2: Тотальное уничтожение (Whitegram + Аккаунты Telegram)
	// =========================================================================
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Тотальное уничтожение (Whitegram + Аккаунты)"),
		QString("Total destruction (Whitegram + Accounts)")));
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Экстренный режим: безвозвратно удаляет ВСЕ авторизованные "
			"аккаунты Telegram с серверов (включая каналы, чаты, группы и данные), "
			"после чего стирает Whitegram с ПК.\n\n"
			"Для включения задаётся отдельный пароль и подтверждаются 2FA-пароли аккаунтов."),
		QString(
			"Emergency mode: permanently deletes ALL authorized "
			"Telegram accounts from servers (including channels, chats, groups, data), "
			"then erases Whitegram from this PC.\n\n"
			"Requires a separate password and confirming 2FA passwords for accounts.")));
	builder.addSkip();

	builder.addButton({
		.id = u"whitegram/totalDestructPassword"_q,
		.title = trWhitegram(
			QString::fromUtf8("Пароль"),
			QString("Password")),
		.icon = { &st::menuIconPermissions },
		.label = rpl::combine(
			Whitegram::SelfDestruct::HasTotalDestructPasswordValue(),
			rpl::single(rpl::empty) | rpl::then(
				Lang::GetInstance().idChanges() | rpl::to_empty)
		) | rpl::map([](bool has, auto) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
			return has
				? (isRu ? QString::fromUtf8("Задан") : QString("Configured"))
				: (isRu ? QString::fromUtf8("Не задан") : QString("Not set"));
		}),
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowTotalDestructSettingsBox(controller);
		},
	});

	builder.addSkip();

	builder.addButton({
		.id = u"whitegram/totalDestructExecute"_q,
		.title = trWhitegram(
			QString::fromUtf8("Уничтожить Whitegram и удалить аккаунты"),
			QString("Destroy Whitegram & delete accounts")),
		.st = &st::settingsAttentionButtonWithIcon,
		.icon = { &st::menuIconDeleteAttention },
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowTotalDestructExecuteBox(controller);
		},
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Внимание: удаление аккаунтов Telegram необратимо. Восстановление "
			"каналов, переписок и прав владельца будет невозможно. "
			"Забыли пароль? Удалите папку с Whitegram вручную."),
		QString(
			"Warning: Telegram account deletion is irreversible. Restoring "
			"channels, messages, and ownership will be impossible. "
			"Forgot the password? Delete the Whitegram folder manually.")));
	builder.addSkip();
}

const auto kWhitegramUpdatesMeta = BuildHelper({
	.id = WhitegramUpdates::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Обновление"),
	.icon = &st::menuIconRestore,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	BuildWhitegramUpdatesPage(builder);
	builder.addSkip();
});

const auto kWhitegramGeneralMeta = BuildHelper({
	.id = WhitegramGeneral::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Основное"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				trWhitegram(
					QString::fromUtf8(
						"Обновления, виджеты бокового меню и настройки "
						"нескольких аккаунтов в одном окне."),
					QString(
						"Updates, side menu widgets, and multi-account "
						"profiles in one window.")),
				st::boxDividerLabel),
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Разделы"),
		QString("Sections")));

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Обновление"),
			QString("Updates")),
		.targetSection = WhitegramUpdates::Id(),
		.icon = { &st::menuIconRestore },
		.keywords = {
			u"update"_q,
			u"version"_q,
			QString::fromUtf8("обновление"),
			QString::fromUtf8("версия"),
		},
	});
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Курсы криптовалют"),
			QString("Cryptocurrency Rates")),
		.targetSection = WhitegramCrypto::Id(),
		.icon = { &st::menuIconTradable },
		.keywords = {
			u"crypto"_q,
			u"bitcoin"_q,
			u"gram"_q,
			u"ton"_q,
			QString::fromUtf8("крипто"),
			QString::fromUtf8("курс"),
		},
	});
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Счетчик времени"),
			QString("Time Spent Counter")),
		.targetSection = WhitegramTimeSpent::Id(),
		.icon = { &st::menuIconTimer },
		.keywords = {
			u"time"_q,
			QString::fromUtf8("время"),
			QString::fromUtf8("счётчик"),
		},
	});
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Звук при запуске"),
			QString("Startup Sound")),
		.targetSection = WhitegramStartupSound::Id(),
		.icon = { &st::menuIconSoundOn },
		.keywords = {
			u"sound"_q,
			u"startup"_q,
			QString::fromUtf8("звук"),
			QString::fromUtf8("запуск"),
		},
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Аккаунты"),
		QString("Accounts")));

	auto ayu = AyuSectionBuilder(builder);
	ayu.addSettingToggle({
		.id = u"whitegram/accountProfilesEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Профили аккаунтов"),
			QString("Account Profiles")),
		.getter = &AyuSettings::accountProfilesEnabled,
		.setter = &AyuSettings::setAccountProfilesEnabled,
		.icon = { &st::menuIconAddAccount },
		.keywords = {
			u"profile"_q,
			u"account"_q,
			QString::fromUtf8("профиль"),
			QString::fromUtf8("аккаунт"),
		},
	});

	int maxProfCurrent = AyuSettings::getInstance().maxAccountProfiles();
	int maxProfIndex = 1;
	if (maxProfCurrent == 5) maxProfIndex = 0;
	else if (maxProfCurrent == 10) maxProfIndex = 1;
	else if (maxProfCurrent == 15) maxProfIndex = 2;
	else if (maxProfCurrent == 20) maxProfIndex = 3;
	else if (maxProfCurrent == 30) maxProfIndex = 4;
	else maxProfIndex = 5;

	auto getProfOptions = [] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return std::vector<QString>{
			QString::fromUtf8("5"),
			QString::fromUtf8("10"),
			QString::fromUtf8("15"),
			QString::fromUtf8("20"),
			QString::fromUtf8("30"),
			isRu ? QString::fromUtf8("Своё значение...") : QString("Custom value..."),
		};
	};

	ayu.addChooseButton({
		.id = u"whitegram/maxAccountProfiles"_q,
		.title = trWhitegram(
			QString::fromUtf8("Максимум профилей"),
			QString("Max Account Profiles")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Максимум профилей"),
			QString("Max Account Profiles")),
		.initialSelection = maxProfIndex,
		.options = getProfOptions(),
		.setter = [=](int index) {
			if (index == 5) {
				const auto controller = builder.controller();
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(trWhitegram(
						QString::fromUtf8("Максимум профилей"),
						QString("Max Account Profiles")));
					const auto field = box->addRow(object_ptr<Ui::InputField>(
						box,
						st::defaultInputField,
						trWhitegram(
							QString::fromUtf8("Число профилей (от 1 до 50)"),
							QString("Number of profiles (1 to 50)")),
						QString::number(AyuSettings::getInstance().maxAccountProfiles())));
					box->addButton(tr::lng_box_ok(), [=] {
						const auto text = field->getLastText().trimmed();
						bool ok = false;
						const int val = text.toInt(&ok);
						if (ok && val > 0) {
							AyuSettings::getInstance().setMaxAccountProfiles(val);
						}
						box->closeBox();
					});
					box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
				}));
			} else {
				const int vals[] = { 5, 10, 15, 20, 30 };
				AyuSettings::getInstance().setMaxAccountProfiles(vals[index]);
			}
		},
		.icon = { &st::menuIconManage },
		.shown = AyuSettings::getInstance().enableAccountProfilesValue(),
	});

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Профили позволяют держать несколько аккаунтов в одном окне "
			"и быстро переключаться между ними. Лимит — максимальное число "
			"профилей, которые можно создать."),
		QString(
			"Profiles let you keep multiple accounts in one window and "
			"switch between them quickly. The limit is the maximum number "
			"of profiles you can create.")));

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Приватность"),
		QString("Privacy")));

	ayu.addSettingToggle({
		.id = u"whitegram/hideTelegramOnViewOnce"_q,
		.title = trWhitegram(
			QString::fromUtf8("Скрывать Telegram в режиме стримера при просмотре одноразового контента"),
			QString("Hide Telegram like streamer mode while viewing view-once media")),
		.getter = &AyuSettings::hideTelegramOnViewOnce,
		.setter = &AyuSettings::setHideTelegramOnViewOnce,
		.icon = { &st::ayuStreamerModeMenuIcon },
		.keywords = {
			u"streamer"_q,
			u"view-once"_q,
			QString::fromUtf8("стример"),
			QString::fromUtf8("одноразовые"),
			QString::fromUtf8("скрыть"),
		},
	});

	{
		const auto controller = builder.controller();
		builder.addButton({
			.id = u"whitegram/hidePhoneNumber"_q,
			.title = trWhitegram(
				QString::fromUtf8("Скрывать номер телефона в моём профиле"),
				QString("Hide phone number in my profile")),
			.icon = { &st::menuIconPhone },
			.toggled = builder.session()->settings().phoneNumberHiddenValue(),
			.onClick = [=] {
				auto &settings = controller->session().settings();
				settings.setPhoneNumberHidden(
					!settings.phoneNumberHidden());
				controller->session().saveSettingsDelayed();
			},
			.keywords = {
				u"phone"_q,
				u"spoiler"_q,
				QString::fromUtf8("номер"),
				QString::fromUtf8("телефон"),
				QString::fromUtf8("скрыть"),
				QString::fromUtf8("спойлер"),
			},
		});
	}

	{
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		const auto revealOptions = std::vector{
			QString(isRu
				? QString::fromUtf8("При клике (как в Telegram)")
				: QString("On click (like Telegram)")),
			QString(isRu
				? QString::fromUtf8("При наведении")
				: QString("On hover")),
		};
		ayu.addChooseButton({
			.id = u"whitegram/phoneRevealMode"_q,
			.title = trWhitegram(
				QString::fromUtf8("Режим раскрытия номера"),
				QString("Phone reveal mode")),
			.boxTitle = trWhitegram(
				QString::fromUtf8("Режим раскрытия номера"),
				QString("Phone reveal mode")),
			.initialSelection = static_cast<int>(
				AyuSettings::getInstance().phoneRevealMode()),
			.options = revealOptions,
			.setter = [](int i) {
				AyuSettings::getInstance().setPhoneRevealMode(
					static_cast<PhoneRevealMode>(i));
			},
			.icon = { &st::menuIconPhone },
		});
	}

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Номер скрывается спойлером, как в Telegram: включить его можно "
			"и правым кликом по номеру в профиле. Режим «При наведении» — "
			"расширение Whitegram: номер показывается, пока курсор над ним."),
		QString(
			"The number is hidden with a spoiler, like in Telegram: it can "
			"also be toggled by right-clicking the number in the profile. "
			"The 'On hover' mode is a Whitegram extension: the number shows "
			"while the cursor is over it.")));
	builder.addSkip();
});

void BuildWhitegramStartupSound(SectionBuilder &builder) {
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);
	ayu.addSettingToggle({
		.id = u"whitegram/startupSoundEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Включить звук при запуске"),
			QString("Play sound on startup")),
		.getter = &AyuSettings::startupSoundEnabled,
		.setter = &AyuSettings::setStartupSoundEnabled,
		.icon = { &st::menuIconSoundOn },
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				trWhitegram(
					QString::fromUtf8(
						"Короткий звук при запуске Whitegram. Можно выбрать "
						"свой файл (WAV, MP3, OGG, M4A) или оставить "
						"стандартный звук Telegram."),
					QString(
						"A short sound when Whitegram starts. Pick your own "
						"file (WAV, MP3, OGG, M4A) or keep the default "
						"Telegram sound.")),
				st::boxDividerLabel),
		};
	});

	builder.addSkip();

	const auto formatCurrentSound = [](const QString &storedPath) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		if (storedPath.isEmpty()) {
			return isRu
				? QString::fromUtf8("Стандартный звук Telegram")
				: QString("Default Telegram sound");
		}
		return QFileInfo(storedPath).fileName();
	};

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Файл"),
		QString("Sound file")));

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			ctx.container.get(),
			rpl::single(QString()),
			st::boxLabel);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		label->setTextColorOverride(st::windowActiveTextFg->c);

		AyuSettings::getInstance().startupSoundPathValue(
		) | rpl::on_next([=](const QString &path) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			const auto missing = !path.isEmpty() && !QFileInfo::exists(path);
			label->setText(formatCurrentSound(path));
			label->setTextColorOverride(missing
				? st::boxTextFgError->c
				: st::windowActiveTextFg->c);
			if (missing || !path.isEmpty()) {
				label->setToolTip(path);
			} else {
				label->setToolTip(isRu
					? QString::fromUtf8("Встроенный звук Telegram")
					: QString("Built-in Telegram sound"));
			}
		}, label->lifetime());

		return {
			.widget = object_ptr<Ui::FlatLabel>::fromRaw(label),
			.margin = st::boxRowPadding,
		};
	});

	builder.addButton({
		.id = u"whitegram/startupSoundChoose"_q,
		.title = trWhitegram(
			QString::fromUtf8("Выбрать файл…"),
			QString("Choose file…")),
		.icon = { &st::menuIconFile },
		.onClick = [=] {
			const auto filters = u"Audio files (*.wav *.mp3 *.ogg *.m4a);;"_q
				+ FileDialog::AllFilesFilter();
			FileDialog::GetOpenPath(
				Core::App().getFileDialogParent(),
				u"Open audio file"_q,
				filters,
				[=](const FileDialog::OpenResult &result) {
					if (result.paths.isEmpty()) {
						return;
					}
					auto &settings = AyuSettings::getInstance();
					settings.setStartupSoundPath(result.paths.front());
					if (!settings.startupSoundEnabled()) {
						settings.setStartupSoundEnabled(true);
					}
				});
		},
	});

	builder.addButton({
		.id = u"whitegram/startupSoundPreview"_q,
		.title = trWhitegram(
			QString::fromUtf8("Прослушать"),
			QString("Preview")),
		.icon = { &st::menuIconSoundOn },
		.onClick = [=] {
			AyuSettings::getInstance().previewStartupSound();
		},
	});

	builder.addButton({
		.id = u"whitegram/startupSoundStop"_q,
		.title = trWhitegram(
			QString::fromUtf8("Остановить"),
			QString("Stop")),
		.icon = { &st::menuIconCancel },
		.onClick = [=] {
			AyuSettings::getInstance().stopStartupSound();
		},
	});

	builder.addButton({
		.id = u"whitegram/startupSoundReset"_q,
		.title = trWhitegram(
			QString::fromUtf8("Сбросить на стандартный"),
			QString("Reset to default")),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			AyuSettings::getInstance().setStartupSoundPath(QString());
		},
	});

	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Громкость"),
		QString("Volume")));

	ayu.addSlider({
		.id = u"whitegram/startupSoundVolume"_q,
		.title = trWhitegram(
			QString::fromUtf8("Громкость"),
			QString("Volume")),
		.showTitle = false,
		.steps = 21,
		.current = AyuSettings::getInstance().startupSoundVolume(),
		.indexToValue = [](int index) { return index * 5; },
		.onFinalChanged = [](int value) {
			AyuSettings::getInstance().setStartupSoundVolume(value);
		},
		.formatLabel = [](int value) {
			return QString::number(value) + u'%';
		},
		.keywords = { u"volume"_q, u"sound"_q, u"loud"_q },
	});

	builder.addSkip();
}

const auto kWhitegramStartupSoundMeta = BuildHelper({
	.id = WhitegramStartupSound::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Звук при запуске"),
	.icon = &st::menuIconSoundOn,
}, BuildWhitegramStartupSound);

const auto kWhitegramStartupAnimMeta = BuildHelper({
	.id = WhitegramStartupAnim::Id(),
	.parentId = WhitegramAppearance::Id(),
	.title = QString::fromUtf8("Анимация при входе"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);

	ayu.addSettingToggle({
		.id = u"whitegram/startupAnimationEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Включить анимацию при запуске"),
			QString("Enable Startup Animation")),
		.getter = &AyuSettings::startupAnimationEnabled,
		.setter = &AyuSettings::setStartupAnimationEnabled,
	});

	ayu.addSettingToggle({
		.id = u"whitegram/startupAnimationShowIcon"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать иконку"),
			QString("Show Icon")),
		.getter = &AyuSettings::startupAnimationShowIcon,
		.setter = &AyuSettings::setStartupAnimationShowIcon,
	});

	builder.addSkip();

	auto getAnimOptions = [] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return std::vector<QString>{
			QString("Steam Big Picture"),
			isRu ? QString::fromUtf8("Минималистичный тёмный") : QString("Minimalist Dark"),
			isRu ? QString::fromUtf8("Киберпанк сетка") : QString("Cyberpunk Grid"),
			isRu ? QString::fromUtf8("Закат") : QString("Sunset"),
			isRu ? QString::fromUtf8("Космическая туманность") : QString("Space Nebula"),
		};
	};

	ayu.addChooseButton({
		.id = u"whitegram/startupAnimationType"_q,
		.title = trWhitegram(
			QString::fromUtf8("Стиль анимации"),
			QString("Animation Style")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Стиль анимации"),
			QString("Animation Style")),
		.initialSelection = AyuSettings::getInstance().startupAnimationType(),
		.options = getAnimOptions(),
		.setter = [](int index) {
			AyuSettings::getInstance().setStartupAnimationType(index);
		},
	});

	builder.addSkip();

	int currentFps = AyuSettings::getInstance().startupAnimationFps();
	int fpsIndex = 4;
	if (currentFps == 60) fpsIndex = 0;
	else if (currentFps == 90) fpsIndex = 1;
	else if (currentFps == 120) fpsIndex = 2;
	else if (currentFps == 165) fpsIndex = 3;
	else if (currentFps == 240) fpsIndex = 4;
	else fpsIndex = 5;

	auto getFpsOptions = [] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return std::vector<QString>{
			QString("60 FPS"),
			QString("90 FPS"),
			QString("120 FPS"),
			QString("165 FPS"),
			QString("240 FPS"),
			isRu ? QString::fromUtf8("Своё значение...") : QString("Custom value..."),
		};
	};

	ayu.addChooseButton({
		.id = u"whitegram/startupAnimationFps"_q,
		.title = trWhitegram(
			QString::fromUtf8("Частота кадров (FPS)"),
			QString("Frame Rate (FPS)")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Частота кадров (FPS)"),
			QString("Frame Rate (FPS)")),
		.initialSelection = fpsIndex,
		.options = getFpsOptions(),
		.setter = [=](int index) {
			if (index == 5) {
				const auto controller = builder.controller();
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(trWhitegram(
						QString::fromUtf8("Частота кадров (FPS)"),
						QString("Frame Rate (FPS)")));
					const auto field = box->addRow(object_ptr<Ui::InputField>(
						box,
						st::defaultInputField,
						trWhitegram(
							QString::fromUtf8("Значение FPS (от 15 до 360)"),
							QString("FPS Value (15 to 360)")),
						QString::number(AyuSettings::getInstance().startupAnimationFps())));
					box->addButton(tr::lng_box_ok(), [=] {
						const auto text = field->getLastText().trimmed();
						bool ok = false;
						const int val = text.toInt(&ok);
						if (ok && val >= 15 && val <= 360) {
							AyuSettings::getInstance().setStartupAnimationFps(val);
						}
						box->closeBox();
					});
					box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
				}));
			} else {
				static const int fpsValues[] = { 60, 90, 120, 165, 240 };
				if (index >= 0 && index < 5) {
					AyuSettings::getInstance().setStartupAnimationFps(fpsValues[index]);
				}
			}
		},
	});

	builder.addSkip();
});

[[nodiscard]] QString ArchiveRowTitle(const CustomArchive &archive) {
	auto title = archive.name;
	if (!archive.icon.isEmpty()) {
		if (archive.icon.endsWith(u"home"_q)) title = u"🏠 "_q + title;
		else if (archive.icon.endsWith(u"work"_q)) title = u"💼 "_q + title;
		else if (archive.icon.endsWith(u"game"_q)) title = u"🎮 "_q + title;
		else if (archive.icon.endsWith(u"favorite"_q)) title = u"⭐ "_q + title;
		else if (archive.icon.endsWith(u"love"_q)) title = u"❤️ "_q + title;
		else if (archive.icon.endsWith(u"private"_q)) title = u"🔒 "_q + title;
		else title = u"📁 "_q + title;
	}
	if (archive.isLocked()) {
		title = u"🔐 "_q + title;
	}
	return title;
}

[[nodiscard]] QString ArchiveRowLabel(const CustomArchive &archive) {
	const auto count = int(archive.peerIds.size());
	auto parts = QStringList();
	parts.push_back(QString::number(count));
	if (archive.hasPin() && archive.hasPassword()) {
		parts.push_back(QString::fromUtf8("PIN + пароль"));
	} else if (archive.hasPin()) {
		parts.push_back(u"PIN"_q);
	} else 	if (archive.hasPassword()) {
		parts.push_back(QString::fromUtf8("пароль"));
	}
	return parts.join(u" · "_q);
}

void BuildArchivesList(SectionBuilder &builder) {
	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			ctx.container,
			object_ptr<Ui::VerticalLayout>(ctx.container));
		const auto slide = wrap.data();
		const auto layout = slide->entity();
		const auto controller = ctx.controller;

		const auto rebuild = [=] {
			while (layout->count() > 0) {
				delete layout->widgetAt(0);
			}

			const auto enabled = AyuSettings::getInstance().enableCustomArchives();
			slide->toggle(enabled, anim::type::normal);
			if (!enabled) {
				layout->resizeToWidth(layout->width());
				return;
			}

			auto archives = AyuSettings::getInstance().customArchives();
			std::sort(archives.begin(), archives.end(), [](
					const CustomArchive &a,
					const CustomArchive &b) {
				if (a.order != b.order) {
					return a.order < b.order;
				}
				return a.id < b.id;
			});

			const auto create = AddButtonWithIcon(
				layout,
				trWhitegram(
					QString::fromUtf8("Создать папку"),
					QString("Create folder")),
				st::settingsButton,
				{ &st::menuIconAddToFolder });
			create->setClickedCallback([=] {
				controller->show(Box(EditCustomArchiveBox, controller, -1));
			});

			if (archives.empty()) {
				layout->add(
					object_ptr<Ui::FlatLabel>(
						layout,
						trWhitegram(
							QString::fromUtf8(
								"Пока нет папок. Создайте первую — "
								"можно сразу поставить PIN или пароль."),
							QString(
								"No folders yet. Create the first one — "
								"you can set a PIN or password right away.")),
						st::boxDividerLabel),
					st::boxRowPadding);
			}

			for (const auto &archive : archives) {
				const auto archiveId = archive.id;
				const auto button = AddButtonWithLabel(
					layout,
					rpl::single(ArchiveRowTitle(archive)),
					rpl::single(ArchiveRowLabel(archive)),
					st::settingsButton,
					{ &st::menuIconArchive });
				button->setClickedCallback([=] {
					AyuFeatures::CustomArchives::OpenArchive(
						controller,
						archiveId);
				});
			}

			const auto manage = AddButtonWithIcon(
				layout,
				trWhitegram(
					QString::fromUtf8("Управление папками"),
					QString("Manage folders")),
				st::settingsButton,
				{ &st::menuIconSettings });
			manage->setClickedCallback([=] {
				controller->show(Box(CustomArchivesBox, controller));
			});

			layout->resizeToWidth(layout->width());
		};

		rebuild();
		rpl::merge(
			AyuSettings::getInstance().enableCustomArchivesChanges(
			) | rpl::to_empty,
			AyuSettings::getInstance().customArchivesChanges()
		) | rpl::on_next(rebuild, layout->lifetime());

		return { .widget = std::move(wrap) };
	});
}

const auto kWhitegramArchivesMeta = BuildHelper({
	.id = WhitegramArchives::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Папки чатов"),
	.icon = &st::menuIconArchive,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				trWhitegram(
					QString::fromUtf8(
						"Свои папки в списке чатов — отдельно от обычного "
						"архива Telegram. Чаты можно скрыть из основного "
						"списка и защитить PIN или паролем."),
					QString(
						"Your own folders in the chat list — separate from "
						"Telegram’s archive. Hide chats from the main list "
						"and protect folders with a PIN or password.")),
				st::boxDividerLabel),
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Основное"),
		QString("General")));

	auto ayu = AyuSectionBuilder(builder);
	ayu.addSettingToggle({
		.id = u"whitegram/customArchivesEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Папки чатов"),
			QString("Chat folders")),
		.getter = &AyuSettings::customArchivesEnabled,
		.setter = &AyuSettings::setCustomArchivesEnabled,
		.icon = { &st::menuIconArchive },
	});

	ayu.addSettingToggle({
		.id = u"whitegram/confirmFolderChanges"_q,
		.title = trWhitegram(
			QString::fromUtf8("Сохранять папки вручную"),
			QString("Save folder edits manually")),
		.getter = &AyuSettings::confirmFolderChanges,
		.setter = &AyuSettings::setConfirmFolderChanges,
		.icon = { &st::menuIconEdit },
	});

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"«Сохранять папки вручную» — правки применяются после "
			"«Сохранить». Если выключить, изменения входят в силу сразу "
			"и кнопки «Сохранить» не будет.\n\n"
			"Если выключить сами папки, они исчезнут из списка чатов. "
			"Настройки сохранятся. Архив Telegram не меняется."),
		QString(
			"“Save folder edits manually” applies changes after Save. "
			"If disabled, edits apply immediately and Save is hidden.\n\n"
			"If Chat folders is disabled, they disappear from the list. "
			"Settings are kept. Telegram’s archive is unchanged.")));

	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Ваши папки"),
		QString("Your folders")));

	BuildArchivesList(builder);

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Совет: ПКМ по чату → «Папки». Папки видны вверху списка чатов."),
		QString(
			"Tip: right-click a chat → Folders. Folders appear at the top "
			"of the chat list.")));
	builder.addSkip();
});

const auto kWhitegramAppearanceMeta = BuildHelper({
	.id = WhitegramAppearance::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Оформление"),
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Иконка приложения"),
		QString("Application icon")));
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(
				ctx.container,
				WhitegramAppIcons()),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Нажмите на иконку, чтобы поставить её приложению, "
			"на панели задач и в трее."),
		QString(
			"Tap an icon to apply it to the app, the taskbar and the tray.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Боковое меню"),
		QString("Side menu")));
	auto ayu = AyuSectionBuilder(builder);
	ayu.addSettingToggle({
		.id = u"whitegram/showSendBotInDrawer"_q,
		.title = trWhitegram(
			QString::fromUtf8("CryptoBot (@send)"),
			QString("CryptoBot (@send)")),
		.getter = &AyuSettings::showSendBotInDrawer,
		.setter = &AyuSettings::setShowSendBotInDrawer,
		.icon = { &st::menuIconSend },
	});
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Добавляет кнопку CryptoBot (@send) в боковое меню "
			"(t.me/send). Это не Telegram Wallet (@wallet)."),
		QString(
			"Adds the CryptoBot (@send) button to the side menu "
			"(t.me/send). This is not Telegram Wallet (@wallet).")));
	builder.addSkip();

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Анимация при входе"),
			QString("Startup Animation")),
		.targetSection = WhitegramStartupAnim::Id(),
		.icon = { &st::menuIconShowAll },
	});
	// "Фон профиля" lives on the "My Profile" page (settings_information.cpp),
	// next to the photo and the bio, so it is not listed here.
});

const auto kWhitegramProfileBackgroundMeta = BuildHelper({
	.id = WhitegramProfileBackground::Id(),
	.parentId = WhitegramAppearance::Id(),
	.title = QString::fromUtf8("Фон профиля"),
	.icon = &st::menuIconPhoto,
}, [](SectionBuilder &builder) {
	namespace Background = AyuFeatures::CustomProfileBackground;
	using Zone = Background::Zone;
	auto ayu = AyuSectionBuilder(builder);
	const auto peerId = builder.session()->userPeerId().value;
	const auto controller = builder.controller();

	builder.addSkip();
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				trWhitegram(
					QString::fromUtf8(
						"Свой фон для твоего профиля: шапка, весь профиль "
						"или блок «Информация». Поддерживаются фото и видео."),
					QString(
						"Custom background for your profile: header, full "
						"profile or the info block. Photos and videos "
						"are supported.")),
				st::boxDividerLabel),
		};
	});
	builder.addSkip();

	auto addZone = [&](
			Zone zone,
			const QString &titleRu,
			const QString &titleEn,
			const QString &hintRu,
			const QString &hintEn) {
		const auto isRu = Lang::GetInstance().id().startsWith(u"ru"_q)
			|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
		builder.add([&](const WidgetContext &ctx)
				-> SectionBuilder::WidgetToAdd {
			return {
				.widget = object_ptr<ProfileBackgroundCard>(
					ctx.container,
					isRu ? titleRu : titleEn,
					isRu ? hintRu : hintEn,
					peerId,
					zone,
					[=] { Background::PickBackground(controller, peerId, zone); },
					[=] { Background::RemoveBackground(peerId, zone); }),
			};
		});
		builder.add(nullptr, [titleEn] {
			return SearchEntry{
				.id = u"whitegram/profileBackground/"_q,
				.title = titleEn,
				.keywords = { u"background"_q, u"profile"_q },
			};
		});
	};

	addZone(
		Zone::Top,
		QString::fromUtf8("Фон шапки"),
		QString("Header background"),
		QString::fromUtf8(
			"Показывает только верхнюю часть профиля: аватар, имя и статус."),
		QString(
			"Covers only the top of the profile: avatar, name and status."));
	addZone(
		Zone::Full,
		QString::fromUtf8("Фон всего профиля"),
		QString("Full profile background"),
		QString::fromUtf8(
			"Покрывает весь профиль — от шапки до конца прокрутки. "
			"Страница «Истории» остаётся без изменений."),
		QString(
			"Covers the whole profile, from the header down to the end of "
			"the scroll. The stories page is not affected."));
	addZone(
		Zone::Bottom,
		QString::fromUtf8("Фон блока «Информация»"),
		QString("Info section background"),
		QString::fromUtf8(
			"Покрывает только поля с телефоном, username и ID. "
			"Не применяется, если задан фон всего профиля."),
		QString(
			"Covers only the phone, username and ID fields. Not used when "
			"the full profile background is set."));

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Оформление"),
		QString("Look")));

	ayu.addSlider({
		.id = u"whitegram/profileBackgroundDim"_q,
		.title = trWhitegram(
			QString::fromUtf8("Затемнение фона"),
			QString("Background dimming")),
		.steps = 101,
		.current = AyuSettings::getInstance().profileBackgroundDim(),
		.indexToValue = [](int index) { return index; },
		.onChanged = [](int value) {
			AyuSettings::getInstance().setProfileBackgroundDim(value);
		},
		.onFinalChanged = [](int value) {
			AyuSettings::getInstance().setProfileBackgroundDim(value);
		},
		.formatLabel = [](int value) {
			return QString::number(value) + u'%';
		},
		.keywords = { u"dim"_q, QString::fromUtf8("затемнение") },
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Затемнение помогает сохранить читаемость текста на светлых "
			"фонах. 100% — как задумано, 0% — без затемнения. Другие "
			"пользователи Whitegram тоже увидят эти фоны. Лучше всего "
			"подходят вертикальные изображения вроде 1080×2800."),
		QString(
			"Dimming keeps text readable on bright backgrounds. 100% is "
			"the designed look, 0% is no dimming. Other Whitegram users "
			"will see these backgrounds too. Vertical images like "
			"1080×2800 work best.")));
	builder.addSkip();
});
[[nodiscard]] std::vector<QString> MusicIslandExpandModeOptions() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	return std::vector<QString>{
		isRu ? QString::fromUtf8("При наведении") : QString("On hover"),
		isRu ? QString::fromUtf8("По клику") : QString("On click"),
		isRu ? QString::fromUtf8("Наведение и клик") : QString("Hover and click"),
	};
}

[[nodiscard]] std::vector<QString> MusicLyricsAnimationOptions() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	return std::vector<QString>{
		isRu ? QString::fromUtf8("Караоке-заливка") : QString("Karaoke fill"),
		isRu ? QString::fromUtf8("Мягкий fade + glow") : QString("Soft fade + glow"),
		isRu ? QString::fromUtf8("Пульс в бит") : QString("Beat pulse"),
	};
}

[[nodiscard]] std::vector<QString> MusicIslandCornerOptions() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	return std::vector<QString>{
		isRu ? QString::fromUtf8("Сверху слева") : QString("Top left"),
		isRu ? QString::fromUtf8("Сверху справа") : QString("Top right"),
		isRu ? QString::fromUtf8("Снизу слева") : QString("Bottom left"),
		isRu ? QString::fromUtf8("Снизу справа") : QString("Bottom right"),
		isRu ? QString::fromUtf8("Свободное перемещение") : QString("Free movement"),
	};
}

[[nodiscard]] std::vector<QString> MusicIslandBackgroundModeOptions() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	return std::vector<QString>{
		isRu ? QString::fromUtf8("Заполнить") : QString("Fill"),
		isRu ? QString::fromUtf8("Вписать") : QString("Fit"),
		isRu ? QString::fromUtf8("Растянуть") : QString("Stretch"),
	};
}

[[nodiscard]] QString MusicIslandBackgroundCurrentText(const QString &path) {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	if (path.isEmpty()) {
		return isRu
			? QString::fromUtf8("Фон не выбран")
			: QString("No background selected");
	}
	if (!QFileInfo::exists(path)) {
		return isRu
			? QString::fromUtf8("Файл не найден")
			: QString("File not found");
	}
	return QFileInfo(path).fileName();
}

const auto kWhitegramMusicIslandMeta = BuildHelper({
	.id = WhitegramMusicIsland::Id(),
	.parentId = MainId(),
	.title = QString::fromUtf8("Музыкальный островок"),
	.icon = &st::menuIconSoundOn,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);
	const auto settings = &AyuSettings::getInstance();

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				trWhitegram(
					QString::fromUtf8(
						"Плавающий плеер с обложкой, эквалайзером и "
						"управлением прямо поверх чата."),
					QString(
						"A floating player with cover art, an equalizer, "
						"and controls right over the chat.")),
				st::boxDividerLabel),
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Основное"),
		QString("General")));

	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать островок"),
			QString("Show the island")),
		.getter = &AyuSettings::musicIslandEnabled,
		.setter = &AyuSettings::setMusicIslandEnabled,
		.icon = { &st::menuIconSoundOn },
		.keywords = {
			u"music"_q,
			u"island"_q,
			u"player"_q,
			QString::fromUtf8("музыка"),
			QString::fromUtf8("островок"),
			QString::fromUtf8("плеер"),
		},
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Островок появляется, когда играет музыка. В свёрнутом виде "
			"это компактная пилюля; при наведении или клике она "
			"раскрывается в полноценный плеер."),
		QString(
			"The island appears while music is playing. Collapsed it is a "
			"compact pill; on hover or click it expands into a full "
			"player.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Расположение"),
		QString("Placement")));

	ayu.addChooseButton({
		.id = u"whitegram/musicIslandPosition"_q,
		.title = trWhitegram(
			QString::fromUtf8("Позиция островка"),
			QString("Island position")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Позиция островка"),
			QString("Island position")),
		.initialSelection = ([&] {
			if (settings->musicIslandPosition() == 4) {
				return 4;
			}
			const auto x = settings->musicIslandFreeX();
			const auto y = settings->musicIslandFreeY();
			if (x <= 50 && y <= 50) return 0;
			if (x >= 950 && y <= 50) return 1;
			if (x <= 50 && y >= 950) return 2;
			if (x >= 950 && y >= 950) return 3;
			return 4;
		})(),
		.options = MusicIslandCornerOptions(),
		.setter = [](int index) {
			auto &islandSettings = AyuSettings::getInstance();
			if (index == 4) {
				islandSettings.setMusicIslandPosition(4);
				islandSettings.setMusicIslandLocked(false);
				return;
			}
			islandSettings.setMusicIslandPosition(index);
			islandSettings.setMusicIslandFreeX(
				(index == 0 || index == 2) ? 0 : 1000);
			islandSettings.setMusicIslandFreeY(
				(index < 2) ? 0 : 1000);
		},
		.icon = { &st::menuIconPin },
		.keywords = {
			u"corner"_q,
			u"position"_q,
			u"drag"_q,
			u"free"_q,
			QString::fromUtf8("угол"),
			QString::fromUtf8("положение"),
			QString::fromUtf8("перетаскивание"),
			QString::fromUtf8("свободное"),
		},
	});

	builder.addButton({
		.id = u"whitegram/musicIslandFreeMovement"_q,
		.title = trWhitegram(
			QString::fromUtf8("Свободное перемещение"),
			QString("Free movement")),
		.icon = { &st::menuIconUnpin },
		.onClick = [=] {
			auto &islandSettings = AyuSettings::getInstance();
			islandSettings.setMusicIslandPosition(4);
			islandSettings.setMusicIslandLocked(false);
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
			Ui::Toast::Show(isRu
				? QString::fromUtf8("Свободное перемещение включено — теперь островок можно перетаскивать")
				: QString("Free movement enabled — you can now drag the island"));
		},
	});

	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandLocked"_q,
		.title = trWhitegram(
			QString::fromUtf8("Зафиксировать позицию"),
			QString("Lock the position")),
		.getter = &AyuSettings::musicIslandLocked,
		.setter = &AyuSettings::setMusicIslandLocked,
		.icon = { &st::menuIconPin },
		.keywords = {
			u"lock"_q,
			u"pin"_q,
			u"fixed"_q,
			QString::fromUtf8("закрепить"),
			QString::fromUtf8("позиция"),
		},
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Островок можно перетащить в любую точку окна и опустить — "
			"место запомнится. «Зафиксировать позицию» запрещает "
			"случайное перемещение перетаскиванием, пока включено."),
		QString(
			"Drag the island anywhere in the window and drop it — the "
			"spot is remembered. 'Lock the position' prevents accidental "
			"dragging while it stays on.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Фон островка"),
		QString("Island background")));

	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandCustomBackgroundEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Свой фон картинкой"),
			QString("Custom image background")),
		.getter = &AyuSettings::musicIslandCustomBackgroundEnabled,
		.setter = &AyuSettings::setMusicIslandCustomBackgroundEnabled,
		.icon = { &st::menuIconPhoto },
		.keywords = {
			u"background"_q,
			u"image"_q,
			u"photo"_q,
			QString::fromUtf8("фон"),
			QString::fromUtf8("картинка"),
			QString::fromUtf8("фото"),
		},
	});

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			ctx.container.get(),
			rpl::single(QString()),
			st::boxLabel);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		label->setTextColorOverride(st::windowActiveTextFg->c);

		AyuSettings::getInstance().musicIslandCustomBackgroundPathValue(
		) | rpl::on_next([=](const QString &path) {
			const auto missing = !path.isEmpty() && !QFileInfo::exists(path);
			label->setText(MusicIslandBackgroundCurrentText(path));
			label->setTextColorOverride(missing
				? st::boxTextFgError->c
				: st::windowActiveTextFg->c);
			label->setToolTip(path);
		}, label->lifetime());

		return {
			.widget = object_ptr<Ui::FlatLabel>::fromRaw(label),
			.margin = st::boxRowPadding,
		};
	});

	builder.addButton({
		.id = u"whitegram/musicIslandCustomBackgroundChoose"_q,
		.title = trWhitegram(
			QString::fromUtf8("Выбрать картинку…"),
			QString("Choose image…")),
		.icon = { &st::menuIconFile },
		.onClick = [=] {
			const auto filters = u"Image files (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;"_q
				+ FileDialog::AllFilesFilter();
			FileDialog::GetOpenPath(
				Core::App().getFileDialogParent(),
				u"Open image file"_q,
				filters,
				[=](const FileDialog::OpenResult &result) {
					if (result.paths.isEmpty()) {
						return;
					}
					auto &backgroundSettings = AyuSettings::getInstance();
					backgroundSettings.setMusicIslandCustomBackgroundPath(
						result.paths.front());
					if (!backgroundSettings.musicIslandCustomBackgroundEnabled()) {
						backgroundSettings.setMusicIslandCustomBackgroundEnabled(true);
					}
				});
		},
	});

	builder.addButton({
		.id = u"whitegram/musicIslandCustomBackgroundReset"_q,
		.title = trWhitegram(
			QString::fromUtf8("Убрать свой фон"),
			QString("Remove custom background")),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			auto &backgroundSettings = AyuSettings::getInstance();
			backgroundSettings.setMusicIslandCustomBackgroundPath(QString());
			backgroundSettings.setMusicIslandCustomBackgroundEnabled(false);
		},
	});

	ayu.addChooseButton({
		.id = u"whitegram/musicIslandCustomBackgroundMode"_q,
		.title = trWhitegram(
			QString::fromUtf8("Заполнение фоном"),
			QString("Background fit")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Заполнение фоном"),
			QString("Background fit")),
		.initialSelection = settings->musicIslandCustomBackgroundMode(),
		.options = MusicIslandBackgroundModeOptions(),
		.setter = [](int index) {
			AyuSettings::getInstance().setMusicIslandCustomBackgroundMode(index);
		},
		.icon = { &st::menuIconPhoto },
		.keywords = {
			u"fit"_q,
			u"fill"_q,
			u"stretch"_q,
			QString::fromUtf8("заполнение"),
			QString::fromUtf8("вписать"),
			QString::fromUtf8("растянуть"),
		},
	});

	ayu.addSlider({
		.id = u"whitegram/musicIslandCustomBackgroundOpacity"_q,
		.title = trWhitegram(
			QString::fromUtf8("Прозрачность фона"),
			QString("Background opacity")),
		.steps = 101,
		.current = settings->musicIslandCustomBackgroundOpacity(),
		.indexToValue = [](int index) { return index; },
		.onFinalChanged = [](int value) {
			AyuSettings::getInstance().setMusicIslandCustomBackgroundOpacity(value);
		},
		.formatLabel = [](int value) {
			return QString::number(value) + u'%';
		},
		.keywords = { u"opacity"_q, QString::fromUtf8("прозрачность") },
	});

	ayu.addSlider({
		.id = u"whitegram/musicIslandCustomBackgroundDim"_q,
		.title = trWhitegram(
			QString::fromUtf8("Затемнение фона"),
			QString("Background dimming")),
		.steps = 101,
		.current = settings->musicIslandCustomBackgroundDim(),
		.indexToValue = [](int index) { return index; },
		.onFinalChanged = [](int value) {
			AyuSettings::getInstance().setMusicIslandCustomBackgroundDim(value);
		},
		.formatLabel = [](int value) {
			return QString::number(value) + u'%';
		},
		.keywords = { u"dim"_q, QString::fromUtf8("затемнение") },
	});

	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandCustomBackgroundBlur"_q,
		.title = trWhitegram(
			QString::fromUtf8("Размыть фон"),
			QString("Blur background")),
		.getter = &AyuSettings::musicIslandCustomBackgroundBlur,
		.setter = &AyuSettings::setMusicIslandCustomBackgroundBlur,
		.icon = { &st::menuIconPhoto },
		.keywords = {
			u"blur"_q,
			QString::fromUtf8("размытие"),
			QString::fromUtf8("блюр"),
		},
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Свой фон применяется ко всем трекам поверх обычного "
			"оформления островка. Затемнение помогает сохранить "
			"читаемость текста."),
		QString(
			"The custom background applies to every track on top of the "
			"regular island styling. Dimming keeps the text readable.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Поведение"),
		QString("Behaviour")));

	ayu.addChooseButton({
		.id = u"whitegram/musicIslandExpandMode"_q,
		.title = trWhitegram(
			QString::fromUtf8("Раскрытие островка"),
			QString("Island expansion")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Раскрытие островка"),
			QString("Island expansion")),
		.initialSelection = settings->musicIslandExpandMode(),
		.options = MusicIslandExpandModeOptions(),
		.setter = [](int index) {
			AyuSettings::getInstance().setMusicIslandExpandMode(index);
		},
		.icon = { &st::menuIconExpand },
		.keywords = {
			u"hover"_q,
			u"click"_q,
			u"expand"_q,
			QString::fromUtf8("наведение"),
			QString::fromUtf8("клик"),
			QString::fromUtf8("раскрытие"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandClickOpensTrack"_q,
		.title = trWhitegram(
			QString::fromUtf8("Открывать трек по клику"),
			QString("Open the track on click")),
		.getter = &AyuSettings::musicIslandClickOpensTrack,
		.setter = &AyuSettings::setMusicIslandClickOpensTrack,
		.icon = { &st::menuIconShowInChat },
		.keywords = {
			u"click"_q,
			u"track"_q,
			QString::fromUtf8("клик"),
			QString::fromUtf8("трек"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandHideTopBar"_q,
		.title = trWhitegram(
			QString::fromUtf8("Скрывать полосу плеера"),
			QString("Hide the player bar")),
		.getter = &AyuSettings::musicIslandHideTopBar,
		.setter = &AyuSettings::setMusicIslandHideTopBar,
		.icon = { &st::menuIconCaptionHide },
		.keywords = {
			u"topbar"_q,
			u"player"_q,
			QString::fromUtf8("полоса"),
			QString::fromUtf8("плеер"),
		},
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"«При наведении» — пилюля раскрывается, пока курсор над "
			"островком, и сворачивается, когда вы уводите его. "
			"«По клику» — раскрывается только по нажатию, повторное "
			"нажатие сворачивает. «Наведение и клик» — сочетает оба "
			"способа: клик «закрепляет» открытый плеер. Клик по названию "
			"трека в раскрытом виде открывает сообщение с ним (если "
			"включено ниже). Полоса плеера — штатная панель над чатом; "
			"её можно скрыть, чтобы остался только островок."),
		QString(
			"On hover expands while the cursor is over the island and "
			"collapses when you move away. On click expands only when you "
			"tap it; tapping again collapses it back. Hover and click "
			"supports both: a click pins the opened player open. Clicking "
			"the track name in the expanded view opens its message when "
			"enabled below. The player bar is the standard panel above the "
			"chat; hide it to keep only the island.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Оформление"),
		QString("Look")));

	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandCoverColors"_q,
		.title = trWhitegram(
			QString::fromUtf8("Цвета из обложки"),
			QString("Colours from the cover art")),
		.getter = &AyuSettings::musicIslandCoverColors,
		.setter = &AyuSettings::setMusicIslandCoverColors,
		.icon = { &st::menuIconChangeColors },
		.keywords = {
			u"cover"_q,
			u"color"_q,
			QString::fromUtf8("обложка"),
			QString::fromUtf8("цвет"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandBlurCover"_q,
		.title = trWhitegram(
			QString::fromUtf8("Размытая обложка на фоне"),
			QString("Blurred cover art background")),
		.getter = &AyuSettings::musicIslandBlurCover,
		.setter = &AyuSettings::setMusicIslandBlurCover,
		.icon = { &st::menuIconPhoto },
		.keywords = {
			u"blur"_q,
			u"background"_q,
			QString::fromUtf8("размытие"),
			QString::fromUtf8("фон"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicIslandEqualizer"_q,
		.title = trWhitegram(
			QString::fromUtf8("Анимированный эквалайзер"),
			QString("Animated equalizer")),
		.getter = &AyuSettings::musicIslandEqualizer,
		.setter = &AyuSettings::setMusicIslandEqualizer,
		.icon = { &st::menuIconStats },
		.keywords = {
			u"equalizer"_q,
			u"visualizer"_q,
			QString::fromUtf8("эквалайзер"),
			QString::fromUtf8("визуализатор"),
		},
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Островок подбирает акцентный цвет по обложке трека. "
			"Без обложки или с выключенными цветами используется "
			"обычная тема приложения."),
		QString(
			"The island picks an accent colour from the track cover. "
			"Without cover art, or with colours off, it uses the "
			"regular app theme.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Текст песни (Lyrics)"),
		QString("Lyrics")));

	ayu.addSettingToggle({
		.id = u"whitegram/musicLyricsEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать текст песни"),
			QString("Show lyrics")),
		.getter = &AyuSettings::musicLyricsEnabled,
		.setter = &AyuSettings::setMusicLyricsEnabled,
		.icon = { &st::menuIconShowInChat },
		.keywords = {
			u"lyrics"_q,
			u"text"_q,
			u"karaoke"_q,
			QString::fromUtf8("текст"),
			QString::fromUtf8("лирика"),
			QString::fromUtf8("караоке"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicLyricsShowInIsland"_q,
		.title = trWhitegram(
			QString::fromUtf8("Панель текста в островке"),
			QString("Lyrics panel in the island")),
		.getter = &AyuSettings::musicLyricsShowInIsland,
		.setter = &AyuSettings::setMusicLyricsShowInIsland,
		.icon = { &st::menuIconExpand },
		.keywords = {
			u"panel"_q,
			QString::fromUtf8("панель"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicLyricsFloatingLine"_q,
		.title = trWhitegram(
			QString::fromUtf8("Строка текста в свёрнутом виде"),
			QString("Lyrics line when collapsed")),
		.getter = &AyuSettings::musicLyricsFloatingLine,
		.setter = &AyuSettings::setMusicLyricsFloatingLine,
		.icon = { &st::menuIconShowInChat },
		.keywords = {
			u"line"_q,
			QString::fromUtf8("строка"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicLyricsKaraokeButton"_q,
		.title = trWhitegram(
			QString::fromUtf8("Кнопка окна-караоке"),
			QString("Karaoke window button")),
		.getter = &AyuSettings::musicLyricsKaraokeButton,
		.setter = &AyuSettings::setMusicLyricsKaraokeButton,
		.icon = { &st::menuIconUnpin },
		.keywords = {
			u"window"_q,
			u"karaoke"_q,
			QString::fromUtf8("окно"),
		},
	});
	ayu.addChooseButton({
		.id = u"whitegram/musicLyricsAnimation"_q,
		.title = trWhitegram(
			QString::fromUtf8("Анимация текста"),
			QString("Lyrics animation")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Анимация текста"),
			QString("Lyrics animation")),
		.initialSelection = settings->musicLyricsAnimation(),
		.options = MusicLyricsAnimationOptions(),
		.setter = [](int index) {
			AyuSettings::getInstance().setMusicLyricsAnimation(index);
		},
		.icon = { &st::menuIconStats },
		.keywords = {
			u"animation"_q,
			u"karaoke"_q,
			u"glow"_q,
			QString::fromUtf8("анимация"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicLyricsArtScatter"_q,
		.title = trWhitegram(
			QString::fromUtf8("Арт-разброс слов (наклон и сдвиг)"),
			QString("Artistic word scatter")),
		.getter = &AyuSettings::musicLyricsArtScatter,
		.setter = &AyuSettings::setMusicLyricsArtScatter,
		.icon = { &st::menuIconChangeColors },
		.keywords = {
			u"art"_q,
			u"scatter"_q,
			QString::fromUtf8("арт"),
			QString::fromUtf8("разброс"),
		},
	});
	ayu.addSettingToggle({
		.id = u"whitegram/musicLyricsBlur"_q,
		.title = trWhitegram(
			QString::fromUtf8("Затемнённая подложка для читаемости"),
			QString("Dimmed backdrop for readability")),
		.getter = &AyuSettings::musicLyricsBlur,
		.setter = &AyuSettings::setMusicLyricsBlur,
		.icon = { &st::menuIconPhoto },
		.keywords = {
			u"blur"_q,
			u"read"_q,
			QString::fromUtf8("читаемость"),
		},
	});
	ayu.addSlider({
		.id = u"whitegram/musicLyricsFontScale"_q,
		.title = trWhitegram(
			QString::fromUtf8("Размер текста"),
			QString("Lyrics text size")),
		.steps = 19,
		.current = settings->musicLyricsFontScale(),
		.indexToValue = [](int index) { return 70 + index * 5; },
		.onFinalChanged = [](int value) {
			AyuSettings::getInstance().setMusicLyricsFontScale(value);
		},
		.formatLabel = [](int value) {
			return QString::number(value) + u'%';
		},
		.keywords = { u"font"_q, u"size"_q, QString::fromUtf8("размер") },
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Текст подгружается с LRCLIB (бесплатно, с таймингами строк) "
			"и кэшируется. Клик по панели текста открывает окно-караоке "
			"со всем текстом и автопрокруткой за текущей строкой. "
			"Если таймингов нет — строки мягко прокручиваются по "
			"прогрессу трека. Тёмная подложка и тень keep текст "
			"читаемым поверх любой обложки."),
		QString(
			"Lyrics load from LRCLIB (free, with line timings) and are "
			"cached. Clicking the lyrics panel opens a karaoke window "
			"with the full text auto-scrolling to the current line. "
			"Without timings lines softly follow track progress. "
			"A dark backdrop and shadow keep text readable over any "
			"cover art.")));
	builder.addSkip();
});

void ShowBypassCustomMirrorBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(trWhitegram(
		QString::fromUtf8("Свой Worker"),
		QString("Custom Worker")));
	box->setWidth(st::boxWideWidth);

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		trWhitegram(
			QString::fromUtf8(
				"Домен Cloudflare Worker или полный хост. Несколько "
				"адресов — через запятую. Whitegram попробует их "
				"вместе с общими зеркалами."),
			QString(
				"Cloudflare Worker domain or full host. Separate "
				"several entries with commas. Whitegram will try them "
				"along with shared mirrors.")),
		st::boxLabel));

	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::whitegramBypassMirrorField,
			trWhitegram(
				QString::fromUtf8("my-worker.workers.dev"),
				QString("my-worker.workers.dev")),
			AyuSettings::getInstance().connectionBypassSecret().trimmed()),
		st::whitegramBypassMirrorFieldMargin);
	field->setFocus();

	const auto apply = [=] {
		AyuSettings::getInstance().setConnectionBypassSecret(
			field->getLastText().trimmed());
		box->closeBox();
	};

	box->addButton(trWhitegram(
		QString::fromUtf8("Подтвердить"),
		QString("Confirm")), apply);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	if (!AyuSettings::getInstance().connectionBypassSecret().trimmed().isEmpty()) {
		box->addLeftButton(trWhitegram(
			QString::fromUtf8("Удалить"),
			QString("Remove")), [=] {
			AyuSettings::getInstance().setConnectionBypassSecret(QString());
			box->closeBox();
		});
	}

	field->submits() | rpl::on_next([=](Qt::KeyboardModifiers) {
		apply();
	}, field->lifetime());
}

void ShowBypassAboutBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(trWhitegram(
		QString::fromUtf8("Простыми словами"),
		QString("In simple words")));
	box->setWidth(st::boxWideWidth);

	const auto addBlock = [&](
			rpl::producer<QString> title,
			rpl::producer<QString> text) {
		Ui::AddSubsectionTitle(box->verticalLayout(), std::move(title));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boxLabel));
	};

	addBlock(
		trWhitegram(
			QString::fromUtf8("Зачем это нужно"),
			QString("Why it exists")),
		trWhitegram(
			QString::fromUtf8(
				"Иногда Telegram не открывается: крутится загрузка, "
				"чаты не приходят, хотя интернет есть и сайты в браузере "
				"работают. Так бывает, когда сеть (домашний Wi‑Fi, "
				"мобильный интернет, офис) мешает именно Telegram, "
				"а остальной интернет не трогает.\n\n"
				"Обход — это встроенная помощь Whitegram в такой ситуации. "
				"Ничего устанавливать, покупать и настраивать руками "
				"не нужно: выбрали режим — и всё."),
			QString(
				"Sometimes Telegram will not open: it keeps loading and "
				"chats do not arrive, even though the internet works and "
				"websites open. That happens when the network (home Wi‑Fi, "
				"mobile data, office) interferes with Telegram, but leaves "
				"the rest of the internet alone.\n\n"
				"Bypass is Whitegram’s built-in help for that case. You do "
				"not install, buy, or configure anything by hand: pick a "
				"mode and you are done.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Что делает обход"),
			QString("What bypass does")),
		trWhitegram(
			QString::fromUtf8(
				"Обычно Whitegram стучится к Telegram «напрямую». В части "
				"сетей этот прямой путь режут.\n\n"
				"Обход отправляет те же сообщения другим путём — так, "
				"чтобы для сети это выглядело как обычный защищённый "
				"сайт (как когда вы открываете банк или почту в браузере). "
				"Содержимое чатов по-прежнему шифруется, как в обычном "
				"Telegram. Меняется только «дорога» до серверов.\n\n"
				"Включать обход, если Telegram и так нормально работает, "
				"не обязательно."),
			QString(
				"Normally Whitegram reaches Telegram directly. Some "
				"networks cut that direct path.\n\n"
				"Bypass sends the same messages another way, so the "
				"network sees something closer to a normal secure website "
				"(like opening a bank or mailbox in a browser). Chat "
				"contents stay encrypted like in regular Telegram. Only "
				"the route to the servers changes.\n\n"
				"You do not need bypass if Telegram already works fine.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Как именно меняется дорога"),
			QString("How the route actually changes")),
		trWhitegram(
			QString::fromUtf8(
				"Представьте, что каждое сообщение — это письмо.\n\n"
				"Обычно Whitegram кладёт письмо в специальный конверт "
				"«Telegram» и несёт его напрямую в почтовое отделение "
				"Telegram. В некоторых сетях на входе стоит проверка: "
				"увидела конверт «Telegram» — и не пускает. Содержимое "
				"письма она прочитать не может, оно зашифровано, но сам "
				"конверт узнаёт сразу.\n\n"
				"Обход кладёт тот же зашифрованный конверт «Telegram» "
				"внутрь обычного сайтового конверта — такого же, как "
				"когда вы открываете банк или почту в браузере. Снаружи "
				"видно только «защищённый сайт», а не Telegram. Проверка "
				"на такой конверт обычно не смотрит, и письмо проходит.\n\n"
				"Внутри ничего не меняется: сообщения шифруются тем же "
				"способом, что и всегда. Меняется только внешняя "
				"упаковка, по которой сеть узнаёт, куда вы идёте."),
			QString(
				"Think of every message as a letter.\n\n"
				"Normally Whitegram puts the letter in a special "
				"“Telegram” envelope and carries it straight to the "
				"Telegram post office. Some networks have a guard at the "
				"entrance: it sees the “Telegram” envelope and stops it. "
				"The guard cannot read the letter itself — it is "
				"encrypted — but it recognizes the envelope at once.\n\n"
				"Bypass puts the same encrypted “Telegram” envelope "
				"inside an ordinary website envelope — the same kind you "
				"get when you open a bank or mailbox in a browser. From "
				"the outside it looks like just a secure website, not "
				"Telegram. The guard usually ignores that envelope, and "
				"the letter goes through.\n\n"
				"Inside nothing changes: messages are encrypted the same "
				"way as always. Only the outer packaging changes — the "
				"part the network uses to tell where you are going.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Это не VPN"),
			QString("This is not a VPN")),
		trWhitegram(
			QString::fromUtf8(
				"Обход не прячет вас и не меняет ваш IP-адрес. Провайдер "
				"по-прежнему видит, что с телефона или компьютера идёт "
				"интернет.\n\n"
				"Он не включает VPN на весь компьютер: YouTube, браузер "
				"и игры обход не касается. Работает только соединение "
				"самого Whitegram с Telegram.\n\n"
				"Это не отдельный сервер Whitegram и не «прокси, который "
				"нужно вписать руками». Всё уже внутри приложения."),
			QString(
				"Bypass does not hide you and does not change your IP "
				"address. Your provider still sees that this phone or "
				"computer is using the internet.\n\n"
				"It does not turn on a VPN for the whole computer: "
				"YouTube, the browser, and games are unaffected. Only "
				"Whitegram’s connection to Telegram changes.\n\n"
				"It is not a Whitegram server and not a proxy you have "
				"to type in by hand. It is already inside the app.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Какой режим выбрать"),
			QString("Which mode to pick")),
		trWhitegram(
			QString::fromUtf8(
				"Выкл — обычное прямое соединение. Подходит, если "
				"Telegram открывается без проблем.\n\n"
				"Авто — сначала пробует напрямую. Если за несколько "
				"секунд (около 12) так и не подключится, обход включится "
				"сам. Удобно, если сеть то работает, то нет: в поездке, "
				"в другой стране, на чужом Wi‑Fi. Пока Telegram "
				"подключается нормально, обход не трогает соединение. "
				"Если обход уже включился сам, он останется до смены "
				"режима или перезапуска приложения.\n\n"
				"Всегда — обход включён сразу, без ожидания. Выбирайте, "
				"если Telegram у вас стабильно не открывается, а сайты "
				"открываются. Это самый надёжный вариант при блокировке."),
			QString(
				"Off — the normal direct connection. Use it if Telegram "
				"opens without problems.\n\n"
				"Auto — tries direct first. If it still cannot connect "
				"for a few seconds (about 12), bypass turns on by itself. "
				"Handy when the network is hit-or-miss: travel, another "
				"country, someone else’s Wi‑Fi. While Telegram connects "
				"normally, bypass stays out of the way. If bypass has "
				"already turned itself on, it stays until you change the "
				"mode or restart the app.\n\n"
				"Always — bypass is on immediately, with no waiting. "
				"Choose this if Telegram reliably fails to open while "
				"websites still work. This is the most reliable option "
				"when Telegram is blocked.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Что будет после переключения"),
			QString("What happens after you switch")),
		trWhitegram(
			QString::fromUtf8(
				"Whitegram заново устанавливает соединение. На секунду "
				"может появиться «подключение…» — это нормально, ничего "
				"нажимать ещё раз не нужно.\n\n"
				"Подождите 10–20 секунд. Если чаты ожили — всё получилось. "
				"Настройки запоминаются: при следующем запуске будет "
				"тот же режим."),
			QString(
				"Whitegram reconnects. You may briefly see “connecting…” "
				"— that is normal, do not tap anything again.\n\n"
				"Wait 10–20 seconds. If chats come back, it worked. The "
				"setting is remembered: the next launch uses the same "
				"mode.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Частые вопросы"),
			QString("Common questions")),
		trWhitegram(
			QString::fromUtf8(
				"Нужно ли что-то вводить? Нет. Секрет, логин и пароль "
				"для обхода не нужны.\n\n"
				"Это платно? Нет, это функция Whitegram.\n\n"
				"Безопасно ли это? Сообщения шифруются так же, как в "
				"обычном Telegram. Обход не читает ваши чаты.\n\n"
				"У меня уже стоит свой прокси в настройках Telegram. "
				"Можно оставить. Свои SOCKS и HTTP-прокси по-прежнему "
				"работают. Старые локальные «заглушки» Whitegram при "
				"включённом обходе убирает сам — это не ваш настоящий "
				"прокси.\n\n"
				"Обход включил, а Telegram всё равно не заходит. Тогда "
				"дело не только в «прямой дороге»: попробуйте другую "
				"сеть (раздать интернет с телефона), проверьте системное "
				"время на компьютере и что интернет вообще есть. Обход "
				"помогает, когда режут именно Telegram, а не весь "
				"интернет.\n\n"
				"Стало чуть медленнее. Так бывает: обходный путь длиннее "
				"прямого. Если Telegram без обхода открывается — можно "
				"вернуть «Выкл»."),
			QString(
				"Do I need to type anything? No. Bypass needs no secret, "
				"login, or password.\n\n"
				"Does it cost money? No. It is a Whitegram feature.\n\n"
				"Is it safe? Messages are encrypted like in regular "
				"Telegram. Bypass does not read your chats.\n\n"
				"I already have my own proxy in Telegram settings. You "
				"can keep it. Your own SOCKS and HTTP proxies still work. "
				"When bypass is on, Whitegram only removes old local "
				"dummy proxies — not your real one.\n\n"
				"I turned bypass on and Telegram still will not connect. "
				"Then it is not only the “direct road”: try another "
				"network (phone hotspot), check the computer clock, and "
				"make sure the internet works at all. Bypass helps when "
				"Telegram is blocked, not when the whole internet is "
				"down.\n\n"
				"It feels a bit slower. That can happen: the bypass route "
				"is longer than the direct one. If Telegram opens without "
				"bypass, you can switch back to Off.")));

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void BuildWhitegramBypass(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		const auto layout = wrap.data();

		Ui::AddSkip(layout);

		auto icon = object_ptr<Ui::RpWidget>(layout);
		const auto iconRaw = icon.data();
		const auto iconSize = st::settingsFilterIconSize;
		iconRaw->resize(QSize(iconSize, iconSize));
		iconRaw->setNaturalWidth(iconSize);
		iconRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(iconRaw);
			auto hq = PainterHighQualityEnabler(p);
			const auto rect = iconRaw->rect();
			const auto active = AyuSettings::getInstance().connectionBypassMode()
				!= ConnectionBypassMode::Off;
			p.setPen(Qt::NoPen);
			p.setBrush(active ? st::windowBgActive : st::windowBgOver);
			p.drawEllipse(rect);
			st::menuIconUnblock.paintInCenter(
				p,
				rect,
				active ? st::windowFgActive->c : st::windowSubTextFg->c);
		}, iconRaw->lifetime());
		AyuSettings::getInstance().connectionBypassModeChanges(
		) | rpl::on_next([=] {
			iconRaw->update();
		}, iconRaw->lifetime());
		layout->add(std::move(icon), style::al_top);

		Ui::AddSkip(layout);

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				trWhitegram(
					QString::fromUtf8("Обход блокировок"),
					QString("Connection bypass")),
				st::boxTitle),
			style::al_top);

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				AyuSettings::getInstance().connectionBypassModeValue(
				) | rpl::map([](ConnectionBypassMode mode) {
					const auto id = Lang::GetInstance().id();
					const auto baseId = Lang::GetInstance().baseId();
					const auto isRu = id.startsWith(u"ru"_q)
						|| baseId.startsWith(u"ru"_q);
					switch (mode) {
					case ConnectionBypassMode::Off:
						return isRu
							? QString::fromUtf8("Прямое соединение")
							: QString("Direct connection");
					case ConnectionBypassMode::Auto:
						return isRu
							? QString::fromUtf8("Включается при сбое сети")
							: QString("Turns on if the connection fails");
					case ConnectionBypassMode::Always:
						return isRu
							? QString::fromUtf8("Обход всегда включён")
							: QString("Bypass always on");
					}
					return QString();
				}),
				st::boxDividerLabel),
			style::al_top);

		Ui::AddSkip(layout);
		Ui::AddDivider(layout);
		Ui::AddSkip(layout);
		Ui::AddSubsectionTitle(
			layout,
			trWhitegram(
				QString::fromUtf8("Режим"),
				QString("Mode")));

		const auto slider = layout->add(
			object_ptr<Ui::SettingsSlider>(layout, st::whitegramBypassSlider),
			st::whitegramBypassSliderMargin);
		const auto applySections = [=] {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			slider->setSections({
				isRu ? QString::fromUtf8("Выкл") : QString("Off"),
				isRu ? QString::fromUtf8("Авто") : QString("Auto"),
				isRu ? QString::fromUtf8("Всегда") : QString("Always"),
			});
			slider->setActiveSectionFast(static_cast<int>(
				AyuSettings::getInstance().connectionBypassMode()));
		};
		applySections();
		Lang::GetInstance().idChanges(
		) | rpl::on_next([=] {
			applySections();
		}, slider->lifetime());
		slider->sectionActivated(
		) | rpl::on_next([=](int index) {
			AyuSettings::getInstance().setConnectionBypassMode(
				static_cast<ConnectionBypassMode>(index));
		}, slider->lifetime());
		AyuSettings::getInstance().connectionBypassModeChanges(
		) | rpl::on_next([=](ConnectionBypassMode mode) {
			if (slider->activeSection() != static_cast<int>(mode)) {
				slider->setActiveSectionFast(static_cast<int>(mode));
			}
		}, slider->lifetime());

		const auto cooldownHint = layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				rpl::single(QString()),
				st::boxDividerLabel),
			style::al_top);
		cooldownHint->hide();
		AyuFeatures::ConnectionBypass::modeSwitchLockedValue(
		) | rpl::on_next([=](bool locked) {
			slider->setEnabled(!locked);
			if (locked) {
				const auto id = Lang::GetInstance().id();
				const auto baseId = Lang::GetInstance().baseId();
				const auto isRu = id.startsWith(u"ru"_q)
					|| baseId.startsWith(u"ru"_q);
				cooldownHint->setText(isRu
					? QString::fromUtf8(
						"Слишком частые переключения. Подождите несколько "
						"секунд, затем можно снова сменить режим.")
					: QString(
						"Too many mode changes. Wait a few seconds before "
						"switching again."));
				cooldownHint->show();
			} else {
				cooldownHint->hide();
			}
		}, slider->lifetime());

		Ui::AddSkip(layout);

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				AyuSettings::getInstance().connectionBypassModeValue(
				) | rpl::map([](ConnectionBypassMode mode) {
					const auto id = Lang::GetInstance().id();
					const auto baseId = Lang::GetInstance().baseId();
					const auto isRu = id.startsWith(u"ru"_q)
						|| baseId.startsWith(u"ru"_q);
					switch (mode) {
					case ConnectionBypassMode::Off:
						return isRu
							? QString::fromUtf8(
								"Whitegram подключается к Telegram напрямую. "
								"Если мессенджер недоступен, выберите Авто "
								"или Всегда.")
							: QString(
								"Whitegram connects to Telegram directly. "
								"If the messenger is unreachable, choose Auto "
								"or Always.");
					case ConnectionBypassMode::Auto:
						return isRu
							? QString::fromUtf8(
								"Сначала обычное соединение. Если за несколько "
								"секунд подключиться не удалось, включается "
								"обход через WebSocket. Если все зеркала "
								"недоступны — временно пробует обычный TCP.")
							: QString(
								"Starts with a direct connection. If it cannot "
								"connect for a few seconds, WebSocket bypass "
								"turns on. If all mirrors fail, it temporarily "
								"tries plain TCP.");
					case ConnectionBypassMode::Always:
						return isRu
							? QString::fromUtf8(
								"Обход включён постоянно. Помогает при "
								"блокировках провайдера, соединение может "
								"быть чуть медленнее.")
							: QString(
								"Bypass stays on. Helps when a provider blocks "
								"Telegram; the connection may be a bit slower.");
					}
					return QString();
				}),
				st::boxDividerLabel),
			st::boxRowPadding);

		Ui::AddSkip(layout);
		Ui::AddDivider(layout);
		Ui::AddSkip(layout);
		Ui::AddSubsectionTitle(
			layout,
			trWhitegram(
				QString::fromUtf8("Свои зеркала"),
				QString("Custom mirrors")));

		const auto mirrorCardWrap = layout->add(
			object_ptr<BypassMirrorCard>(layout),
			st::whitegramBypassMirrorCardMargin);
		const auto mirrorCard = mirrorCardWrap->content();

		mirrorCard->add(
			object_ptr<Ui::FlatLabel>(
				mirrorCard,
				trWhitegram(
					QString::fromUtf8(
						"Личный Cloudflare Worker — запасной маршрут, "
						"если общие зеркала недоступны. Адрес хранится "
						"только на этом устройстве."),
					QString(
						"A personal Cloudflare Worker is a backup route "
						"when shared mirrors are unavailable. The address "
						"is stored on this device only.")),
				st::whitegramBypassMirrorHint));

		Ui::AddSkip(mirrorCard, st::settingsCheckboxesSkip);

		const auto statusLabel = mirrorCard->add(
			object_ptr<Ui::FlatLabel>(
				mirrorCard,
				rpl::single(QString()),
				st::whitegramBypassMirrorStatus));
		AyuSettings::getInstance().connectionBypassSecretValue(
		) | rpl::on_next([=](const QString &secret) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			const auto configured = !secret.trimmed().isEmpty();
			statusLabel->setText(configured
				? (isRu
					? QString::fromUtf8("Статус: личный Worker подключён")
					: QString("Status: custom Worker is active"))
				: (isRu
					? QString::fromUtf8("Статус: Worker не задан")
					: QString("Status: no Worker configured")));
			statusLabel->setTextColorOverride(configured
				? st::windowActiveTextFg->c
				: std::optional<QColor>());
		}, statusLabel->lifetime());

		Ui::AddSkip(mirrorCard, st::whitegramBypassMirrorButtonSkip);

		const auto controller = ctx.controller;
		const auto mirrorButton = mirrorCard->add(
			object_ptr<Ui::RoundButton>(
				mirrorCard,
				AyuSettings::getInstance().connectionBypassSecretValue(
				) | rpl::map([](const QString &secret) {
					const auto id = Lang::GetInstance().id();
					const auto baseId = Lang::GetInstance().baseId();
					const auto isRu = id.startsWith(u"ru"_q)
						|| baseId.startsWith(u"ru"_q);
					const auto configured = !secret.trimmed().isEmpty();
					if (configured) {
						return isRu
							? QString::fromUtf8("Изменить Worker")
							: QString("Edit Worker");
					}
					return isRu
						? QString::fromUtf8("Добавить Worker")
						: QString("Add Worker");
				}),
				st::whitegramBypassMirrorButton));
		mirrorButton->setTextTransform(
			Ui::RoundButtonTextTransform::NoTransform);
		mirrorButton->setClickedCallback([=] {
			controller->show(Box(ShowBypassCustomMirrorBox));
		});

		Ui::AddSkip(layout);

		const auto more = AddButtonWithIcon(
			layout,
			trWhitegram(
				QString::fromUtf8("Подробнее"),
				QString("Learn more")),
			st::settingsButton,
			{ &st::menuIconInfo });
		more->setClickedCallback([=] {
			controller->show(Box(ShowBypassAboutBox));
		});

		return { .widget = std::move(wrap) };
	}, [] {
		return SearchEntry{
			.id = u"whitegram/connectionBypassMode"_q,
			.title = QString::fromUtf8("Обход блокировок"),
			.keywords = {
				u"bypass"_q,
				u"websocket"_q,
				QString::fromUtf8("обход"),
				QString::fromUtf8("блокиров"),
			},
			.section = WhitegramBypass::Id(),
			.icon = { &st::menuIconUnblock },
		};
	});
}

const auto kWhitegramBypassMeta = BuildHelper({
	.id = WhitegramBypass::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Обход блокировок"),
	.icon = &st::menuIconUnblock,
}, [](SectionBuilder &builder) {
	BuildWhitegramBypass(builder);
});

const auto kWhitegramSelfDestructMeta = BuildHelper({
	.id = WhitegramSelfDestruct::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Самоуничтожение"),
	.icon = &st::menuIconDeleteAttention,
}, [](SectionBuilder &builder) {
	BuildWhitegramSelfDestructPage(builder);
});

QString FormatScheduleTime(int minutes) {
	int h = (minutes / 60) % 24;
	int m = minutes % 60;
	return QString("%1:%2").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0'));
}

QString FormatScheduleDays(int weekdays, bool isRu) {
	if ((weekdays & 0x7F) == 0x7F) {
		return isRu ? QString::fromUtf8("Каждый день") : QString("Every day");
	}
	if ((weekdays & 0x7F) == 0x1F) {
		return isRu ? QString::fromUtf8("Будни (Пн-Пт)") : QString("Weekdays (Mon-Fri)");
	}
	if ((weekdays & 0x7F) == 0x60) {
		return isRu ? QString::fromUtf8("Выходные (Сб-Вс)") : QString("Weekends (Sat-Sun)");
	}
	const QString ruDays[] = {
		QString::fromUtf8("Пн"), QString::fromUtf8("Вт"),
		QString::fromUtf8("Ср"), QString::fromUtf8("Чт"),
		QString::fromUtf8("Пт"), QString::fromUtf8("Сб"),
		QString::fromUtf8("Вс")
	};
	const QString enDays[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
	QStringList selected;
	for (int i = 0; i < 7; ++i) {
		if (weekdays & (1 << i)) {
			selected.push_back(isRu ? ruDays[i] : enDays[i]);
		}
	}
	return selected.isEmpty()
		? (isRu ? QString::fromUtf8("Нет дней") : QString("No days"))
		: selected.join(u", "_q);
}

void ShowGhostScheduleIntervalBox(
		not_null<Window::SessionController*> controller,
		uint64 userId,
		std::optional<int> editingIndex = std::nullopt) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);

		box->setTitle(rpl::single(editingIndex.has_value()
			? (isRu ? QString::fromUtf8("Редактировать интервал") : QString("Edit interval"))
			: (isRu ? QString::fromUtf8("Новый интервал") : QString("New interval"))));

		const auto entries = AyuSettings::ghost(userId).ghostScheduleEntries();
		GhostScheduleEntry initialEntry;
		if (editingIndex.has_value() && *editingIndex >= 0 && *editingIndex < int(entries.size())) {
			initialEntry = entries[*editingIndex];
		}

		const auto fromField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(isRu ? QString::fromUtf8("Время начала (ЧЧ:ММ)") : QString("Start time (HH:MM)")),
			FormatScheduleTime(initialEntry.from)));

		const auto toField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(isRu ? QString::fromUtf8("Время окончания (ЧЧ:ММ)") : QString("End time (HH:MM)")),
			FormatScheduleTime(initialEntry.to)));

		struct DaysState {
			std::vector<Ui::Checkbox*> checkboxes;
			int weekdays = 0x7F;
		};
		const auto daysState = box->lifetime().make_state<DaysState>();
		daysState->weekdays = initialEntry.weekdays;

		const QString dayNamesRu[] = {
			QString::fromUtf8("Понедельник"),
			QString::fromUtf8("Вторник"),
			QString::fromUtf8("Среда"),
			QString::fromUtf8("Четверг"),
			QString::fromUtf8("Пятница"),
			QString::fromUtf8("Суббота"),
			QString::fromUtf8("Воскресенье")
		};
		const QString dayNamesEn[] = {
			"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
		};

		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(isRu ? QString::fromUtf8("Дни недели:") : QString("Days of week:")),
			st::boxTitle));

		for (int i = 0; i < 7; ++i) {
			const auto bit = (1 << i);
			const auto checked = (daysState->weekdays & bit) != 0;
			const auto dayName = isRu ? dayNamesRu[i] : dayNamesEn[i];
			const auto cb = box->addRow(object_ptr<Ui::Checkbox>(
				box,
				dayName,
				checked,
				st::defaultBoxCheckbox));
			daysState->checkboxes.push_back(cb);
			cb->checkedChanges(
			) | rpl::on_next([=](bool isChecked) {
				if (isChecked) {
					daysState->weekdays |= bit;
				} else {
					daysState->weekdays &= ~bit;
				}
			}, box->lifetime());
		}

		const auto setAllDays = [=](int mask) {
			daysState->weekdays = mask;
			for (int i = 0; i < 7; ++i) {
				daysState->checkboxes[i]->setChecked((mask & (1 << i)) != 0);
			}
		};

		const auto presetsWrap = box->addRow(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
		const auto presetsInner = presetsWrap->entity();

		const auto btnAll = presetsInner->add(object_ptr<Ui::LinkButton>(
			presetsInner,
			isRu ? QString::fromUtf8("Выбрать все дни") : QString("Select all days")));
		btnAll->setClickedCallback([=] { setAllDays(0x7F); });

		const auto btnWork = presetsInner->add(object_ptr<Ui::LinkButton>(
			presetsInner,
			isRu ? QString::fromUtf8("Только будни (Пн-Пт)") : QString("Weekdays only (Mon-Fri)")));
		btnWork->setClickedCallback([=] { setAllDays(0x1F); });

		const auto btnWeekend = presetsInner->add(object_ptr<Ui::LinkButton>(
			presetsInner,
			isRu ? QString::fromUtf8("Только выходные (Сб-Вс)") : QString("Weekends only (Sat-Sun)")));
		btnWeekend->setClickedCallback([=] { setAllDays(0x60); });

		auto parseTime = [](const QString &str, int defaultVal) -> int {
			const auto trimmed = str.trimmed();
			const auto parts = trimmed.split(':');
			if (parts.size() == 2) {
				bool okH = false, okM = false;
				int h = parts[0].toInt(&okH);
				int m = parts[1].toInt(&okM);
				if (okH && okM && h >= 0 && h < 24 && m >= 0 && m < 60) {
					return h * 60 + m;
				}
			}
			return defaultVal;
		};

		box->addButton(tr::lng_box_ok(), [=] {
			int fromMin = parseTime(fromField->getLastText(), 540);
			int toMin = parseTime(toField->getLastText(), 1080);
			int mask = daysState->weekdays;
			if (mask == 0) mask = 0x7F;

			GhostScheduleEntry newEntry{ fromMin, toMin, mask };
			auto &g = AyuSettings::ghost(userId);
			if (editingIndex.has_value()) {
				g.updateGhostScheduleEntry(*editingIndex, newEntry);
			} else {
				g.addGhostScheduleEntry(newEntry);
			}
			box->closeBox();
		});

		if (editingIndex.has_value()) {
			box->addLeftButton(rpl::single(isRu ? QString::fromUtf8("Удалить") : QString("Delete")), [=] {
				auto &g = AyuSettings::ghost(userId);
				g.removeGhostScheduleEntry(*editingIndex);
				box->closeBox();
			});
		}

		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void AddGhostTargetModeControls(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		uint64 userId) {
	const auto targetOptions = std::vector<QString>{
		tr::ayu_GhostTargetModeGlobal(tr::now),
		tr::ayu_GhostTargetModeWhitelist(tr::now),
		tr::ayu_GhostTargetModeBlacklist(tr::now),
	};
	const auto targetOptionText = AyuSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::map([=](GhostTargetMode value) {
		return targetOptions[static_cast<int>(value)];
	});
	const auto targetButton = AddButtonWithLabel(
		container,
		tr::ayu_GhostTargetMode(),
		std::move(targetOptionText),
		st::settingsButtonNoIcon);
	targetButton->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto save = [=](int index) {
				AyuSettings::ghost(userId).setGhostTargetMode(
					static_cast<GhostTargetMode>(index));
			};
			SingleChoiceBox(box, {
				.title = tr::ayu_GhostTargetMode(),
				.options = targetOptions,
				.initialSelection = static_cast<int>(
					AyuSettings::ghost(userId).ghostTargetMode()),
				.callback = save,
			});
		}));
	});

	const auto managePeersCountLabel = [=] {
		auto &ghostSettings = AyuSettings::ghost(userId);
		const auto mode = ghostSettings.ghostTargetMode();
		if (mode == GhostTargetMode::Whitelist) {
			const auto count = int(ghostSettings.ghostTargetPeers().size());
			return count > 0
				? tr::ayu_GhostTargetPeersCount(tr::now, lt_count, count)
				: tr::ayu_GhostNoPeersSelected(tr::now);
		} else if (mode == GhostTargetMode::Blacklist) {
			const auto count = int(ghostSettings.ghostExcludedPeers().size());
			return count > 0
				? tr::ayu_GhostExcludedPeersCount(tr::now, lt_count, count)
				: tr::ayu_GhostNoPeersSelected(tr::now);
		}
		return QString();
	};

	const auto managePeersLabelProducer = AyuSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::map([=](GhostTargetMode) {
		return managePeersCountLabel();
	});

	const auto manageWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto manageInner = manageWrap->entity();
	const auto manageButton = AddButtonWithLabel(
		manageInner,
		tr::ayu_GhostManageTargetPeers(),
		rpl::duplicate(managePeersLabelProducer),
		st::settingsButtonNoIcon);
	manageButton->addClickHandler([=] {
		Window::ShowChooseRecipientBox(
			controller,
			[=](not_null<Data::Thread*> thread) {
				auto &ghostSettings = AyuSettings::ghost(userId);
				const auto peerId = thread->peer()->id.value;
				const auto mode = ghostSettings.ghostTargetMode();
				if (mode == GhostTargetMode::Whitelist) {
					ghostSettings.togglePeerInTargetList(peerId);
				} else if (mode == GhostTargetMode::Blacklist) {
					ghostSettings.togglePeerInExcludedList(peerId);
				}
				return true;
			},
			tr::ayu_GhostSelectChat());
	});
	AddSkip(manageInner);
	AddDividerText(manageInner,
		AyuSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::map([=](GhostTargetMode mode) {
		return (mode == GhostTargetMode::Whitelist)
			? tr::ayu_GhostManageTargetPeersDescription(tr::now)
			: tr::ayu_GhostManageExcludedPeersDescription(tr::now);
	}));

	AyuSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::on_next([=](GhostTargetMode mode) {
		manageWrap->toggle(mode != GhostTargetMode::Global, anim::type::normal);
	}, container->lifetime());
	manageWrap->toggle(
		AyuSettings::ghost(userId).ghostTargetMode() != GhostTargetMode::Global,
		anim::type::instant);
	manageWrap->finishAnimating();

	AddSkip(container);
	AddDividerText(container, tr::ayu_GhostTargetModeDescription());
}

void BuildWhitegramGhostMode(SectionBuilder &builder) {
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Настройки Ghost Mode (AyuGram)"),
			QString("Ghost Mode settings (AyuGram)")),
		.targetSection = AyuGhost::Id(),
		.icon = { &st::menuIconStealth },
		.keywords = {
			u"ghost"_q,
			u"ayugram"_q,
			QString::fromUtf8("ghost"),
			QString::fromUtf8("невидимка"),
		},
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto userId = AyuSettings::getInstance().useGlobalGhostMode()
				? uint64(0)
				: wctx.controller->session().userId().bare;
			AddGhostTargetModeControls(
				wctx.container,
				wctx.controller,
				userId);
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"whitegram/ghostTargetMode"_q,
				.title = tr::ayu_GhostTargetMode(tr::now),
				.section = sctx.sectionId,
			});
			sctx.entries->push_back({
				.id = u"whitegram/ghostManageTargetPeers"_q,
				.title = tr::ayu_GhostManageTargetPeers(tr::now),
				.section = sctx.sectionId,
			});
		});
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Расписание Ghost Mode"),
		QString("Ghost Mode Schedule")));

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			const auto controller = wctx.controller;
			const auto userId = AyuSettings::getInstance().useGlobalGhostMode()
				? uint64(0)
				: controller->session().userId().bare;

			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);

			const auto scheduleToggle = AddButtonWithIcon(
				container,
				trWhitegram(
					QString::fromUtf8("Включить расписание"),
					QString("Enable schedule")),
				st::settingsButtonNoIcon);
			scheduleToggle->toggleOn(
				AyuSettings::ghost(userId).ghostScheduleEnabledValue()
			)->toggledValue(
			) | rpl::filter([=](bool enabled) {
				return enabled != AyuSettings::ghost(userId).ghostScheduleEnabled();
			}) | on_next([=](bool enabled) {
				AyuSettings::ghost(userId).setGhostScheduleEnabled(enabled);
			}, container->lifetime());

			AddSkip(container);
			AddDividerText(container, trWhitegram(
				QString::fromUtf8("Ghost Mode будет автоматически включаться и выключаться по заданным интервалам времени и дням недели."),
				QString("Ghost Mode will automatically turn on and off based on scheduled time intervals and days of the week.")));

			const auto scheduleWrap = container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					container,
					object_ptr<Ui::VerticalLayout>(container)));
			const auto scheduleInner = scheduleWrap->entity();

			const auto intervalsList = scheduleInner->add(
				object_ptr<Ui::VerticalLayout>(scheduleInner));

			struct ScheduleItemsHolder {
				std::vector<base::unique_qptr<QWidget>> widgets;
			};
			const auto holder = scheduleInner->lifetime().make_state<ScheduleItemsHolder>();

			const auto rebuildIntervals = [=] {
				holder->widgets.clear();
				const auto &entries = AyuSettings::ghost(userId).ghostScheduleEntries();
				if (entries.empty()) {
					auto emptyLabel = object_ptr<Ui::FlatLabel>(
						intervalsList,
						trWhitegram(
							QString::fromUtf8("Нет интервалов (нажмите «Добавить интервал»)"),
							QString("No intervals (click \"Add interval\")")),
						st::boxDividerLabel);
					holder->widgets.push_back(base::make_unique_q<QWidget>(intervalsList->add(std::move(emptyLabel))));
				} else {
					for (int i = 0; i < int(entries.size()); ++i) {
						const auto &entry = entries[i];
						const auto timeText = FormatScheduleTime(entry.from) + u" — "_q + FormatScheduleTime(entry.to);
						const auto daysText = FormatScheduleDays(entry.weekdays, isRu);
						const auto btn = AddButtonWithLabel(
							intervalsList,
							rpl::single(timeText),
							rpl::single(daysText),
							st::settingsButtonNoIcon);
						btn->addClickHandler([=] {
							ShowGhostScheduleIntervalBox(controller, userId, i);
						});
						holder->widgets.push_back(base::make_unique_q<QWidget>(btn));
					}
				}
				intervalsList->resizeToWidth(intervalsList->width());
			};

			rebuildIntervals();

			AyuSettings::ghost(userId).ghostScheduleEntriesChanges(
			) | rpl::on_next([=] {
				rebuildIntervals();
			}, scheduleInner->lifetime());

			AddSkip(scheduleInner);

			const auto addIntervalBtn = AddButtonWithIcon(
				scheduleInner,
				trWhitegram(
					QString::fromUtf8("+ Добавить интервал"),
					QString("+ Add interval")),
				st::settingsButtonNoIcon);
			addIntervalBtn->addClickHandler([=] {
				ShowGhostScheduleIntervalBox(controller, userId, std::nullopt);
			});

			AyuSettings::ghost(userId).ghostScheduleEnabledValue(
			) | rpl::on_next([=](bool enabled) {
				scheduleWrap->toggle(enabled, anim::type::normal);
			}, container->lifetime());
			scheduleWrap->toggle(
				AyuSettings::ghost(userId).ghostScheduleEnabled(),
				anim::type::instant);
			scheduleWrap->finishAnimating();
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"whitegram/ghostSchedule"_q,
				.title = QString::fromUtf8("Расписание Ghost Mode"),
				.section = sctx.sectionId,
			});
		});
	});
}

const auto kWhitegramGhostModeMeta = BuildHelper({
	.id = WhitegramGhostMode::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Ghost Mode"),
	.icon = &st::menuIconStealth,
}, [](SectionBuilder &builder) {
	BuildWhitegramGhostMode(builder);
});

const auto kWhitegramMeta = BuildHelper({
	.id = WhitegramMain::Id(),
	.parentId = MainId(),
	.title = QString::fromUtf8("Whitegram"),
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);
	builder.addSkip();
	BuildWhitegramVersionInfo(builder);
	builder.addSkip();

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Категории"),
			QString("Categories")));

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Общие"),
			QString("General")),
		.targetSection = WhitegramGeneral::Id(),
		.icon = { &st::menuIconShowAll },
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Ghost Mode"),
			QString("Ghost Mode")),
		.targetSection = WhitegramGhostMode::Id(),
		.icon = { &st::menuIconStealth },
		.keywords = {
			u"ghost"_q,
			u"stealth"_q,
			QString::fromUtf8("призрак"),
			QString::fromUtf8("невидим"),
			QString::fromUtf8("расписание"),
			QString::fromUtf8("таймер"),
			QString::fromUtf8("область"),
			QString::fromUtf8("whitelist"),
			QString::fromUtf8("blacklist"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Папки чатов"),
			QString("Chat folders")),
		.targetSection = WhitegramArchives::Id(),
		.icon = { &st::menuIconArchive },
	});
	
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Оформление"),
			QString("Appearance")),
		.targetSection = WhitegramAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Музыкальный островок"),
			QString("Music island")),
		.targetSection = WhitegramMusicIsland::Id(),
		.icon = { &st::menuIconSoundOn },
		.keywords = {
			u"music"_q,
			u"island"_q,
			u"player"_q,
			QString::fromUtf8("музыка"),
			QString::fromUtf8("островок"),
			QString::fromUtf8("плеер"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Обход блокировок"),
			QString("Connection bypass")),
		.targetSection = WhitegramBypass::Id(),
		.icon = { &st::menuIconUnblock },
		.keywords = {
			u"bypass"_q,
			u"websocket"_q,
			QString::fromUtf8("обход"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Самоуничтожение"),
			QString("Self-destruct")),
		.targetSection = WhitegramSelfDestruct::Id(),
		.icon = { &st::menuIconDeleteAttention },
		.keywords = {
			u"self-destruct"_q,
			u"wipe"_q,
			QString::fromUtf8("самоуничтожение"),
			QString::fromUtf8("удалить"),
		},
	});

	BuildWhitegramLinks(builder);
});

} // namespace

rpl::producer<QString> trWhitegram(const QString &ru, const QString &en) {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().idChanges() | rpl::to_empty
	) | rpl::map([ru, en] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return isRu ? ru : en;
	});
}

rpl::producer<QString> WhitegramMain::title() {
	return rpl::single(QString("Whitegram"));
}

WhitegramMain::WhitegramMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramMainId() {
	return WhitegramMain::Id();
}

rpl::producer<QString> WhitegramGeneral::title() {
	return trWhitegram(
		QString::fromUtf8("Основное"),
		QString("General"));
}

WhitegramGeneral::WhitegramGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramGeneralMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramGeneralId() {
	return WhitegramGeneral::Id();
}

rpl::producer<QString> WhitegramUpdates::title() {
	return trWhitegram(
		QString::fromUtf8("Обновление"),
		QString("Updates"));
}

WhitegramUpdates::WhitegramUpdates(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramUpdates::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramUpdatesMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramUpdatesId() {
	return WhitegramUpdates::Id();
}

rpl::producer<QString> WhitegramGhostMode::title() {
	return rpl::single(QString("Ghost Mode"));
}

WhitegramGhostMode::WhitegramGhostMode(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramGhostMode::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramGhostModeMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramGhostModeId() {
	return WhitegramGhostMode::Id();
}

rpl::producer<QString> WhitegramCrypto::title() {
	return trWhitegram(
		QString::fromUtf8("Курсы криптовалют"),
		QString("Cryptocurrency Rates"));
}

WhitegramCrypto::WhitegramCrypto(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramCrypto::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramCryptoMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramCryptoId() {
	return WhitegramCrypto::Id();
}

rpl::producer<QString> WhitegramTimeSpent::title() {
	return trWhitegram(
		QString::fromUtf8("Счетчик времени"),
		QString("Time Spent Counter"));
}

WhitegramTimeSpent::WhitegramTimeSpent(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramTimeSpent::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramTimeSpentMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramTimeSpentId() {
	return WhitegramTimeSpent::Id();
}

Type WhitegramStartupSoundId() { return WhitegramStartupSound::Id(); }
Type WhitegramAppearanceId() { return WhitegramAppearance::Id(); }
Type WhitegramMusicIslandId() { return WhitegramMusicIsland::Id(); }
Type WhitegramProfileBackgroundId() {
	return WhitegramProfileBackground::Id();
}
Type WhitegramStartupAnimId() { return WhitegramStartupAnim::Id(); }
Type WhitegramArchivesId() { return WhitegramArchives::Id(); }
Type WhitegramBypassId() { return WhitegramBypass::Id(); }

rpl::producer<QString> WhitegramArchives::title() {
	return trWhitegram(
		QString::fromUtf8("Папки чатов"),
		QString("Chat folders"));
}

WhitegramArchives::WhitegramArchives(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramArchives::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramArchivesMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> WhitegramBypass::title() {
	return trWhitegram(
		QString::fromUtf8("Обход блокировок"),
		QString("Connection bypass"));
}

WhitegramBypass::WhitegramBypass(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramBypass::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramBypassMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> WhitegramSelfDestruct::title() {
	return trWhitegram(
		QString::fromUtf8("Самоуничтожение"),
		QString("Self-destruct"));
}

WhitegramSelfDestruct::WhitegramSelfDestruct(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramSelfDestruct::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramSelfDestructMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramSelfDestructId() {
	return WhitegramSelfDestruct::Id();
}

WhitegramStartupSound::WhitegramStartupSound(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramStartupSound::title() {
	return trWhitegram(
		QString::fromUtf8("Звук при запуске"),
		QString("Startup Sound"));
}
void WhitegramStartupSound::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramStartupSoundMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramAppearance::WhitegramAppearance(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramAppearance::title() {
	return trWhitegram(
		QString::fromUtf8("Оформление"),
		QString("Appearance"));
}
void WhitegramAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramAppearanceMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramMusicIsland::WhitegramMusicIsland(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramMusicIsland::title() {
	return trWhitegram(
		QString::fromUtf8("Музыкальный островок"),
		QString("Music island"));
}
void WhitegramMusicIsland::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramMusicIslandMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramProfileBackground::WhitegramProfileBackground(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramProfileBackground::title() {
	return trWhitegram(
		QString::fromUtf8("Фон профиля"),
		QString("Profile background"));
}
void WhitegramProfileBackground::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramProfileBackgroundMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramStartupAnim::WhitegramStartupAnim(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramStartupAnim::title() {
	return trWhitegram(
		QString::fromUtf8("Анимация при входе"),
		QString("Startup Animation"));
}
void WhitegramStartupAnim::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramStartupAnimMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> AyuMain::title() {
	return rpl::single(QString(""));
}

AyuMain::AyuMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kAyuMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuMainId() {
	return AyuMain::Id();
}

void ShowConnectionBypassBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(trWhitegram(
		QString::fromUtf8("Обход блокировок"),
		QString("Connection bypass")));
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	const auto content = box->verticalLayout();

	Ui::AddSubsectionTitle(
		content,
		trWhitegram(
			QString::fromUtf8("Режим"),
			QString("Mode")));

	const auto slider = content->add(
		object_ptr<Ui::SettingsSlider>(content, st::whitegramBypassSlider),
		st::whitegramBypassSliderMargin);
	const auto applySections = [=] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		slider->setSections({
			isRu ? QString::fromUtf8("Выкл") : QString("Off"),
			isRu ? QString::fromUtf8("Авто") : QString("Auto"),
			isRu ? QString::fromUtf8("Всегда") : QString("Always"),
		});
		slider->setActiveSectionFast(static_cast<int>(
			AyuSettings::getInstance().connectionBypassMode()));
	};
	applySections();
	Lang::GetInstance().idChanges(
	) | rpl::on_next([=] {
		applySections();
	}, slider->lifetime());
	slider->sectionActivated(
	) | rpl::on_next([=](int index) {
		AyuSettings::getInstance().setConnectionBypassMode(
			static_cast<ConnectionBypassMode>(index));
	}, slider->lifetime());
	AyuSettings::getInstance().connectionBypassModeChanges(
	) | rpl::on_next([=](ConnectionBypassMode mode) {
		if (slider->activeSection() != static_cast<int>(mode)) {
			slider->setActiveSectionFast(static_cast<int>(mode));
		}
	}, slider->lifetime());

	const auto cooldownHint = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(QString()),
			st::boxDividerLabel),
		style::al_top);
	cooldownHint->hide();
	AyuFeatures::ConnectionBypass::modeSwitchLockedValue(
	) | rpl::on_next([=](bool locked) {
		slider->setEnabled(!locked);
		if (locked) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			cooldownHint->setText(isRu
				? QString::fromUtf8(
					"Слишком частые переключения. Подождите несколько "
					"секунд, затем можно снова сменить режим.")
				: QString(
					"Too many mode changes. Wait a few seconds before "
					"switching again."));
			cooldownHint->show();
		} else {
			cooldownHint->hide();
		}
	}, slider->lifetime());

	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			AyuSettings::getInstance().connectionBypassModeValue(
			) | rpl::map([](ConnectionBypassMode mode) {
				const auto id = Lang::GetInstance().id();
				const auto baseId = Lang::GetInstance().baseId();
				const auto isRu = id.startsWith(u"ru"_q)
					|| baseId.startsWith(u"ru"_q);
				switch (mode) {
				case ConnectionBypassMode::Off:
					return isRu
						? QString::fromUtf8(
							"Whitegram подключается к Telegram напрямую. "
							"Если мессенджер недоступен, выберите Авто "
							"или Всегда.")
						: QString(
							"Whitegram connects to Telegram directly. "
							"If the messenger is unreachable, choose Auto "
							"or Always.");
				case ConnectionBypassMode::Auto:
					return isRu
						? QString::fromUtf8(
							"Сначала обычное соединение. Если за несколько "
							"секунд подключиться не удалось, включается "
							"обход через WebSocket. Если все зеркала "
							"недоступны — временно пробует обычный TCP.")
						: QString(
							"Starts with a direct connection. If it cannot "
							"connect for a few seconds, WebSocket bypass "
							"turns on. If all mirrors fail, it temporarily "
							"tries plain TCP.");
				case ConnectionBypassMode::Always:
					return isRu
						? QString::fromUtf8(
							"Обход включён постоянно. Помогает при "
							"блокировках провайдера, соединение может "
							"быть чуть медленнее.")
						: QString(
							"Bypass stays on. Helps when a provider blocks "
							"Telegram; the connection may be a bit slower.");
				}
				return QString();
			}),
			st::boxDividerLabel),
		st::boxRowPadding);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		trWhitegram(
			QString::fromUtf8("Свои зеркала"),
			QString("Custom mirrors")));

	const auto mirrorCardWrap = content->add(
		object_ptr<BypassMirrorCard>(content),
		st::whitegramBypassMirrorCardMargin);
	const auto mirrorCard = mirrorCardWrap->content();

	mirrorCard->add(
		object_ptr<Ui::FlatLabel>(
			mirrorCard,
			trWhitegram(
				QString::fromUtf8(
					"Личный Cloudflare Worker — запасной маршрут, "
					"если общие зеркала недоступны. Адрес хранится "
					"только на этом устройстве."),
				QString(
					"A personal Cloudflare Worker is a backup route "
					"when shared mirrors are unavailable. The address "
					"is stored on this device only.")),
			st::whitegramBypassMirrorHint));

	Ui::AddSkip(mirrorCard, st::settingsCheckboxesSkip);

	const auto statusLabel = mirrorCard->add(
		object_ptr<Ui::FlatLabel>(
			mirrorCard,
			rpl::single(QString()),
			st::whitegramBypassMirrorStatus));
	AyuSettings::getInstance().connectionBypassSecretValue(
	) | rpl::on_next([=](const QString &secret) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		const auto configured = !secret.trimmed().isEmpty();
		statusLabel->setText(configured
			? (isRu
				? QString::fromUtf8("Статус: личный Worker подключён")
				: QString("Status: custom Worker is active"))
			: (isRu
				? QString::fromUtf8("Статус: Worker не задан")
				: QString("Status: no Worker configured")));
		statusLabel->setTextColorOverride(configured
			? st::windowActiveTextFg->c
			: std::optional<QColor>());
	}, statusLabel->lifetime());

	Ui::AddSkip(mirrorCard, st::whitegramBypassMirrorButtonSkip);

	const auto mirrorButton = mirrorCard->add(
		object_ptr<Ui::RoundButton>(
			mirrorCard,
			AyuSettings::getInstance().connectionBypassSecretValue(
			) | rpl::map([](const QString &secret) {
				const auto id = Lang::GetInstance().id();
				const auto baseId = Lang::GetInstance().baseId();
				const auto isRu = id.startsWith(u"ru"_q)
					|| baseId.startsWith(u"ru"_q);
				const auto configured = !secret.trimmed().isEmpty();
				if (configured) {
					return isRu
						? QString::fromUtf8("Изменить Worker")
						: QString("Edit Worker");
				}
				return isRu
					? QString::fromUtf8("Добавить Worker")
					: QString("Add Worker");
			}),
			st::whitegramBypassMirrorButton));
	mirrorButton->setTextTransform(
		Ui::RoundButtonTextTransform::NoTransform);
	mirrorButton->setClickedCallback([=] {
		box->uiShow()->showBox(Box(ShowBypassCustomMirrorBox));
	});

	Ui::AddSkip(content);

	const auto more = AddButtonWithIcon(
		content,
		trWhitegram(
			QString::fromUtf8("Подробнее"),
			QString("Learn more")),
		st::settingsButton,
		{ &st::menuIconInfo });
	more->setClickedCallback([=] {
		box->uiShow()->showBox(Box(ShowBypassAboutBox));
	});
}

void SetupConnectionBypass(
		not_null<Window::Controller*> controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto label = AyuSettings::getInstance().connectionBypassModeValue(
	) | rpl::map([](ConnectionBypassMode mode) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		switch (mode) {
		case ConnectionBypassMode::Off:
			return isRu
				? QString::fromUtf8("Выкл")
				: QString("Off");
		case ConnectionBypassMode::Auto:
			return isRu
				? QString::fromUtf8("Авто")
				: QString("Auto");
		case ConnectionBypassMode::Always:
			return isRu
				? QString::fromUtf8("Всегда")
				: QString("Always");
		}
		return QString();
	});

	const auto button = AddButtonWithLabel(
		container,
		trWhitegram(
			QString::fromUtf8("Обход блокировок"),
			QString("Connection bypass")),
		std::move(label),
		st::settingsButton,
		{ &st::menuIconUnblock });

	button->addClickHandler([=] {
		controller->show(Box(ShowConnectionBypassBox));
	});
}

} // namespace Settings
