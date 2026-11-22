// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "white/ui/settings/settings_chats.h"

#include "lang_auto.h"
#include "white/white_settings.h"
#include "white/ui/boxes/edit_mark_box.h"
#include "white/ui/components/message_preview.h"
#include "white/ui/settings/white_builder.h"
#include "white/ui/settings/settings_white_utils.h"
#include "white/ui/settings/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_white_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <memory>

namespace Settings {

using namespace Builder;
using namespace WhiteBuilder;

namespace {

struct PreviewState {
	MessagePreview *widget = nullptr;
};

void BuildStickersAndEmoji(SectionBuilder &builder, WhiteSectionBuilder &white) {
	builder.addSubsectionTitle(tr::lng_settings_stickers_emoji());

	white.addSettingToggle({
		.id = u"white/showOnlyAddedEmojisAndStickers"_q,
		.title = tr::white_ShowOnlyAddedEmojisAndStickers(),
		.getter = &WhiteSettings::showOnlyAddedEmojisAndStickers,
		.setter = &WhiteSettings::setShowOnlyAddedEmojisAndStickers,
	});

	white.addSettingToggle({
		.id = u"white/unlimitedRecentStickers"_q,
		.altIds = { u"white/recentStickersCount"_q },
		.title = tr::white_SettingsUnlimitedRecentStickers(),
		.getter = &WhiteSettings::unlimitedRecentStickers,
		.setter = &WhiteSettings::setUnlimitedRecentStickers,
	});

	white.addCollapsibleToggle({
		.id = u"white/hideReactions"_q,
		.title = tr::white_HideReactions(),
		.checkboxes = {
			NestedEntry{
				tr::white_HideReactionsInChannels(tr::now),
				[] { return !WhiteSettings::getInstance().showChannelReactions(); },
				[](bool v) { WhiteSettings::getInstance().setShowChannelReactions(!v); }
			},
			NestedEntry{
				tr::white_HideReactionsInGroups(tr::now),
				[] { return !WhiteSettings::getInstance().showGroupReactions(); },
				[](bool v) { WhiteSettings::getInstance().setShowGroupReactions(!v); }
			},
			NestedEntry{
				tr::white_HideReactionsInPrivateChats(tr::now),
				[] { return !WhiteSettings::getInstance().showPrivateChatReactions(); },
				[](bool v) { WhiteSettings::getInstance().setShowPrivateChatReactions(!v); }
			}
		},
		.toggledWhenAll = false,
	});

	white.addSectionDivider();
}

void BuildGroupsAndChannels(SectionBuilder &builder, WhiteSectionBuilder &white) {
	auto *settings = &WhiteSettings::getInstance();

	builder.addSubsectionTitle(tr::lng_premium_double_limits_subtitle_channels());

	white.addChooseButton({
		.id = u"white/channelBottomButton"_q,
		.altIds = { u"white/bottomButton"_q },
		.title = tr::white_ChannelBottomButton(),
		.boxTitle = tr::white_ChannelBottomButton(),
		.initialSelection = static_cast<int>(settings->channelBottomButton()),
		.options = {
			tr::white_ChannelBottomButtonHide(tr::now),
			tr::white_ChannelBottomButtonMute(tr::now),
			tr::white_ChannelBottomButtonDiscuss(tr::now),
		},
		.setter = [](int index) {
			WhiteSettings::getInstance().setChannelBottomButton(
				static_cast<ChannelBottomButton>(index));
		},
	});

	white.addSettingToggle({
		.id = u"white/quickAdminShortcuts"_q,
		.title = tr::white_QuickAdminShortcuts(),
		.getter = &WhiteSettings::quickAdminShortcuts,
		.setter = &WhiteSettings::setQuickAdminShortcuts,
	});
	white.addSettingToggle({
		.id = u"white/disableGreetingSticker"_q,
		.title = tr::white_DisableGreetingSticker(),
		.getter = &WhiteSettings::disableGreetingSticker,
		.setter = &WhiteSettings::setDisableGreetingSticker,
	});
	white.addSettingToggle({
		.id = u"white/showMessageShot"_q,
		.title = tr::white_SettingsShowMessageShot(),
		.getter = &WhiteSettings::showMessageShot,
		.setter = &WhiteSettings::setShowMessageShot,
	});

	builder.addSkip();
	builder.addDividerText(tr::white_SettingsShowMessageShotDescription());
	builder.addSkip();
}

void BuildMarks(
		SectionBuilder &builder,
		WhiteSectionBuilder &white,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &WhiteSettings::getInstance();
	const auto controller = builder.controller();

	builder.addSubsectionTitle(tr::lng_settings_messages());

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<MessagePreview>(ctx.container, controller);
		previewState->widget = preview.data();
		return {
			.widget = std::move(preview),
			.margin = style::margins(
				0,
				st::defaultVerticalListSkip,
				0,
				st::settingsPrivacySkipTop),
		};
	});

