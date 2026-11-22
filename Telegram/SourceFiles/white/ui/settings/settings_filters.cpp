// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "white/ui/settings/settings_filters.h"

#include "lang_auto.h"
#include "white/white_settings.h"
#include "white/data/white_database.h"
#include "white/features/filters/filters_cache_controller.h"
#include "white/ui/boxes/import_filters_box.h"
#include "white/ui/settings/white_builder.h"
#include "white/ui/settings/settings_main.h"
#include "white/utils/telegram_helpers.h"
#include "boxes/abstract_box.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "filters/per_dialog_filter.h"
#include "filters/settings_filters_list.h"
#include "inline_bots/bot_attach_web_view.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_white_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace WhiteBuilder;

namespace {

void BuildFiltersSettings(SectionBuilder &builder) {
	auto *settings = &WhiteSettings::getInstance();

	builder.addDividerText(
		trWhitegram(
			QString::fromUtf8(
				"Фильтры прячут лишние сообщения: по словам и "
				"выражениям, от заблокированных пользователей "
				"или от выбранных людей и каналов. Скрыто будет "
				"только у вас, остальные видят сообщения как "
				"обычно."),
			QString(
				"Filters hide messages you don't want to see: by "
				"words and expressions, from blocked users, or "
				"from chosen people and channels. They are hidden "
				"only for you, everyone else sees them as usual.")));
	builder.addSkip();
	builder.addSubsectionTitle(tr::white_RegexFilters());

	const auto enabledButton = builder.addButton({
		.id = u"white/filtersEnabled"_q,
		.title = tr::white_RegexFiltersEnable(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabled()),
	});
	if (enabledButton) {
		enabledButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabled());
		}) | on_next([=](bool enabled) {
			WhiteSettings::getInstance().setFiltersEnabled(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, enabledButton->lifetime());
	}

	const auto sharedButton = builder.addButton({
		.id = u"white/filtersEnabledInChats"_q,
		.altIds = { u"white/filtersInChats"_q },
		.title = tr::white_RegexFiltersEnableSharedInChats(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabledInChats()),
	});
	if (sharedButton) {
		sharedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabledInChats());
		}) | on_next([=](bool enabled) {
			WhiteSettings::getInstance().setFiltersEnabledInChats(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, sharedButton->lifetime());
	}

	const auto blockedButton = builder.addButton({
		.id = u"white/hideFromBlocked"_q,
		.title = tr::white_FiltersHideFromBlocked(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->hideFromBlocked()),
	});
	if (blockedButton) {
		blockedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->hideFromBlocked());
		}) | on_next([=](bool enabled) {
			WhiteSettings::getInstance().setHideFromBlocked(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, blockedButton->lifetime());
	}

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Общие фильтры всегда работают в каналах, а в группах и "
			"личных чатах — только со вторым переключателем.\n\n"
			"«Скрывать от пользователей в ЧС» прячет сообщения и "
			"пересылки от тех, кого вы заблокировали, в группах и "
			"каналах. Личный чат с ними остаётся видимым."),
		QString(
			"Shared filters always work in channels, and in groups and "
			"private chats only with the second switch.\n\n"
			"\"Hide from blocked users\" hides messages and forwards "
			"from people you blocked in groups and channels. Your "
			"private chat with them stays visible.")));
	builder.addSkip();
}

void BuildShared(SectionBuilder &builder) {

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"white/sharedFilters"_q,
		.title = tr::white_RegexFiltersShared(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->showSettings(WhiteFiltersList::Id());
		},
	});
}

void BuildShadowBan(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addButton({
		.id = u"white/shadowBanIds"_q,
		.altIds = { u"white/shadowBanList"_q },
		.title = tr::white_FiltersShadowBan(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->shadowBan = true;
			controller->showSettings(WhiteFiltersList::Id());
		},
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Общие фильтры — слова и регулярные выражения для всех "
			"чатов. Фильтр только для одного чата: выделите текст в "
			"сообщении, нажмите правой кнопкой и выберите «Добавить "
			"фильтр».\n\n"
			"Теневой бан скрывает все сообщения и пересылки выбранных "
			"людей и каналов. Добавить можно через меню сообщения: "
			"«В теневой бан»."),
		QString(
			"Shared filters are words and regular expressions for all "
			"chats. For one chat only, select text in a message, "
			"right-click and choose \"Add filter\".\n\n"
			"Shadow ban hides every message and forward from chosen "
			"people and channels. Add them from the message menu: "
			"\"Shadow ban\".")));
}

