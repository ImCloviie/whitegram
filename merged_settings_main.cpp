// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/ayu_logo.h"
#include "ayu/ui/settings/settings_appearance.h"
#include "ayu/ui/settings/settings_ayu.h"
#include "ayu/ui/settings/settings_chats.h"
#include "ayu/ui/settings/settings_filters.h"
#include "ayu/ui/settings/settings_general.h"
#include "ayu/ui/settings/settings_other.h"
#include "core/version.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ui/painter.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QDesktopServices>

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

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
				rpl::single(QString::fromUtf8("Улучшенный форк AyuGram с дополнительными функциями, эксклюзивной кастомизацией и уникальными возможностями.")),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildCryptoSettings(SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSubsectionTitle(
		rpl::single(QString::fromUtf8("Курсы криптовалют")));

	ayu.addSettingToggle({
		.id = u"whitegram/showCryptoInDrawer"_q,
		.title = rpl::single(QString::fromUtf8("Показывать в боковом меню")),
		.getter = &AyuSettings::showCryptoInDrawer,
		.setter = &AyuSettings::setShowCryptoInDrawer,
	});

	auto *settings = &AyuSettings::getInstance();

	const auto fiatOptions = std::vector<QString>{
		QString("USD ($)"),
		QString("EUR (€)"),
		QString::fromUtf8("RUB (₽)"),
	};

	auto currentFiat = AyuSettings::getInstance().cryptoFiatCurrencyValue()
		| rpl::map([=](int val) {
			return (val >= 0 && val < int(fiatOptions.size()))
				? fiatOptions[val]
				: fiatOptions[0];
		});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"whitegram/cryptoFiatCurrency"_q,
		.title = rpl::single(QString::fromUtf8("Валюта отображения")),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentFiat),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						AyuSettings::getInstance().setCryptoFiatCurrency(index);
					};
					SingleChoiceBox(box, {
						.title = rpl::single(
							QString::fromUtf8("Валюта отображения")),
						.options = fiatOptions,
						.initialSelection = settings->cryptoFiatCurrency(),
						.callback = save,
					});
				}));
		},
	});

	ayu.addSectionDivider();

	builder.addSubsectionTitle(
		rpl::single(QString::fromUtf8("Отображаемые монеты")));

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
		.title = rpl::single(QString("Toncoin (TON)")),
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
		rpl::single(QString::fromUtf8("Обычные валюты (к текущей)")));

	ayu.addSettingToggle({
		.id = u"whitegram/showFiatUSD"_q,
		.title = rpl::single(QString("US Dollar (USD)")),
		.getter = &AyuSettings::showFiatUSD,
		.setter = &AyuSettings::setShowFiatUSD,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showFiatEUR"_q,
		.title = rpl::single(QString("Euro (EUR)")),
		.getter = &AyuSettings::showFiatEUR,
		.setter = &AyuSettings::setShowFiatEUR,
	});
	ayu.addSettingToggle({
		.id = u"whitegram/showFiatRUB"_q,
		.title = rpl::single(QString::fromUtf8("Russian Ruble (RUB)")),
		.getter = &AyuSettings::showFiatRUB,
		.setter = &AyuSettings::setShowFiatRUB,
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
		.title = rpl::single(QString::fromUtf8("Разработчик")),
		.icon = { &st::menuIconChannel },
		.label = rpl::single(QString("@ImCloviie")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ImCloviie"),
			});
		},
	});

	builder.addSkip();
}

const auto kWhitegramCryptoMeta = BuildHelper({
	.id = WhitegramCrypto::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Курсы криптовалют"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	BuildCryptoSettings(builder);
	builder.addSkip();
});

const auto kWhitegramTimeSpentMeta = BuildHelper({
	.id = WhitegramTimeSpent::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Счетчик времени"),
	.icon = &st::menuIconInfo,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	auto timeText = AyuSettings::getInstance().totalTimeSpent() | rpl::map([](int64 minutes) {
		int hours = minutes / 60;
		int mins = minutes % 60;
		return QString::fromUtf8("Проведено в Whitegram: %1 ч. %2 мин.").arg(hours).arg(mins);
	});
	builder.addButton({
		.id = u"whitegram/timespent"_q,
		.title = std::move(timeText),
		.icon = { &st::menuIconInfo },
		.title = std::move(timeText),
		.icon = { &st::menuIconInfo },
	});
	
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);
	ayu.addSettingToggle({
		.id = u"whitegram/showTimeSpentInDrawer"_q,
		.title = rpl::single(QString::fromUtf8("Показывать в боковом меню")),
		.getter = &AyuSettings::showTimeSpentInDrawer,
		.setter = &AyuSettings::setShowTimeSpentInDrawer,
	});
	builder.addSkip();
});

const auto kWhitegramGeneralMeta = BuildHelper({
	.id = WhitegramGeneral::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Основное"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	builder.addSectionButton({
		.title = rpl::single(QString::fromUtf8("Курсы криптовалют")),
		.targetSection = WhitegramCrypto::Id(),
		.icon = { &st::menuIconShowAll },
	});
	builder.addSectionButton({
		.title = rpl::single(QString::fromUtf8("Счетчик времени")),
		.targetSection = WhitegramTimeSpent::Id(),
		.icon = { &st::menuIconInfo },
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);

	builder.addSubsectionTitle(
		rpl::single(QString::fromUtf8("Звук при запуске")));

	ayu.addSettingToggle({
		.id = u"whitegram/startupSoundEnabled"_q,
		.title = rpl::single(QString::fromUtf8("Включить звук")),
		.getter = &AyuSettings::startupSoundEnabled,
		.setter = &AyuSettings::setStartupSoundEnabled,
	});

	builder.addButton({
		.id = u"whitegram/selectStartupSound"_q,
		.title = rpl::single(QString::fromUtf8("Выбрать звук...")),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			auto audioFilters = u"Audio files (*.wav *.mp3);;"_q + FileDialog::AllFilesFilter();
			FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open audio file", audioFilters, [=](const FileDialog::OpenResult &result) {
				if (!result.paths.isEmpty()) {
					AyuSettings::getInstance().setStartupSoundPath(result.paths.front());
				}
			});
		},
	});

	builder.addButton({
		.id = u"whitegram/resetStartupSound"_q,
		.title = rpl::single(QString::fromUtf8("Сбросить звук на стандартный")),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			AyuSettings::getInstance().setStartupSoundPath(QString::fromUtf8("C:\\Users\\ImCloviie\\Downloads\\yooooooooooo (mp3cut.net).mp3"));
		},
	});

	builder.addSkip();
});

const auto kWhitegramMeta = BuildHelper({
	.id = WhitegramMain::Id(),
	.parentId = MainId(),

rpl::producer<QString> WhitegramMain::title() {
	return rpl::single(QString(""));
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
	return rpl::single(QString::fromUtf8("Основное"));
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

rpl::producer<QString> WhitegramCrypto::title() {
	return rpl::single(QString::fromUtf8("Курсы криптовалют"));
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
	return rpl::single(QString::fromUtf8("Счетчик времени"));
}

WhitegramTimeSpent::WhitegramTimeSpent(
Type WhitegramMainId() {
	return WhitegramMain::Id();
}

rpl::producer<QString> WhitegramGeneral::title() {
	return rpl::single(QString::fromUtf8("Основное"));
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

rpl::producer<QString> WhitegramCrypto::title() {
	return rpl::single(QString::fromUtf8("Курсы криптовалют"));
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
	return rpl::single(QString::fromUtf8("Счетчик времени"));
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

} // namespace Settings

