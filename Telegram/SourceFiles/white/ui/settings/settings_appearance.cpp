// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "white/ui/settings/settings_appearance.h"

#include "lang_auto.h"
#include "white/white_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "white/ui/boxes/font_selector.h"
#include "white/ui/components/avatar_corners_preview.h"
#include "white/ui/settings/white_builder.h"
#include "white/ui/settings/settings_white_utils.h"
#include "white/ui/settings/settings_main.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_white_icons.h"
#include "styles/style_white_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace WhiteBuilder;

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

// The app icon picker lives in the Whitegram appearance page.
void BuildNotificationBadge(SectionBuilder &builder, WhiteSectionBuilder &white) {
#if defined Q_OS_WIN || defined Q_OS_MAC
	white.addSettingToggle({
		.id = u"white/hideNotificationBadge"_q,
		.title = tr::white_HideNotificationBadge(),
		.getter = &WhiteSettings::hideNotificationBadge,
		.setter = &WhiteSettings::setHideNotificationBadge,
	});
	builder.addSkip();
	builder.addDividerText(tr::white_HideNotificationBadgeDescription());
	builder.addSkip();
#else
	white.addSectionDivider();
#endif
}

void BuildAvatarCorners(SectionBuilder &builder, WhiteSectionBuilder &white) {
	auto *settings = &WhiteSettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::white_AvatarCornersSquare(tr::now).toUpper();
		} else if (val == AyuUiSettings::kMaxAvatarCorners) {
			return tr::white_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::white_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersValue() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::whiteBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::whiteBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"white/avatarCorners"_q,
			.title = tr::white_AvatarCorners(tr::now),
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	white.addSlider({
		.id = u"white/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = AyuUiSettings::kMaxAvatarCorners + 1,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			WhiteSettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			WhiteSettings::getInstance().setAvatarCorners(val);
			ShowRestartPrompt(controller);
		},
	});

	white.addSettingToggle({
		.id = u"white/singleCornerRadius"_q,
		.title = tr::white_SingleCornerRadius(),
		.getter = &WhiteSettings::singleCornerRadius,
		.setter = &WhiteSettings::setSingleCornerRadius,
	});

	builder.addSkip();
	builder.addDividerText(tr::white_SingleCornerRadiusDescription());
	builder.addSkip();
}

