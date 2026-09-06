// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/experimental_settings_i18n.h"

#include "lang/lang_instance.h"

namespace Settings {
namespace {

struct Entry {
	const char *ruName = nullptr;
	const char *enName = nullptr;
	const char *ruDescription = nullptr;
	const char *enDescription = nullptr;
};

[[nodiscard]] const QHash<QString, Entry> &Entries() {
	static const auto result = [] {
		auto map = QHash<QString, Entry>();
		const auto add = [&](
				const char *id,
				const char *ruName,
				const char *enName,
				const char *ruDescription,
				const char *enDescription) {
			map.insert(
				QString::fromLatin1(id),
				Entry{ ruName, enName, ruDescription, enDescription });
		};

		add(
			"forum-hide-chats-list",
			"Скрыть список чатов в форумах",
			"Hide chat list in forums",
			"Не показывать узкую колонку со списком чатов.",
			"Don't keep a narrow column of chat list.");
		add(
			"dialogs-unread-on-top",
			"Держать непрочитанные без звука сверху",
			"Keep unmuted unread chats on top",
			"Сортировать чаты с новыми непрочитанными сообщениями "
			"без звука сразу под закреплёнными и оставлять там, "
			"пока вы их не прочитаете.",
			"Sort chats with new unmuted messages right below the "
			"pinned ones and keep them there until you read them.");
		add(
			"dialogs-mute-icon",
			"Иконка без звука в списке чатов",
			"Mute icon in dialogs",
			"Показывать маленькую иконку без звука рядом с названием "
			"чата для чатов без уведомлений.",
			"Show a small mute icon next to the chat name "
			"for muted chats.");
		add(
			"auto-scroll-inactive-chat",
			"Отмечать прочитанным в неактивном чате",
			"Mark as read of inactive chat",
			"Отмечать новые сообщения прочитанными и прокручивать чат, "
			"даже когда окно не в фокусе.",
			"Mark new messages as read and scroll the chat "
			"even when the window is not in focus.");
		add(
			"moderate-common-groups",
			"Банить пользователей сразу из нескольких групп.",
			"Ban users from several groups at once.",
			nullptr,
			nullptr);
		add(
			"classic-profile-scroll",
			"Использовать классическую прокрутку профиля.",
			"Use classic profile scroll processing.",
			"Вернуть прежнюю (на основе заполнителя) реализацию "
			"прокрутки обложки профиля.",
			"Reverts the profile cover scroll to the previous "
			"(filler-based) implementation.");
		add(
			"use-small-msg-bubble-radius",
			"Маленький радиус пузырьков сообщений",
			"Use small message bubble radius",
			"Делает большинство пузырьков сообщений более квадратными.",
			"Makes most message bubbles square-ish.");
		add(
			"unlimited-message-width",
			"Неограниченная ширина сообщений",
			"Unlimited message width",
			"Разрешить текстовым пузырькам расширяться за пределы "
			"стандартной максимальной ширины.",
			"Allow text-only message bubbles "
			"to expand beyond the default maximum width.");
		add(
			"mac-cmd-reply-immediately",
			"Mac: мгновенный ответ по Cmd + Up/Down",
			"Mac: instant reply on Cmd + Up/Down",
			"Отвечать на предыдущее или следующее сообщение сразу "
			"по Cmd + Up/Down, вместо перемещения курсора в начало "
			"или конец поля ввода. Удерживайте Shift для перемещения "
			"курсора как раньше.",
			"Reply to the previous or next message right away on "
			"Cmd + Up/Down, instead of first moving the text cursor "
			"to the start or end of the input field. Hold Shift to "
			"move the cursor as before.");
		add(
			"hide-ai-button",
			"Скрыть кнопку ИИ",
			"Hide AI button",
			"Скрыть кнопку инструментов ИИ в поле ввода сообщения.",
			"Hide the AI Tools button in message compose fields.");
		add(
			"force-compose-search-one-column",
			"Принудительный поиск в чатах",
			"Force embedded search in chats",
			"Принудительно использовать встроенный поиск в чатах "
			"в одноколоночном режиме.",
			"Force in one-column mode the embedded search in chats.");
		add(
			"view-profile-in-chats-list-context-menu",
			"Добавить «Посмотреть профиль»",
			"Add \"View Profile\"",
			"Добавить «Посмотреть профиль» в контекстное меню "
			"списка чатов.",
			"Add \"View Profile\" to context menu in chat list");
		add(
			"show-peer-id-below-about",
			"Показывать ID пользователей в профиле",
			"Show Peer IDs in Profile",
			"Показывать ID из API под био / описанием. "
			"Добавлять ID контактов в экспортируемые данные.",
			"Show peer IDs from API below their Bio / Description."
			" Add contact IDs to exported data.");
		add(
			"show-channel-joined-below-about",
			"Показывать дату вступления в канал",
			"Show Channel Joined Date in Profile",
			"Показывать, когда вы вступили в канал, под его описанием.",
			"Show when you join Channel under its Description.");
		add(
			"profile-media-tabs",
			"Показывать общие медиа вкладками в профиле.",
			"Show shared media as tabs in the profile.",
			"Заменить кнопки общих медиа в профилях полосой вкладок "
			"со списками медиа. В разработке.",
			"Replace the shared media buttons in profiles with "
			"a strip of tabs holding the media lists inline. "
			"Work in progress.");
		add(
			"profile-media-tabs-expanded",
			"Разделить вкладку медиа на фото и видео.",
			"Split profile media tab into photos and videos.",
			"Показывать отдельные вкладки фото и видео в профилях "
			"вместо одной общей.",
			"Show separate photo and video tabs in profiles instead "
			"of a single combined media tab.");
		add(
			"tabbed-panel-show-on-click",
			"Показывать панель вкладок по клику",
			"Show tabbed panel by click",
			"Показывать панель эмодзи / стикеров / GIF только "
			"после клика.",
			"Show Emoji / Stickers / GIFs panel only after a click.");
		add(
			"unlimited-recent-stickers",
			"Неограниченное число недавних стикеров",
			"Unlimited recent stickers",
			"Показывать столько недавних стикеров, "
			"сколько предоставляет сервер.",
			"Display as much recent stickers as the server provides");
		add(
			"disable-autoplay-next",
			"Отключить автовоспроизведение следующего трека",
			"Disable auto-play of the next track",
			"Отключить автовоспроизведение следующего аудиофайла, "
			"голосового или видеосообщения.",
			"Disable auto-play of the next "
			"Audio file / Voice Message / Video message.");
		add(
			"external-media-viewer",
			"Внешний просмотрщик медиа",
			"External media viewer",
			"Использовать системный просмотрщик медиа "
			"вместо встроенного.",
			"Use system media viewer instead of the internal one.");
		add(
			"ffmpeg-multi-thread",
			"Многопоточное декодирование видео",
			"Multi-thread video decoding",
			"Разрешить FFmpeg использовать пул потоков для декодирования, "
			"обычно по одному на ядро CPU.",
			"Allow FFmpeg to use a thread pool for decoding,"
			" typically a thread per CPU thread.");
		add(
			"custom-notification",
			"Принудительная доступность не нативных уведомлений",
			"Force non-native notifications availability",
			"Разрешить отключать нативные уведомления, даже если "
			"пользовательские уведомления на этой платформе "
			"работают некорректно.",
			"Allow to disable native notifications"
			" even if custom notifications are broken on this platform");
		add(
			"gnotification",
			"GNotification",
			"GNotification",
			"Принудительно включить GNotification из GLib. "
			"При отключении используется автоматическое определение.",
			"Force enable GLib's GNotification."
			" When disabled, autodetect is used.");
		add(
			"mac-modern-notifications",
			"Современные уведомления macOS",
			"Modern macOS notifications",
			"Использовать UserNotifications для нативных уведомлений "
			"(macOS 10.14+). Система запросит разрешение при первом запуске.",
			"Use UserNotifications framework"
			" for native notifications (macOS 10.14+)."
			" System asks for notifications permission on first launch.");
		add(
			"hide-reply-button",
			"Скрыть кнопку ответа",
			"Hide reply button",
			"Скрыть кнопку ответа в уведомлениях.",
			"Hide reply button in notifications.");
		add(
			"fractional-scaling-enabled",
			"Точное масштабирование High DPI",
			"Enable precise High DPI scaling",
			"Точно следовать системным настройкам масштаба интерфейса.",
			"Follow system interface scale settings exactly.");
		add(
			"high-dpi-downscale",
			"High DPI downscale",
			"High DPI downscale",
			"Точно следовать системным настройкам масштаба интерфейса "
			"(другой подход, вероятно лучшее качество).",
			"Follow system interface scale settings exactly"
			" (another approach, likely better quality).");
		add(
			"use-qt-rhi",
			"Рендерер Qt RHI",
			"Use Qt RHI renderer",
			"Рендерить главное окно на GPU вместо CPU.",
			"Render the main window on the GPU instead of the CPU.");
		add(
			"enable-vulkan-rhi",
			"Рендерер Vulkan",
			"Enable Vulkan renderer",
			"Использовать Vulkan для GPU-рендеринга вместо OpenGL, "
			"когда доступен.",
			"Use Vulkan for GPU rendering "
			"instead of OpenGL when it is available.");
		add(
			"freetype",
			"Движок шрифтов FreeType",
			"FreeType font engine",
			"Использовать движок шрифтов из Linux вместо системного.",
			"Use the font engine from Linux instead of the system one.");
		add(
			"qscroller",
			"QScroller для прокрутки тачпадом",
			"Use QScroller for touchpad scrolling",
			"Обеспечивает инерционную прокрутку.",
			"Provides kinetic scrolling");
		add(
			"touchbar-disabled",
			"Отключить Touch Bar (только macOS).",
			"Disable Touch Bar (macOS only).",
			nullptr,
			nullptr);
		add(
			"new-windows-size-as-first",
			"Размер новых окон чатов",
			"Adjust size of new chat windows",
			"Открывать новые окна с размером главного окна.",
			"Open new windows with a size of the main window.");
		add(
			"prefer-ipv6",
			"Предпочитать IPv6",
			"Prefer IPv6",
			"Предпочитать IPv6, если доступен. Требует включения "
			"«Пробовать подключаться через IPv6».",
			"Prefer IPv6 if it is available. Require "
			"\"Try connecting through IPv6\" to be enabled");
		add(
			"skip-url-scheme-register",
			"Пропустить регистрацию URL-схемы",
			"Skip URL scheme register",
			"Не перерегистрировать схему tg:// при автообновлении.",
			"Don't re-register tg:// URL scheme on autoupdate.");
		add(
			"deadlock-detector",
			"Детектор зависаний",
			"Deadlock Detector",
			"Проверять каждые 30 секунд, что главный поток "
			"всё ещё отвечает.",
			"Check once every 30 seconds that main thread "
			"is still responsive.");
		add(
			"webview-debug-enabled",
			"Инспектирование webview",
			"Enable webview inspecting",
			"ПКМ и «Inspect» в окнах webview. "
			"(на macOS запустите Safari, откройте из меню Develop)",
			"Right click and choose Inspect in the webview windows. "
			"(on macOS launch Safari, open from Develop menu)");
		add(
			"webview-legacy-edge",
			"Принудительно legacy Edge WebView",
			"Force legacy Edge WebView",
			"Пропустить проверку CoreWebView2 и использовать "
			"legacy Edge WebView в Windows.",
			"Skip modern CoreWebView2 check and force using "
			"legacy Edge WebView on Windows.");
		add(
			"fast-buttons-mode",
			"Быстрый режим кнопок",
			"Fast buttons mode",
			"Активировать inline-кнопки клавишами 1–9.",
			"Trigger inline keyboard buttons by 1-9 keyboard keys.");
		add(
			"folder-favorite-link",
			"Кнопка избранной папки",
			"Favorite folder button",
			"Кнопка внизу полосы папок, открывающая ссылку tg:// или t.me/.",
			"Favorite button at the bottom of the folders strip "
			"that opens a tg:// or t.me/ link.");

		return map;
	}();
	return result;
}

[[nodiscard]] const QHash<QString, QString> &CategoryTitles() {
	static const auto result = [] {
		auto map = QHash<QString, QString>();
		map.insert(u"Chats"_q, u"Чаты"_q);
		map.insert(u"Messages"_q, u"Сообщения"_q);
		map.insert(u"Profile"_q, u"Профиль"_q);
		map.insert(u"Stickers and emoji"_q, u"Стикеры и эмодзи"_q);
		map.insert(u"Media"_q, u"Медиа"_q);
		map.insert(u"Notifications"_q, u"Уведомления"_q);
		map.insert(u"Interface"_q, u"Интерфейс"_q);
		map.insert(u"System"_q, u"Система"_q);
		map.insert(u"Other"_q, u"Прочее"_q);
		return map;
	}();
	return result;
}

[[nodiscard]] const Entry *FindEntry(const QString &id) {
	const auto &entries = Entries();
	const auto it = entries.find(id);
	return (it != entries.end()) ? &it.value() : nullptr;
}

[[nodiscard]] QString Pick(const char *ru, const char *en, const QString &fallback) {
	if (IsWhitegramRussian()) {
		if (ru) {
			return QString::fromUtf8(ru);
		}
	} else if (en) {
		return QString::fromUtf8(en);
	}
	return fallback;
}

} // namespace

bool IsWhitegramRussian() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
}

QString ExperimentalOptionName(const QString &id, const QString &fallback) {
	if (const auto entry = FindEntry(id)) {
		return Pick(entry->ruName, entry->enName, fallback);
	}
	return fallback;
}

QString ExperimentalOptionDescription(
		const QString &id,
		const QString &fallback) {
	if (const auto entry = FindEntry(id)) {
		return Pick(entry->ruDescription, entry->enDescription, fallback);
	}
	return fallback;
}

QString ExperimentalCategoryTitle(const QString &title) {
	if (!IsWhitegramRussian()) {
		return title;
	}
	const auto &categories = CategoryTitles();
	const auto it = categories.find(title);
	return (it != categories.end()) ? it.value() : title;
}

} // namespace Settings