	white.addSettingToggle({
		.id = u"white/replaceBottomInfoWithIcons"_q,
		.altIds = { u"white/replaceEditedWithIcon"_q },
		.title = tr::white_ReplaceMarksWithIcons(),
		.getter = &WhiteSettings::replaceBottomInfoWithIcons,
		.setter = &WhiteSettings::setReplaceBottomInfoWithIcons,
	});

	builder.scope([&] {
		builder.addButton({
			.id = u"white/deletedMark"_q,
			.title = tr::white_DeletedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = WhiteSettings::getInstance().deletedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::white_DeletedMarkText(),
					settings->deletedMark(),
					QString("🧹"),
					[=](const QString &value) {
						WhiteSettings::getInstance().setDeletedMark(value);
					});
				Ui::show(std::move(box));
			},
		});

		builder.addButton({
			.id = u"white/editedMark"_q,
			.title = tr::white_EditedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = WhiteSettings::getInstance().editedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::white_EditedMarkText(),
					settings->editedMark(),
					tr::lng_edited(tr::now),
					[=](const QString &value) {
						WhiteSettings::getInstance().setEditedMark(value);
					});
				Ui::show(std::move(box));
			},
		});
	}, WhiteSettings::getInstance().replaceBottomInfoWithIconsValue()
		| rpl::map([](bool v) { return !v; }));

	white.addSettingToggle({
		.id = u"white/removeMessageTail"_q,
		.title = tr::white_RemoveMessageTail(),
		.getter = &WhiteSettings::removeMessageTail,
		.setter = &WhiteSettings::setRemoveMessageTail,
	});

	white.addSettingToggle({
		.id = u"white/hideFastShare"_q,
		.altIds = { u"white/hideShareButton"_q },
		.title = tr::white_HideShareButton(),
		.getter = &WhiteSettings::hideFastShare,
		.setter = &WhiteSettings::setHideFastShare,
	});
	white.addSettingToggle({
		.id = u"white/simpleQuotesAndReplies"_q,
		.altIds = { u"white/disableColorfulReplies"_q, u"white/replyElements"_q },
		.title = tr::white_SimpleQuotesAndReplies(),
		.getter = &WhiteSettings::simpleQuotesAndReplies,
		.setter = &WhiteSettings::setSimpleQuotesAndReplies,
	});

	const auto semiTransparent = white.addSettingToggle({
		.id = u"white/semiTransparentDeletedMessages"_q,
		.altIds = { u"white/translucentDeletedMessages"_q },
		.title = tr::white_SemiTransparentDeletedMessages(),
		.getter = &WhiteSettings::semiTransparentDeletedMessages,
		.setter = &WhiteSettings::setSemiTransparentDeletedMessages,
	});
	if (semiTransparent) {
		white.addBetaBadge(semiTransparent);
	}

	white.addSectionDivider();
}