void BuildAppearance(SectionBuilder &builder, WhiteSectionBuilder &white) {
	auto *settings = &WhiteSettings::getInstance();

	builder.addSubsectionTitle(tr::white_CategoryAppearance());

	white.addSettingToggle({
		.id = u"white/materialSwitches"_q,
		.altIds = { u"white/newSwitchStyle"_q },
		.title = tr::white_MaterialSwitches(),
		.getter = &WhiteSettings::materialSwitches,
		.setter = &WhiteSettings::setMaterialSwitches,
	});
	white.addSettingToggle({
		.id = u"white/disableCustomBackgrounds"_q,
		.altIds = { u"white/customThemes"_q },
		.title = tr::white_DisableCustomBackgrounds(),
		.getter = &WhiteSettings::disableCustomBackgrounds,
		.setter = &WhiteSettings::setDisableCustomBackgrounds,
	});
	white.addSettingToggle({
		.id = u"white/hidePremiumStatuses"_q,
		.title = tr::white_HidePremiumStatuses(),
		.getter = &WhiteSettings::hidePremiumStatuses,
		.setter = &WhiteSettings::setHidePremiumStatuses,
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"white/monoFont"_q,
		.title = tr::white_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::white_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			WhiteUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					WhiteSettings::getInstance().setMonoFont(font);
				});
		},
	});

	white.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, WhiteSectionBuilder &white) {
	builder.addSubsectionTitle(tr::white_ChatFoldersHeader());

	white.addSettingToggle({
		.id = u"white/hideNotificationCounters"_q,
		.altIds = { u"white/tabCounter"_q },
		.title = tr::white_HideNotificationCounters(),
		.getter = &WhiteSettings::hideNotificationCounters,
		.setter = &WhiteSettings::setHideNotificationCounters,
	});
	white.addSettingToggle({
		.id = u"white/hideAllChatsFolder"_q,
		.altIds = { u"white/hideAllChats"_q },
		.title = tr::white_HideAllChats(),
		.getter = &WhiteSettings::hideAllChatsFolder,
		.setter = &WhiteSettings::setHideAllChatsFolder,
	});

	white.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, WhiteSectionBuilder &white) {
	builder.addSubsectionTitle(tr::white_TrayElementsHeader());

	white.addSettingToggle({
		.id = u"white/showGhostToggleInTray"_q,
		.title = tr::white_EnableGhostModeTray(),
		.getter = &WhiteSettings::showGhostToggleInTray,
		.setter = &WhiteSettings::setShowGhostToggleInTray,
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	white.addSettingToggle({
		.id = u"white/showStreamerToggleInTray"_q,
		.title = tr::white_EnableStreamerModeTray(),
		.getter = &WhiteSettings::showStreamerToggleInTray,
		.setter = &WhiteSettings::setShowStreamerToggleInTray,
	});
#endif

	// Ends the block with its own divider.
	BuildNotificationBadge(builder, white);
}

void BuildDrawerElements(SectionBuilder &builder, WhiteSectionBuilder &white) {
	builder.addSubsectionTitle(tr::white_DrawerElementsHeader());

	white.addSettingToggle({
		.id = u"white/showMyProfileInDrawer"_q,
		.title = tr::lng_menu_my_profile(),
		.getter = &WhiteSettings::showMyProfileInDrawer,
		.setter = &WhiteSettings::setShowMyProfileInDrawer,
		.icon = { &st::menuIconProfile },
	});

	const auto controller = builder.controller();
	if (controller && HasDrawerBots(controller)) {
		white.addSettingToggle({
			.id = u"white/showBotsInDrawer"_q,
			.title = tr::lng_filters_type_bots(),
			.getter = &WhiteSettings::showBotsInDrawer,
			.setter = &WhiteSettings::setShowBotsInDrawer,
			.icon = { &st::menuIconBot },
		});
	}

	white.addSettingToggle({
		.id = u"white/showNewGroupInDrawer"_q,
		.title = tr::lng_create_group_title(),
		.getter = &WhiteSettings::showNewGroupInDrawer,
		.setter = &WhiteSettings::setShowNewGroupInDrawer,
		.icon = { &st::menuIconGroups },
	});
	white.addSettingToggle({
		.id = u"white/showNewChannelInDrawer"_q,
		.title = tr::lng_create_channel_title(),
		.getter = &WhiteSettings::showNewChannelInDrawer,
		.setter = &WhiteSettings::setShowNewChannelInDrawer,
		.icon = { &st::menuIconChannel },
	});
	white.addSettingToggle({
		.id = u"white/showContactsInDrawer"_q,
		.title = tr::lng_menu_contacts(),
		.getter = &WhiteSettings::showContactsInDrawer,
		.setter = &WhiteSettings::setShowContactsInDrawer,
		.icon = { &st::menuIconUserShow },
	});
	white.addSettingToggle({
		.id = u"white/showCallsInDrawer"_q,
		.title = tr::lng_menu_calls(),
		.getter = &WhiteSettings::showCallsInDrawer,
		.setter = &WhiteSettings::setShowCallsInDrawer,
		.icon = { &st::menuIconPhone },
	});
	white.addSettingToggle({
		.id = u"white/showSavedMessagesInDrawer"_q,
		.title = tr::lng_saved_messages(),
		.getter = &WhiteSettings::showSavedMessagesInDrawer,
		.setter = &WhiteSettings::setShowSavedMessagesInDrawer,
		.icon = { &st::menuIconSavedMessages },
	});
	white.addSettingToggle({
		.id = u"white/showLReadToggleInDrawer"_q,
		.title = tr::white_LReadMessages(),
		.getter = &WhiteSettings::showLReadToggleInDrawer,
		.setter = &WhiteSettings::setShowLReadToggleInDrawer,
		.icon = { &st::whiteLReadMenuIcon },
	});
	white.addSettingToggle({
		.id = u"white/showSReadToggleInDrawer"_q,
		.title = tr::white_SReadMessages(),
		.getter = &WhiteSettings::showSReadToggleInDrawer,
		.setter = &WhiteSettings::setShowSReadToggleInDrawer,
		.icon = { &st::whiteSReadMenuIcon },
	});
	white.addSettingToggle({
		.id = u"white/showNightModeToggleInDrawer"_q,
		.title = tr::lng_menu_night_mode(),
		.getter = &WhiteSettings::showNightModeToggleInDrawer,
		.setter = &WhiteSettings::setShowNightModeToggleInDrawer,
		.icon = { &st::menuIconNightMode },
	});
	white.addSettingToggle({
		.id = u"white/showGhostToggleInDrawer"_q,
		.title = tr::white_GhostModeToggle(),
		.getter = &WhiteSettings::showGhostToggleInDrawer,
		.setter = &WhiteSettings::setShowGhostToggleInDrawer,
		.icon = { &st::whiteGhostIcon },
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	white.addSettingToggle({
		.id = u"white/showStreamerToggleInDrawer"_q,
		.title = tr::white_StreamerModeToggle(),
		.getter = &WhiteSettings::showStreamerToggleInDrawer,
		.setter = &WhiteSettings::setShowStreamerToggleInDrawer,
		.icon = { &st::whiteStreamerModeMenuIcon },
	});
#endif

	builder.addSkip();
}

} // namespace

void BuildAyuAppearanceContent(SectionBuilder &builder) {
	auto white = WhiteSectionBuilder(builder);

	builder.addSkip();
	BuildAvatarCorners(builder, white);
	BuildAppearance(builder, white);
	BuildChatFolders(builder, white);
	BuildTrayElements(builder, white);
	BuildDrawerElements(builder, white);
	builder.addSkip();
}

} // namespace Settings
