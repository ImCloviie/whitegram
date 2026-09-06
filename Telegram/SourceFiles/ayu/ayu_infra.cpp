// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_infra.h"

#include "ayu/ayu_lang.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ayu_worker.h"
#include "ayu/data/ayu_database.h"
#include "ayu/features/connection_bypass/connection_bypass.h"
#include "ayu/features/whitegram_backend/whitegram_backend.h"
#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "ayu/ui/ayu_logo.h"
#include "features/translator/ayu_translator.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"

#include "rpl/lifetime.h"

#ifdef Q_OS_WIN
#include "ayu/utils/windows_utils.h"
#endif

namespace AyuInfra {

rpl::lifetime &langLifetime() {
	static rpl::lifetime lifetime;
	return lifetime;
}

void initLang() {
	AyuLanguage::init();

	const auto syncLangSettings = [=](const QString &langId, bool force) {
		const auto curId = Lang::LanguageIdOrDefault(langId);
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = curId.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		AyuSettings::getInstance().setCryptoFiatCurrencyFromLanguage(
			isRu ? 2 : 0,
			force);
	};

	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	if (!id.isEmpty()) {
		AyuLanguage::currentInstance()->fetchLanguage(id, baseId);
	}

	syncLangSettings(id, false);

	Lang::GetInstance().idChanges(
	) | rpl::on_next([=](const QString &newId) {
		const auto baseId = Lang::GetInstance().baseId();
		if (!newId.isEmpty()) {
			AyuLanguage::currentInstance()->fetchLanguage(newId, baseId);
		}
		syncLangSettings(newId, true);
	}, langLifetime());
}

void initUiSettings() {
	const auto &settings = AyuSettings::getInstance();

	AyuUiSettings::setMonoFont(settings.monoFont());
	AyuUiSettings::setWideMultiplier(settings.wideMultiplier());
	AyuUiSettings::setMaterialSwitches(settings.materialSwitches());
	AyuUiSettings::setAvatarCorners(settings.avatarCorners());
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
}

void initDatabase() {
	AyuDatabase::initialize();
}

void initWorker() {
	AyuWorker::initialize();
}

void initRCManager() {
	RCManager::getInstance().start();
}

void initTranslator() {
	Ayu::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	AyuAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif
}

void initConnectionBypass() {
	AyuFeatures::ConnectionBypass::init();
}

void init() {
	initLang();
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initRCManager();
	initTranslator();
	initConnectionBypass();
	Whitegram::Backend::InitClientInfo();
	(void)Whitegram::E2EE::GetOrGenerateIdentityKeyPair();
	AyuSettings::initTimeSpentTracker();
}

}
