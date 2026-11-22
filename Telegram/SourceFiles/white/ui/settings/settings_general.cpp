// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "white/ui/settings/settings_general.h"

#include "lang_auto.h"
#include "lang/lang_keys.h"
#include "white/white_settings.h"
#include "white/ui/settings/white_builder.h"
#include "white/ui/settings/settings_white_utils.h"
#include "white/ui/settings/settings_main.h"
#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "lang/lang_text_entity.h"
#include "platform/platform_translate_provider.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace WhiteBuilder;

namespace {

void BuildTranslator(SectionBuilder &builder, WhiteSectionBuilder &white) {
	builder.addSubsectionTitle(tr::lng_translate_settings_subtitle());

	auto *settings = &WhiteSettings::getInstance();

	const auto options = std::vector{
		std::pair(TranslationProvider::Telegram, QString("Telegram")),
		std::pair(TranslationProvider::Google, QString("Google")),
		std::pair(TranslationProvider::Yandex, QString("Yandex")),
	};
	const auto nativeAvailable = Platform::IsTranslateProviderAvailable();
	auto availableOptions = options;
	if (nativeAvailable) {
		availableOptions.push_back(std::pair(
			TranslationProvider::Native,
			[] {
				if constexpr (Platform::IsMac()) {
					return QString("macOS");
				} else if constexpr (Platform::IsWindows()) {
					return QString("Windows");
				} else {
					return QString("Linux");
				}
			}()));
	}
	auto optionLabels = std::vector<QString>();
	optionLabels.reserve(availableOptions.size());
	for (const auto &option : availableOptions) {
		optionLabels.push_back(option.second);
	}

	const auto getIndex = [=](TranslationProvider val) {
		const auto i = ranges::find(
			availableOptions,
			val,
			&std::pair<TranslationProvider, QString>::first);
		return (i != end(availableOptions))
			? int(i - begin(availableOptions))
			: 0;
	};

	auto currentVal = WhiteSettings::getInstance().translationProviderValue()
		| rpl::map(getIndex)
		| rpl::map([=](int val) { return availableOptions[val].second; });

	const auto button = builder.addButton({
		.id = u"white/translationProvider"_q,
		.title = tr::white_TranslationProvider(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			if (const auto controller = Core::App().activeWindow()->sessionController()) {
				controller->show(Box(
						[=](not_null<Ui::GenericBox*> box) {
							const auto save = [=](int index) {
								const auto option = availableOptions[index].first;
								WhiteSettings::getInstance().setTranslationProvider(option);

								if constexpr (Platform::IsMac()) {
									if (option == TranslationProvider::Native) {
										controller->showToast(Ui::Toast::Config{
											.text = tr::lng_translate_settings_use_platform_mac_about(tr::now, tr::rich),
											.duration = 6 * crl::time(1000)
										});
									}
								}
							};
							SingleChoiceBox(box, {
								.title = tr::white_TranslationProvider(),
								.options = optionLabels,
								.initialSelection = getIndex(settings->translationProvider()),
								.callback = save,
							});
						}));
			}
		},
	});
	if (button) {
		white.addBetaBadge(button);
	}
}

void BuildShowPeerId(SectionBuilder &builder) {
	auto *settings = &WhiteSettings::getInstance();

	const auto options = std::vector{
		QString(tr::white_SettingsShowID_Hide(tr::now)),
		QString("Telegram API"),
		QString("Bot API")
	};

	auto currentVal = WhiteSettings::getInstance().showPeerIdValue()
		| rpl::map([=](PeerIdDisplay val) {
			return options[static_cast<int>(val)];
		});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"white/showPeerId"_q,
		.altIds = { u"white/showIdAndDc"_q },
		.title = tr::white_SettingsShowID(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						WhiteSettings::getInstance().setShowPeerId(
							static_cast<PeerIdDisplay>(index));
					};
					SingleChoiceBox(box, {
						.title = tr::white_SettingsShowID(),
						.options = options,
						.initialSelection = static_cast<int>(settings->showPeerId()),
						.callback = save,
					});
				}));
		},
	});
}