void BuildWideMessagesMultiplier(
		SectionBuilder &builder,
		WhiteSectionBuilder &white,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &WhiteSettings::getInstance();

	constexpr auto kMinSize = 1.00;
	constexpr auto kStep = 0.05;

	const auto valueToIndex = [=](double value) {
		return static_cast<int>(std::round((value - kMinSize) / kStep));
	};

	const auto controller = builder.controller();
	white.addSlider({
		.id = u"white/messageBubbleRadius"_q,
		.title = tr::white_MessageBubbleRadius(),
		.steps = 17,
		.current = settings->messageBubbleRadius(),
		.indexToValue = [](int index) { return index; },
		.onChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
		},
		.onFinalChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
			WhiteSettings::getInstance().setMessageBubbleRadius(index);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [](int index) {
			return QString::number(index);
		},
	});

	white.addSectionDivider();

	white.addSlider({
		.id = u"white/wideMultiplier"_q,
		.title = tr::white_SettingsWideMultiplier(),
		.steps = 61, // (4.00 - 1.00) / 0.05 + 1
		.current = valueToIndex(settings->wideMultiplier()),
		.indexToValue = [](int index) { return index; },
		.onChanged = nullptr,
		.onFinalChanged = [=](int index) {
			WhiteSettings::getInstance().setWideMultiplier(
				kMinSize + index * kStep);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [=](int index) {
			return QString::number(kMinSize + index * kStep, 'f', 2);
		},
	});

	builder.addSkip();
	builder.addDividerText(tr::white_SettingsWideMultiplierDescription());
	builder.addSkip();
}

void BuildContextMenuElements(SectionBuilder &builder, WhiteSectionBuilder &white) {
	auto *settings = &WhiteSettings::getInstance();

	builder.addSubsectionTitle(tr::white_ContextMenuElementsHeader());

	const auto options = std::vector{
		tr::white_SettingsContextMenuItemHidden(tr::now),
		tr::white_SettingsContextMenuItemShown(tr::now),
		tr::white_SettingsContextMenuItemExtended(tr::now),
	};

	white.addChooseButton({
		.id = u"white/showReactionsPanelInContextMenu"_q,
		.title = tr::white_SettingsContextMenuReactionsPanel(),
		.boxTitle = tr::white_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showReactionsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { WhiteSettings::getInstance().setShowReactionsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconReactions },
	});
	white.addChooseButton({
		.id = u"white/showViewsPanelInContextMenu"_q,
		.title = tr::white_SettingsContextMenuViewsPanel(),
		.boxTitle = tr::white_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showViewsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { WhiteSettings::getInstance().setShowViewsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconShowInChat },
	});
	white.addChooseButton({
		.id = u"white/showHideMessageInContextMenu"_q,
		.title = tr::white_ContextHideMessage(),
		.boxTitle = tr::white_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showHideMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { WhiteSettings::getInstance().setShowHideMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconClear },
	});
	white.addChooseButton({
		.id = u"white/showUserMessagesInContextMenu"_q,
		.title = tr::white_UserMessagesMenuText(),
		.boxTitle = tr::white_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showUserMessagesInContextMenu()),
		.options = options,
		.setter = [](int i) { WhiteSettings::getInstance().setShowUserMessagesInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconTTL },
	});
	white.addChooseButton({
		.id = u"white/showMessageDetailsInContextMenu"_q,
		.title = tr::white_MessageDetailsPC(),
		.boxTitle = tr::white_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showMessageDetailsInContextMenu()),
		.options = options,
		.setter = [](int i) { WhiteSettings::getInstance().setShowMessageDetailsInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconInfo },
	});
	white.addChooseButton({
		.id = u"white/showRepeatMessageInContextMenu"_q,
		.title = tr::white_RepeatMessage(),
		.boxTitle = tr::white_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showRepeatMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { WhiteSettings::getInstance().setShowRepeatMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::whiteRepeatMenuIcon },
	});
	if (settings->filtersEnabled()) {
		white.addChooseButton({
			.id = u"white/showAddFilterInContextMenu"_q,
			.title = tr::white_RegexFilterQuickAdd(),
			.boxTitle = tr::white_SettingsContextMenuTitle(),
			.initialSelection = static_cast<int>(settings->showAddFilterInContextMenu()),
			.options = options,
			.setter = [](int i) { WhiteSettings::getInstance().setShowAddFilterInContextMenu(static_cast<ContextMenuVisibility>(i)); },
			.icon = { &st::menuIconAddToFolder },
		});
	}

	builder.addSkip();
	builder.addDividerText(tr::white_SettingsContextMenuDescription());
	builder.addSkip();
}