void BuildPerDialog(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			if (!WhiteDatabase::hasPerDialogFilters()) {
				return;
			}

			const auto container = wctx.container;
			const auto controller = wctx.controller;

			AddSkip(container);
			AddDivider(container);

			auto ctrl = container->lifetime().make_state<PerDialogFiltersListController>(
				&controller->session(),
				controller);

			auto list = object_ptr<Ui::PaddingWrap<PeerListContent>>(
				container,
				object_ptr<PeerListContent>(
					container,
					ctrl),
				QMargins(0, -st::peerListBox.padding.top(), 0, -st::peerListBox.padding.bottom()));
			AddSkip(container);
			const auto content = container->add(std::move(list));
			AddSkip(container);
			auto delegate = container->lifetime().make_state<PeerListContentDelegateSimple>();
			delegate->setContent(content->entity());
			ctrl->setDelegate(delegate);
		}, [&](const SearchContext &) {
		});
	});
}

const auto kMeta = BuildHelper({
	.id = WhiteFilters::Id(),
	.parentId = WhitegramMain::Id(),
	.title = &tr::white_CategoryFilters,
	.icon = &st::menuIconTagFilter,
}, [](SectionBuilder &builder) {
	BuildFiltersSettings(builder);
	BuildShared(builder);
	BuildShadowBan(builder);
	BuildPerDialog(builder);
});

} // namespace

rpl::producer<QString> WhiteFilters::title() {
	return tr::white_CategoryFilters();
}

void WhiteFilters::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	addAction(
		tr::white_FiltersMenuSelectChat(tr::now),
		[=] {
			if (const auto window = Core::App().activeWindow()) {
				if (const auto controller = window->sessionController()) {
					auto types = InlineBots::PeerTypes();
					types |= InlineBots::PeerType::Bot;
					types |= InlineBots::PeerType::Group;
					types |= InlineBots::PeerType::Broadcast;

					Window::ShowChooseRecipientBox(
						controller,
						[=](not_null<Data::Thread*> thread) {
							const auto peer = thread->peer();
							controller->dialogId = getDialogIdFromPeer(peer);
							controller->showExclude = true;
							controller->showSettings(WhiteFiltersList::Id());
							return true;
						},
						tr::white_FiltersMenuSelectChat(),
						nullptr,
						types);
				}
			}
		},
		&st::menuIconSearch);
	addAction({ .isSeparator = true });
	addAction(
		tr::white_FiltersMenuImport(tr::now),
		[=] {
			auto box = Box(Ui::FillImportFiltersBox, true);
			Ui::show(std::move(box));
		},
		&st::menuIconArchive);
	if (WhiteDatabase::hasFilters()) {
		addAction(
			tr::white_FiltersMenuExport(tr::now),
			[=] {
				auto box = Box(Ui::FillImportFiltersBox, false);
				Ui::show(std::move(box));
			},
			&st::menuIconUnarchive);
	}
	addAction({ .isSeparator = true });
	addAction({
		.text = tr::white_FiltersMenuClear(tr::now),
		.handler = [=] {
			auto callback = [=](Fn<void()> &&close) {
				WhiteDatabase::deleteAllFilters();
				WhiteDatabase::deleteAllExclusions();
				FiltersCacheController::rebuildCache();
				FiltersCacheController::fireUpdate();
				close();
			};
			auto box = Ui::MakeConfirmBox({
				.text = tr::white_FiltersClearPopupText(),
				.confirmed = callback,
				.confirmText = tr::white_FiltersClearPopupActionText(),
				.confirmStyle = &st::attentionBoxButton,
			});
			Ui::show(std::move(box));
		},
		.icon = &st::menuIconClearAttention,
		.isAttention = true,
	});
}

WhiteFilters::WhiteFilters(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhiteFilters::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhiteFiltersId() {
	return WhiteFilters::Id();
}

} // namespace Settings
