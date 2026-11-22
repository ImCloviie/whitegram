// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "white/white_infra.h"

#include "white/white_lang.h"
#include "white/white_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "white/white_worker.h"
#include "white/data/white_database.h"
#include "white/features/connection_bypass/connection_bypass.h"
#include "white/features/tg_ws_proxy/tg_ws_proxy.h"
#include "white/features/glass_effect/glass_effect.h"
#include "white/features/whitegram_backend/whitegram_backend.h"
#include "white/features/whitegram_e2ee/whitegram_e2ee.h"
#include "white/ui/white_logo.h"
#include "features/translator/white_translator.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"

#include "rpl/lifetime.h"

#ifdef Q_OS_WIN
#include "white/utils/windows_utils.h"
#endif

namespace WhiteInfra {

rpl::lifetime &langLifetime() {
	static rpl::lifetime lifetime;
	return lifetime;
}

void initLang() {
	WhiteLanguage::init();

	const auto syncLangSettings = [=](const QString &langId, bool force) {
		const auto curId = Lang::LanguageIdOrDefault(langId);
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = curId.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		WhiteSettings::getInstance().setCryptoFiatCurrencyFromLanguage(
			isRu ? 2 : 0,
			force);
	};

	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	if (!id.isEmpty()) {
		WhiteLanguage::currentInstance()->fetchLanguage(id, baseId);
	}

	syncLangSettings(id, false);

	Lang::GetInstance().idChanges(
	) | rpl::on_next([=](const QString &newId) {
		const auto baseId = Lang::GetInstance().baseId();
		if (!newId.isEmpty()) {
			WhiteLanguage::currentInstance()->fetchLanguage(newId, baseId);
		}
		syncLangSettings(newId, true);
	}, langLifetime());
}

void initUiSettings() {
	const auto &settings = WhiteSettings::getInstance();

	AyuUiSettings::setMonoFont(settings.monoFont());
	AyuUiSettings::setWideMultiplier(settings.wideMultiplier());
	AyuUiSettings::setMaterialSwitches(settings.materialSwitches());
	AyuUiSettings::setAvatarCorners(settings.avatarCorners());
	AyuUiSettings::setTypingAnimation(
		static_cast<AyuUiSettings::TypingAnimation>(settings.typingAnimation()));
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
}

void initDatabase() {
	WhiteDatabase::initialize();
}

void initWorker() {
	WhiteWorker::initialize();
}

void initTranslator() {
	White::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	WhiteAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif
}

void initConnectionBypass() {
	WhiteFeatures::ConnectionBypass::init();
	WhiteFeatures::TgWsProxy::init();
}

void initGlassEffect() {
	WhiteFeatures::GlassEffect::init();
}

void init() {
	initLang();
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initTranslator();
	initConnectionBypass();
	initGlassEffect();
	Whitegram::Backend::InitClientInfo();
	(void)Whitegram::E2EE::GetOrGenerateIdentityKeyPair();
	WhiteSettings::initTimeSpentTracker();
}

}