void BuildMessageFieldElements(SectionBuilder &builder, WhiteSectionBuilder &white) {
	builder.addSubsectionTitle(tr::white_MessageFieldElementsHeader());

	white.addSettingToggle({
		.id = u"white/showAttachButtonInMessageField"_q,
		.title = tr::white_MessageFieldElementAttach(),
		.getter = &WhiteSettings::showAttachButtonInMessageField,
		.setter = &WhiteSettings::setShowAttachButtonInMessageField,
		.icon = { &st::messageFieldAttachIcon },
	});
	white.addSettingToggle({
		.id = u"white/showCommandsButtonInMessageField"_q,
		.title = tr::white_MessageFieldElementCommands(),
		.getter = &WhiteSettings::showCommandsButtonInMessageField,
		.setter = &WhiteSettings::setShowCommandsButtonInMessageField,
		.icon = { &st::messageFieldCommandsIcon },
	});
	white.addSettingToggle({
		.id = u"white/showAutoDeleteButtonInMessageField"_q,
		.title = tr::white_MessageFieldElementTTL(),
		.getter = &WhiteSettings::showAutoDeleteButtonInMessageField,
		.setter = &WhiteSettings::setShowAutoDeleteButtonInMessageField,
		.icon = { &st::messageFieldTTLIcon },
	});
	white.addSettingToggle({
		.id = u"white/showEmojiButtonInMessageField"_q,
		.title = tr::white_MessageFieldElementEmoji(),
		.getter = &WhiteSettings::showEmojiButtonInMessageField,
		.setter = &WhiteSettings::setShowEmojiButtonInMessageField,
		.icon = { &st::messageFieldEmojiIcon },
	});
	white.addSettingToggle({
		.id = u"white/showMicrophoneButtonInMessageField"_q,
		.title = tr::white_MessageFieldElementVoice(),
		.getter = &WhiteSettings::showMicrophoneButtonInMessageField,
		.setter = &WhiteSettings::setShowMicrophoneButtonInMessageField,
		.icon = { &st::messageFieldVoiceIcon },
	});
	white.addSettingToggle({
		.id = u"white/showGiftButtonInMessageField"_q,
		.title = tr::lng_profile_action_short_gift(),
		.getter = &WhiteSettings::showGiftButtonInMessageField,
		.setter = &WhiteSettings::setShowGiftButtonInMessageField,
		.icon = { &st::settingsButtonIconGift },
	});
	white.addSettingToggle({
		.id = u"white/showAiEditorButtonInMessageField"_q,
		.title = tr::lng_ai_compose_title(),
		.getter = &WhiteSettings::showAiEditorButtonInMessageField,
		.setter = &WhiteSettings::setShowAiEditorButtonInMessageField,
		.icon = { &st::messageFieldCocoonAiIcon },
	});

	white.addSectionDivider();
}

void BuildMessageFieldPopups(SectionBuilder &builder, WhiteSectionBuilder &white) {
	builder.addSubsectionTitle(tr::white_MessageFieldPopupsHeader());

	white.addSettingToggle({
		.id = u"white/showAttachPopup"_q,
		.title = tr::white_MessageFieldElementAttach(),
		.getter = &WhiteSettings::showAttachPopup,
		.setter = &WhiteSettings::setShowAttachPopup,
		.icon = { &st::messageFieldAttachIcon },
	});
	white.addSettingToggle({
		.id = u"white/showEmojiPopup"_q,
		.title = tr::white_MessageFieldElementEmoji(),
		.getter = &WhiteSettings::showEmojiPopup,
		.setter = &WhiteSettings::setShowEmojiPopup,
		.icon = { &st::messageFieldEmojiIcon },
	});
}

} // namespace

void BuildAyuChatsContent(SectionBuilder &builder) {
	auto white = WhiteSectionBuilder(builder);
	const auto previewState = std::make_shared<PreviewState>();

	builder.addSkip();
	BuildStickersAndEmoji(builder, white);
	BuildGroupsAndChannels(builder, white);
	BuildMarks(builder, white, previewState);
	BuildWideMessagesMultiplier(builder, white, previewState);
	BuildContextMenuElements(builder, white);
	BuildMessageFieldElements(builder, white);
	BuildMessageFieldPopups(builder, white);
	builder.addSkip();
}

} // namespace Settings