void BuildQoLToggles(SectionBuilder &builder, WhiteSectionBuilder &white) {
	auto *settings = &WhiteSettings::getInstance();

	BuildTranslator(builder, white);
	white.addSectionDivider();

	builder.addSubsectionTitle(tr::white_CategoryGeneral());

	const auto controller = builder.controller();
	white.addToggle({
		.id = u"white/disableStories"_q,
		.altIds = { u"white/hideStories"_q },
		.title = tr::white_DisableStories(),
		.getter = [=] { return settings->disableStories(); },
		.setter = [=](bool enabled) {
			WhiteSettings::getInstance().setDisableStories(enabled);
			ShowRestartPrompt(controller);
		},
	});

	white.addSettingToggle({
		.id = u"white/disableOpenLinkWarning"_q,
		.title = tr::white_DisableOpenLinkWarning(),
		.getter = &WhiteSettings::disableOpenLinkWarning,
		.setter = &WhiteSettings::setDisableOpenLinkWarning,
	});

	white.addCollapsibleToggle({
		.id = u"white/similarChannels"_q,
		.title = tr::white_DisableSimilarChannels(),
		.checkboxes = {
			NestedEntry{
				tr::white_CollapseSimilarChannels(tr::now),
				[] { return WhiteSettings::getInstance().collapseSimilarChannels(); },
				[](bool v) { WhiteSettings::getInstance().setCollapseSimilarChannels(v); }
			},
			NestedEntry{
				tr::white_HideSimilarChannelsTab(tr::now),
				[] { return WhiteSettings::getInstance().hideSimilarChannels(); },
				[](bool v) { WhiteSettings::getInstance().setHideSimilarChannels(v); }
			}
		},
		.toggledWhenAll = true,
	});

	white.addSettingToggle({
		.id = u"white/disableNotificationsDelay"_q,
		.title = tr::white_DisableNotificationsDelay(),
		.getter = &WhiteSettings::disableNotificationsDelay,
		.setter = &WhiteSettings::setDisableNotificationsDelay,
	});

	white.addSectionDivider();

	const auto zalgoButton = builder.addButton({
		.id = u"white/filterZalgo"_q,
		.title = tr::white_FilterZalgo(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filterZalgo()),
	});
	if (zalgoButton) {
		zalgoButton->toggledValue(
		) | rpl::filter(
			[=](bool enabled) {
				return (enabled != settings->filterZalgo());
			}
		) | on_next(
			[=](bool enabled) {
				WhiteSettings::getInstance().setFilterZalgo(enabled);
				ShowRestartPrompt(controller);
			},
			zalgoButton->lifetime());
		white.addBetaBadge(zalgoButton);
	}

	white.addSettingToggle({
		.id = u"white/improveLinkPreviews"_q,
		.title = tr::white_ImproveLinkPreviews(),
		.getter = &WhiteSettings::improveLinkPreviews,
		.setter = &WhiteSettings::setImproveLinkPreviews,
	});
	white.addCollapsibleToggle({
		.id = u"white/confirmations"_q,
		.title = tr::white_ConfirmationsTitle(),
		.checkboxes = {
			NestedEntry{
				tr::white_StickerConfirmation(tr::now),
				[] { return WhiteSettings::getInstance().stickerConfirmation(); },
				[](bool v) { WhiteSettings::getInstance().setStickerConfirmation(v); }
			},
			NestedEntry{
				tr::white_GIFConfirmation(tr::now),
				[] { return WhiteSettings::getInstance().gifConfirmation(); },
				[](bool v) { WhiteSettings::getInstance().setGifConfirmation(v); }
			},
			NestedEntry{
				tr::white_VoiceConfirmation(tr::now),
				[] { return WhiteSettings::getInstance().voiceConfirmation(); },
				[](bool v) { WhiteSettings::getInstance().setVoiceConfirmation(v); }
			},
			NestedEntry{
				tr::white_RoundConfirmation(tr::now),
				[] { return WhiteSettings::getInstance().roundConfirmation(); },
				[](bool v) { WhiteSettings::getInstance().setRoundConfirmation(v); }
			}
		},
		.toggledWhenAll = false,
	});
	white.addSettingToggle({
		.id = u"white/showMessageSeconds"_q,
		.altIds = { u"white/formatTimeWithSeconds"_q },
		.title = tr::white_SettingsShowMessageSeconds(),
		.getter = &WhiteSettings::showMessageSeconds,
		.setter = &WhiteSettings::setShowMessageSeconds,
	});

	BuildShowPeerId(builder);

	white.addSectionDivider();

	builder.addSubsectionTitle(rpl::single(QString("Webview")));

	white.addSettingToggle({
		.id = u"white/spoofWebviewAsAndroid"_q,
		.title = tr::white_SettingsSpoofWebviewAsAndroid(),
		.getter = &WhiteSettings::spoofWebviewAsAndroid,
		.setter = &WhiteSettings::setSpoofWebviewAsAndroid,
	});

	white.addCollapsibleToggle({
		.id = u"white/biggerWindow"_q,
		.title = tr::white_SettingsBiggerWindow(),
		.checkboxes = {
			NestedEntry{
				tr::white_SettingsIncreaseWebviewHeight(tr::now),
				[] { return WhiteSettings::getInstance().increaseWebviewHeight(); },
				[](bool v) { WhiteSettings::getInstance().setIncreaseWebviewHeight(v); }
			},
			NestedEntry{
				tr::white_SettingsIncreaseWebviewWidth(tr::now),
				[] { return WhiteSettings::getInstance().increaseWebviewWidth(); },
				[](bool v) { WhiteSettings::getInstance().setIncreaseWebviewWidth(v); }
			}
		},
		.toggledWhenAll = false,
	});
}

} // namespace

void BuildAyuGeneralContent(SectionBuilder &builder) {
	auto white = WhiteSectionBuilder(builder);

	builder.addSkip();
	BuildQoLToggles(builder, white);
	builder.addSkip();
}

} // namespace Settings
