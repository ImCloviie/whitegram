// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_settings.h"

#include "lang_auto.h"
#include "tray.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ayu_worker.h"
#include "ayu/features/connection_bypass/connection_bypass.h"
#include "ayu/features/streamer_mode/streamer_mode.h"
#include "ayu/ui/ayu_logo.h"
#include "core/application.h"
#include "media/audio/media_audio_track.h"
#include "features/filters/filters_cache_controller.h"
#include "features/translator/ayu_translator.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "platform/platform_translate_provider.h"
#include "rpl/combine.h"
#include "window/window_controller.h"

#include <array>
#include <algorithm>
#include <fstream>
#include <QApplication>
#include <QFile>
#include <QTime>
#include <QDate>
#include <QDateTime>
#include "base/timer.h"
#include "base/random.h"

#include <QCryptographicHash>

using json = nlohmann::json;

namespace {

std::string getSettingsPath() {
	return (cWorkingDir() + u"tdata/ayu_settings.json"_q).toStdString();
}

void repaintApp() {
	for (QWidget *widget : QApplication::allWidgets()) {
		widget->update();
	}
}

rpl::lifetime lifetime; // idk reactivity dies when placed in `GhostModeAccountSettings` as field

} // namespace

GhostModeAccountSettings::GhostModeAccountSettings() {
	rpl::combine(
		_sendReadMessages.value(),
		_sendReadMessagesLocked.value(),
		_sendReadStories.value(),
		_sendReadStoriesLocked.value(),
		_sendOnlinePackets.value(),
		_sendOnlinePacketsLocked.value(),
		_sendUploadProgress.value(),
		_sendUploadProgressLocked.value(),
		_sendOfflinePacketAfterOnline.value(),
		_sendOfflinePacketAfterOnlineLocked.value()
	) | rpl::on_next([=](
			bool readMsg, bool readMsgLocked,
			bool readStories, bool readStoriesLocked,
			bool online, bool onlineLocked,
			bool upload, bool uploadLocked,
			bool offline, bool offlineLocked) {
		_ghostModeActive = (readMsgLocked || !readMsg)
			&& (readStoriesLocked || !readStories)
			&& (onlineLocked || !online)
			&& (uploadLocked || !upload)
			&& (offlineLocked || offline);
	}, lifetime);
}

void GhostModeAccountSettings::setSendReadMessages(bool val) {
	if (_sendReadMessages.current() == val) return;
	_sendReadMessages = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendReadStories(bool val) {
	if (_sendReadStories.current() == val) return;
	_sendReadStories = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendOnlinePackets(bool val) {
	if (_sendOnlinePackets.current() == val) return;
	_sendOnlinePackets = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendUploadProgress(bool val) {
	if (_sendUploadProgress.current() == val) return;
	_sendUploadProgress = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendOfflinePacketAfterOnline(bool val) {
	if (_sendOfflinePacketAfterOnline.current() == val) return;
	_sendOfflinePacketAfterOnline = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setMarkReadAfterAction(bool val) {
	if (_markReadAfterAction.current() == val) return;
	_markReadAfterAction = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setUseScheduledMessages(bool val) {
	if (_useScheduledMessages.current() == val) return;
	_useScheduledMessages = val;
	AyuSettings::save();
}

bool GhostModeAccountSettings::shouldSendWithoutSound() const {
	switch (_sendWithoutSound.current()) {
	case SendWithoutSoundOption::Never:
		return false;
	case SendWithoutSoundOption::InGhostMode:
		return isGhostModeActive();
	case SendWithoutSoundOption::Always:
		return true;
	}
	Unexpected("Value in GhostModeAccountSettings::shouldSendWithoutSound.");
}

void GhostModeAccountSettings::setSendWithoutSound(
		SendWithoutSoundOption val) {
	if (_sendWithoutSound.current() == val) return;
	_sendWithoutSound = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSuggestGhostModeBeforeViewingStory(bool val) {
	if (_suggestGhostModeBeforeViewingStory.current() == val) return;
	_suggestGhostModeBeforeViewingStory = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setGhostModeEnabled(bool val) {
	if (!_sendReadMessagesLocked.current()) _sendReadMessages = !val;
	if (!_sendReadStoriesLocked.current()) _sendReadStories = !val;
	if (!_sendOnlinePacketsLocked.current()) _sendOnlinePackets = !val;
	if (!_sendUploadProgressLocked.current()) _sendUploadProgress = !val;
	if (!_sendOfflinePacketAfterOnlineLocked.current()) _sendOfflinePacketAfterOnline = val;
	AyuSettings::save();

	if (val) {
		if (const auto window = Core::App().activeWindow()) {
			if (const auto session = window->maybeSession()) {
				AyuWorker::markAsOnline(session);
			}
		}
	}
}

void GhostModeAccountSettings::setSendReadMessagesLocked(bool val) {
	if (_sendReadMessagesLocked.current() == val) return;
	_sendReadMessagesLocked = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendReadStoriesLocked(bool val) {
	if (_sendReadStoriesLocked.current() == val) return;
	_sendReadStoriesLocked = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendOnlinePacketsLocked(bool val) {
	if (_sendOnlinePacketsLocked.current() == val) return;
	_sendOnlinePacketsLocked = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendUploadProgressLocked(bool val) {
	if (_sendUploadProgressLocked.current() == val) return;
	_sendUploadProgressLocked = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setSendOfflinePacketAfterOnlineLocked(bool val) {
	if (_sendOfflinePacketAfterOnlineLocked.current() == val) return;
	_sendOfflinePacketAfterOnlineLocked = val;
	AyuSettings::save();
}

void GhostModeAccountSettings::setGhostTargetMode(GhostTargetMode mode) {
	if (_ghostTargetMode.current() == mode) return;
	_ghostTargetMode = mode;
	AyuSettings::save();
}

void GhostModeAccountSettings::setGhostTargetPeers(const std::vector<uint64> &peers) {
	if (_ghostTargetPeers.current() == peers) return;
	_ghostTargetPeers = peers;
	AyuSettings::save();
}

void GhostModeAccountSettings::setGhostExcludedPeers(const std::vector<uint64> &peers) {
	if (_ghostExcludedPeers.current() == peers) return;
	_ghostExcludedPeers = peers;
	AyuSettings::save();
}

bool GhostModeAccountSettings::isPeerInTargetList(uint64 peerId) const {
	const auto &list = _ghostTargetPeers.current();
	return std::find(list.begin(), list.end(), peerId) != list.end();
}

bool GhostModeAccountSettings::isPeerInExcludedList(uint64 peerId) const {
	const auto &list = _ghostExcludedPeers.current();
	return std::find(list.begin(), list.end(), peerId) != list.end();
}

void GhostModeAccountSettings::togglePeerInTargetList(uint64 peerId) {
	auto list = _ghostTargetPeers.current();
	const auto it = std::find(list.begin(), list.end(), peerId);
	if (it != list.end()) {
		list.erase(it);
	} else {
		list.push_back(peerId);
	}
	setGhostTargetPeers(list);
}

void GhostModeAccountSettings::togglePeerInExcludedList(uint64 peerId) {
	auto list = _ghostExcludedPeers.current();
	const auto it = std::find(list.begin(), list.end(), peerId);
	if (it != list.end()) {
		list.erase(it);
	} else {
		list.push_back(peerId);
	}
	setGhostExcludedPeers(list);
}

bool GhostModeAccountSettings::isGhostActiveForPeer(uint64 peerId) const {
	if (!isGhostModeActive()) {
		return false;
	}
	const auto mode = ghostTargetMode();
	if (mode == GhostTargetMode::Global) {
		return true;
	}
	if (mode == GhostTargetMode::Whitelist) {
		return isPeerInTargetList(peerId);
	}
	if (mode == GhostTargetMode::Blacklist) {
		return !isPeerInExcludedList(peerId);
	}
	return true;
}

bool GhostModeAccountSettings::shouldSendReadMessagesForPeer(uint64 peerId) const {
	if (!isGhostActiveForPeer(peerId)) {
		return true;
	}
	return sendReadMessages();
}

bool GhostModeAccountSettings::shouldSendReadStoriesForPeer(uint64 peerId) const {
	if (!isGhostActiveForPeer(peerId)) {
		return true;
	}
	return sendReadStories();
}

bool GhostModeAccountSettings::shouldSendUploadProgressForPeer(uint64 peerId) const {
	if (!isGhostActiveForPeer(peerId)) {
		return true;
	}
	return sendUploadProgress();
}

GhostModeAccountSettings::~GhostModeAccountSettings() = default;

bool GhostModeAccountSettings::isInScheduleWindow() const {
	if (!_ghostScheduleEnabled.current() || _ghostScheduleEntries.empty()) {
		return false;
	}
	const auto nowTime = QTime::currentTime();
	const auto currentMinutes = nowTime.hour() * 60 + nowTime.minute();
	const auto today = QDate::currentDate();
	const auto todayDayOfWeek = today.dayOfWeek();
	const auto todayDayBit = 1 << (todayDayOfWeek - 1);
	const auto yesterdayDayOfWeek = (todayDayOfWeek == 1) ? 7 : (todayDayOfWeek - 1);
	const auto yesterdayDayBit = 1 << (yesterdayDayOfWeek - 1);

	for (const auto &entry : _ghostScheduleEntries) {
		if (entry.from <= entry.to) {
			if ((entry.weekdays & todayDayBit) != 0) {
				if (currentMinutes >= entry.from && currentMinutes < entry.to) {
					return true;
				}
			}
		} else {
			if (currentMinutes >= entry.from) {
				if ((entry.weekdays & todayDayBit) != 0) {
					return true;
				}
			}
			if (currentMinutes < entry.to) {
				if ((entry.weekdays & yesterdayDayBit) != 0) {
					return true;
				}
			}
		}
	}
	return false;
}

void GhostModeAccountSettings::checkSchedule() {
	if (!_ghostScheduleEnabled.current()) {
		return;
	}
	const auto shouldBeActive = isInScheduleWindow();
	const auto currentlyActive = isGhostModeActive();

	if (shouldBeActive && !currentlyActive) {
		_scheduleAppliedGhost = true;
		setGhostModeEnabled(true);
	} else if (!shouldBeActive && currentlyActive && _scheduleAppliedGhost) {
		_scheduleAppliedGhost = false;
		setGhostModeEnabled(false);
	}
}

void GhostModeAccountSettings::startScheduleTimer() {
	if (!_scheduleTimer) {
		_scheduleTimer = std::make_unique<base::Timer>([=] {
			checkSchedule();
		});
	}
	if (!_scheduleTimer->isActive()) {
		_scheduleTimer->callEach(30 * crl::time(1000));
	}
	checkSchedule();
}

void GhostModeAccountSettings::stopScheduleTimer() {
	if (_scheduleTimer) {
		_scheduleTimer->cancel();
	}
}

void GhostModeAccountSettings::setGhostScheduleEnabled(bool val) {
	if (_ghostScheduleEnabled.current() == val) return;
	_ghostScheduleEnabled = val;
	if (val) {
		if (_ghostScheduleEntries.empty()) {
			_ghostScheduleEntries.push_back(GhostScheduleEntry{ 540, 1080, 0x7F });
			_ghostScheduleEntriesChanges.fire({});
		}
		startScheduleTimer();
	} else {
		stopScheduleTimer();
	}
	AyuSettings::save();
}

void GhostModeAccountSettings::setGhostScheduleEntries(const std::vector<GhostScheduleEntry> &entries) {
	_ghostScheduleEntries = entries;
	_ghostScheduleEntriesChanges.fire({});
	if (_ghostScheduleEnabled.current()) {
		checkSchedule();
	}
	AyuSettings::save();
}

void GhostModeAccountSettings::addGhostScheduleEntry(const GhostScheduleEntry &entry) {
	_ghostScheduleEntries.push_back(entry);
	_ghostScheduleEntriesChanges.fire({});
	if (_ghostScheduleEnabled.current()) {
		checkSchedule();
	}
	AyuSettings::save();
}

void GhostModeAccountSettings::removeGhostScheduleEntry(int index) {
	if (index >= 0 && index < int(_ghostScheduleEntries.size())) {
		_ghostScheduleEntries.erase(_ghostScheduleEntries.begin() + index);
		_ghostScheduleEntriesChanges.fire({});
		if (_ghostScheduleEnabled.current()) {
			checkSchedule();
		}
		AyuSettings::save();
	}
}

void GhostModeAccountSettings::updateGhostScheduleEntry(int index, const GhostScheduleEntry &entry) {
	if (index >= 0 && index < int(_ghostScheduleEntries.size())) {
		_ghostScheduleEntries[index] = entry;
		_ghostScheduleEntriesChanges.fire({});
		if (_ghostScheduleEnabled.current()) {
			checkSchedule();
		}
		AyuSettings::save();
	}
}

void to_json(nlohmann::json &j, const GhostModeAccountSettings &s) {
	j = nlohmann::json{
		{"sendReadMessages", s._sendReadMessages.current()},
		{"sendReadStories", s._sendReadStories.current()},
		{"sendOnlinePackets", s._sendOnlinePackets.current()},
		{"sendUploadProgress", s._sendUploadProgress.current()},
		{"sendOfflinePacketAfterOnline", s._sendOfflinePacketAfterOnline.current()},
		{"markReadAfterAction", s._markReadAfterAction.current()},
		{"useScheduledMessages", s._useScheduledMessages.current()},
		{"sendWithoutSound", s._sendWithoutSound.current()},
		{"suggestGhostModeBeforeViewingStory", s._suggestGhostModeBeforeViewingStory.current()},
		{"sendReadMessagesLocked", s._sendReadMessagesLocked.current()},
		{"sendReadStoriesLocked", s._sendReadStoriesLocked.current()},
		{"sendOnlinePacketsLocked", s._sendOnlinePacketsLocked.current()},
		{"sendUploadProgressLocked", s._sendUploadProgressLocked.current()},
		{"sendOfflinePacketAfterOnlineLocked", s._sendOfflinePacketAfterOnlineLocked.current()},
		{"ghostTargetMode", s._ghostTargetMode.current()},
		{"ghostTargetPeers", s._ghostTargetPeers.current()},
		{"ghostExcludedPeers", s._ghostExcludedPeers.current()},
		{"ghostScheduleEnabled", s._ghostScheduleEnabled.current()},
		{"ghostScheduleEntries", s._ghostScheduleEntries}
	};
}

void from_json(const nlohmann::json &j, GhostModeAccountSettings &s) {
	s._sendReadMessages = j.value("sendReadMessages", true);
	s._sendReadStories = j.value("sendReadStories", true);
	s._sendOnlinePackets = j.value("sendOnlinePackets", true);
	s._sendUploadProgress = j.value("sendUploadProgress", true);
	s._sendOfflinePacketAfterOnline = j.value("sendOfflinePacketAfterOnline", false);
	s._markReadAfterAction = j.value("markReadAfterAction", true);
	s._useScheduledMessages = j.value("useScheduledMessages", false);
	const auto sendWithoutSound = j.find("sendWithoutSound");
	s._sendWithoutSound = (sendWithoutSound == j.end())
		? SendWithoutSoundOption::Never
		: sendWithoutSound->is_boolean()
		? (sendWithoutSound->get<bool>()
			? SendWithoutSoundOption::Always
			: SendWithoutSoundOption::Never)
		: sendWithoutSound->get<SendWithoutSoundOption>();
	s._suggestGhostModeBeforeViewingStory = j.value("suggestGhostModeBeforeViewingStory", true);
	s._sendReadMessagesLocked = j.value("sendReadMessagesLocked", false);
	s._sendReadStoriesLocked = j.value("sendReadStoriesLocked", false);
	s._sendOnlinePacketsLocked = j.value("sendOnlinePacketsLocked", false);
	s._sendUploadProgressLocked = j.value("sendUploadProgressLocked", false);
	s._sendOfflinePacketAfterOnlineLocked = j.value("sendOfflinePacketAfterOnlineLocked", false);
	s._ghostTargetMode = j.value("ghostTargetMode", GhostTargetMode::Global);
	s._ghostTargetPeers = j.value("ghostTargetPeers", std::vector<uint64>{});
	s._ghostExcludedPeers = j.value("ghostExcludedPeers", std::vector<uint64>{});
	s._ghostScheduleEnabled = j.value("ghostScheduleEnabled", false);
	if (j.contains("ghostScheduleEntries") && j["ghostScheduleEntries"].is_array()) {
		j["ghostScheduleEntries"].get_to(s._ghostScheduleEntries);
	}
	if (s._ghostScheduleEnabled.current()) {
		s.startScheduleTimer();
	}
}

AccountNotificationSettings::AccountNotificationSettings() {
}

void AccountNotificationSettings::setEnabled(bool val) {
	if (_enabled.current() == val) return;
	_enabled = val;
	AyuSettings::save();
}

void AccountNotificationSettings::setSoundEnabled(bool val) {
	if (_soundEnabled.current() == val) return;
	_soundEnabled = val;
	AyuSettings::save();
}

void AccountNotificationSettings::setIncludeInUnreadCounter(bool val) {
	if (_includeInUnreadCounter.current() == val) return;
	_includeInUnreadCounter = val;
	Core::App().domain().notifyUnreadBadgeChanged();
	AyuSettings::save();
}

void AccountNotificationSettings::setShowUnreadBadge(bool val) {
	if (_showUnreadBadge.current() == val) return;
	_showUnreadBadge = val;
	Core::App().domain().notifyUnreadBadgeChanged();
	AyuSettings::save();
}

void AccountNotificationSettings::setCompletelyMute(bool val) {
	if (_completelyMute.current() == val) return;
	_completelyMute = val;
	Core::App().domain().notifyUnreadBadgeChanged();
	AyuSettings::save();
}

void to_json(nlohmann::json &j, const AccountNotificationSettings &s) {
	j = nlohmann::json{
		{"enabled", s._enabled.current()},
		{"soundEnabled", s._soundEnabled.current()},
		{"includeInUnreadCounter", s._includeInUnreadCounter.current()},
		{"showUnreadBadge", s._showUnreadBadge.current()},
		{"completelyMute", s._completelyMute.current()}
	};
}

void from_json(const nlohmann::json &j, AccountNotificationSettings &s) {
	s._enabled = j.value("enabled", true);
	s._soundEnabled = j.value("soundEnabled", true);
	s._includeInUnreadCounter = j.value("includeInUnreadCounter", true);
	s._showUnreadBadge = j.value("showUnreadBadge", true);
	s._completelyMute = j.value("completelyMute", false);
}

void MessageShotSettings::setShowBackground(bool val) {
	if (_showBackground.current() == val) return;
	_showBackground = val;
	AyuSettings::save();
}

void MessageShotSettings::setShowDate(bool val) {
	if (_showDate.current() == val) return;
	_showDate = val;
	AyuSettings::save();
}

void MessageShotSettings::setShowReactions(bool val) {
	if (_showReactions.current() == val) return;
	_showReactions = val;
	AyuSettings::save();
}

void MessageShotSettings::setShowHeaderDecorations(bool val) {
	if (_showHeaderDecorations.current() == val) return;
	_showHeaderDecorations = val;
	AyuSettings::save();
}

void MessageShotSettings::setShowColorfulReplies(bool val) {
	if (_showColorfulReplies.current() == val) return;
	_showColorfulReplies = val;
	AyuSettings::save();
}

void MessageShotSettings::setRevealSpoilers(bool val) {
	if (_revealSpoilers.current() == val) return;
	_revealSpoilers = val;
	AyuSettings::save();
}

bool MessageShotSettings::isCloudThemeEmpty() const {
	return !_cloudThemeId.current()
		&& !_cloudThemeAccessHash.current()
		&& !_cloudThemeDocumentId.current()
		&& _cloudThemeTitle.current().isEmpty();
}

void MessageShotSettings::clearCloudThemeData() {
	_cloudThemeId = uint64(0);
	_cloudThemeAccessHash = uint64(0);
	_cloudThemeDocumentId = uint64(0);
	_cloudThemeTitle = QString();
	_cloudThemeAccountId = uint64(0);
}

void MessageShotSettings::setEmbeddedTheme(int type, uint32 accentColor) {
	if (_embeddedThemeType.current() == type
		&& _embeddedThemeAccentColor.current() == accentColor
		&& isCloudThemeEmpty()) {
		return;
	}
	_embeddedThemeType = type;
	_embeddedThemeAccentColor = accentColor;
	clearCloudThemeData();
	AyuSettings::save();
}

void MessageShotSettings::setCloudTheme(uint64 accountId, uint64 id, uint64 accessHash, uint64 documentId, const QString &title) {
	if (_embeddedThemeType.current() == -1
		&& _embeddedThemeAccentColor.current() == 0
		&& _cloudThemeAccountId.current() == accountId
		&& _cloudThemeId.current() == id
		&& _cloudThemeAccessHash.current() == accessHash
		&& _cloudThemeDocumentId.current() == documentId
		&& _cloudThemeTitle.current() == title) {
		return;
	}
	_embeddedThemeType = -1;
	_embeddedThemeAccentColor = uint32(0);
	_cloudThemeAccountId = accountId;
	_cloudThemeId = id;
	_cloudThemeAccessHash = accessHash;
	_cloudThemeDocumentId = documentId;
	_cloudThemeTitle = title;
	AyuSettings::save();
}

void MessageShotSettings::clearTheme() {
	if (_embeddedThemeType.current() == -1
		&& _embeddedThemeAccentColor.current() == 0
		&& isCloudThemeEmpty()) {
		return;
	}
	_embeddedThemeType = -1;
	_embeddedThemeAccentColor = uint32(0);
	clearCloudThemeData();
	AyuSettings::save();
}

void to_json(nlohmann::json &j, const MessageShotSettings &s) {
	j = nlohmann::json{
		{"showBackground", s._showBackground.current()},
		{"showDate", s._showDate.current()},
		{"showReactions", s._showReactions.current()},
		{"showHeaderDecorations", s._showHeaderDecorations.current()},
		{"showColorfulReplies", s._showColorfulReplies.current()},
		{"revealSpoilers", s._revealSpoilers.current()},
		{"embeddedThemeType", s._embeddedThemeType.current()},
		{"embeddedThemeAccentColor", s._embeddedThemeAccentColor.current()},
		{"cloudThemeId", s._cloudThemeId.current()},
		{"cloudThemeAccessHash", s._cloudThemeAccessHash.current()},
		{"cloudThemeDocumentId", s._cloudThemeDocumentId.current()},
		{"cloudThemeTitle", s._cloudThemeTitle.current()},
		{"cloudThemeAccountId", s._cloudThemeAccountId.current()},
	};
}

void from_json(const nlohmann::json &j, MessageShotSettings &s) {
	s._showBackground = j.value("showBackground", true);
	s._showDate = j.value("showDate", false);
	s._showReactions = j.value("showReactions", false);
	s._showHeaderDecorations = j.value("showHeaderDecorations", true);
	s._showColorfulReplies = j.value("showColorfulReplies", true);
	s._revealSpoilers = j.value("revealSpoilers", true);
	s._embeddedThemeType = j.value("embeddedThemeType", j.value("themeType", -1));
	s._embeddedThemeAccentColor = j.value("embeddedThemeAccentColor", j.value("themeAccentColor", uint32(0)));
	s._cloudThemeId = j.value("cloudThemeId", uint64(0));
	s._cloudThemeAccessHash = j.value("cloudThemeAccessHash", uint64(0));
	s._cloudThemeDocumentId = j.value("cloudThemeDocumentId", uint64(0));
	s._cloudThemeTitle = j.value("cloudThemeTitle", QString());
	s._cloudThemeAccountId = j.value("cloudThemeAccountId", uint64(0));
}

AyuSettings::AyuSettings()
: _appIcon(AyuAssets::WHITEGRAM_ICON)
, _editedMark(Core::IsAppLaunched() ? tr::lng_edited(tr::now) : QString("edited")) {
}

AyuSettings::~AyuSettings() {
	stopStartupSound();
}

AyuSettings &AyuSettings::getInstance() {
	static AyuSettings instance;
	return instance;
}

void AyuSettings::load() {
	std::ifstream file(getSettingsPath());
	if (!file.good()) {
		return;
	}

	auto &settings = getInstance();

	try {
		json p;
		file >> p;
		file.close();

		if (!p.contains("ghostModeSettings")) {
			p["ghostModeSettings"] = nlohmann::json::object({
				{"0", {
					{"sendReadMessages", p.value("sendReadMessages", true)},
					{"sendReadStories", p.value("sendReadStories", true)},
					{"sendOnlinePackets", p.value("sendOnlinePackets", true)},
					{"sendUploadProgress", p.value("sendUploadProgress", true)},
					{"sendOfflinePacketAfterOnline", p.value("sendOfflinePacketAfterOnline", false)},
					{"markReadAfterAction", p.value("markReadAfterAction", true)},
					{"useScheduledMessages", p.value("useScheduledMessages", false)},
					{"sendWithoutSound", p.value("sendWithoutSound", false)}
				}}
			});
			p["useGlobalGhostMode"] = true;

			LOG(("AyuGramSettings: migrated ghost mode settings to per-account format"));
		}

		try {
			from_json(p, settings);
		} catch (...) {
			LOG(("AyuGramSettings: failed to parse settings file"));
		}
	} catch (...) {
		LOG(("AyuGramSettings: failed to read settings file (not json-like)"));
	}

	if (cGhost()) {
		auto &ghost = AyuSettings::ghost();
		ghost._sendReadMessages = false;
		ghost._sendReadStories = false;
		ghost._sendOnlinePackets = false;
		ghost._sendUploadProgress = false;
		ghost._sendOfflinePacketAfterOnline = true;
	}

	settings.validate();
	settings.syncSelfDestructPasswordLockout();
}

void AyuSettings::save() {
	auto &settings = getInstance();
	json p = settings;

	std::ofstream file;
	file.open(getSettingsPath());
	file << p.dump(4);
	file.flush();
	file.close();
}

void AyuSettings::reset() {
	getInstance() = AyuSettings();
	save();
}

void AyuSettings::initTimeSpentTracker() {
	auto &settings = getInstance();
	if (!settings._timeSpentTimer) {
		settings._timeSpentTimer = std::make_unique<base::Timer>([] {
			auto &s = getInstance();
			s.setTotalTimeSpent(s.getTotalTimeSpent() + 1);
		});
	}
	if (!settings._timeSpentTimer->isActive()) {
		settings._timeSpentTimer->callEach(60 * crl::time(1000));
	}
}

GhostModeAccountSettings &AyuSettings::ghost(not_null<Main::Session*> session) {
	return ghost(session->userId().bare);
}

GhostModeAccountSettings &AyuSettings::ghost(uint64 userId) {
	auto &settings = getInstance();
	auto overriddenId = settings.getOverriddenGhostUserId(userId);

	auto it = settings._ghostAccounts.find(overriddenId);
	if (it == settings._ghostAccounts.end()) {
		auto account = std::make_unique<GhostModeAccountSettings>();
		it = settings._ghostAccounts.emplace(overriddenId, std::move(account)).first;
	}

	return *it->second;
}

GhostModeAccountSettings &AyuSettings::ghost() {
	if (const auto window = Core::App().activeWindow()) {
		if (const auto session = window->maybeSession()) {
			return ghost(session);
		}
	}
	return ghost(0);
}

AccountNotificationSettings &AyuSettings::accountNotifications(not_null<Main::Session*> session) {
	return accountNotifications(session->userId().bare);
}

AccountNotificationSettings &AyuSettings::accountNotifications(uint64 userId) {
	auto &settings = getInstance();
	auto it = settings._notificationAccounts.find(userId);
	if (it == settings._notificationAccounts.end()) {
		auto account = std::make_unique<AccountNotificationSettings>();
		it = settings._notificationAccounts.emplace(userId, std::move(account)).first;
	}
	return *it->second;
}

AccountNotificationSettings &AyuSettings::accountNotifications() {
	if (const auto window = Core::App().activeWindow()) {
		if (const auto session = window->maybeSession()) {
			return accountNotifications(session);
		}
	}
	return accountNotifications(0);
}

void AyuSettings::setUseGlobalGhostMode(bool val) {
	if (_useGlobalGhostMode.current() == val) return;
	_useGlobalGhostMode = val;
	save();
}

void AyuSettings::addShadowBan(int64 id) {
	if (_shadowBanIds.insert(id).second) {
		FiltersCacheController::rebuildCache();
		FiltersCacheController::fireUpdate();
		save();
	}
}

void AyuSettings::removeShadowBan(int64 id) {
	if (_shadowBanIds.erase(id) > 0) {
		FiltersCacheController::rebuildCache();
		FiltersCacheController::fireUpdate();
		save();
	}
}

void AyuSettings::validate() {
	AyuSettings defaults;
	auto modified = false;

	auto validateRange = [&](auto &var, auto min, auto max, const auto &defaultVar) {
		if (var.current() < min || var.current() > max) {
			var = defaultVar.current();
			modified = true;
		}
	};

	auto validateEnum = [&](auto &var, const auto &defaultVar, int max = 2) {
		auto intVal = static_cast<int>(var.current());
		if (intVal < 0 || intVal > max) {
			var = defaultVar.current();
			modified = true;
		}
	};

	validateEnum(_showPeerId, defaults._showPeerId);
	validateEnum(_channelBottomButton, defaults._channelBottomButton);
	validateEnum(_showReactionsPanelInContextMenu, defaults._showReactionsPanelInContextMenu);
	validateEnum(_showViewsPanelInContextMenu, defaults._showViewsPanelInContextMenu);
	validateEnum(_showHideMessageInContextMenu, defaults._showHideMessageInContextMenu);
	validateEnum(_showUserMessagesInContextMenu, defaults._showUserMessagesInContextMenu);
	validateEnum(_showMessageDetailsInContextMenu, defaults._showMessageDetailsInContextMenu);
	validateEnum(_showRepeatMessageInContextMenu, defaults._showRepeatMessageInContextMenu);
	validateEnum(_showAddFilterInContextMenu, defaults._showAddFilterInContextMenu);

	validateEnum(_connectionBypassMode, defaults._connectionBypassMode, 2);
	validateEnum(_translationProvider, defaults._translationProvider, 3);
	if ((_translationProvider.current() == TranslationProvider::Native)
		&& !Platform::IsTranslateProviderAvailable()) {
		_translationProvider = defaults._translationProvider.current();
		modified = true;
	}

	validateRange(_messageBubbleRadius, 0, 16, defaults._messageBubbleRadius);
	validateRange(_wideMultiplier, 0.5, 4.0, defaults._wideMultiplier);
	validateRange(_avatarCorners, 0, AyuUiSettings::kMaxAvatarCorners, defaults._avatarCorners);
	validateRange(_cryptoFiatCurrency, 0, 2, defaults._cryptoFiatCurrency);

	const auto embeddedType = _messageShotSettings._embeddedThemeType.current();
	auto embeddedTypeValid = (embeddedType == -1) || (embeddedType >= 0 && embeddedType <= 3); // from Window::Theme::EmbeddedType::DayBlue to Window::Theme::EmbeddedType::NightGreen
	if (!embeddedTypeValid) {
		_messageShotSettings._embeddedThemeType = defaults._messageShotSettings._embeddedThemeType.current();
		_messageShotSettings._embeddedThemeAccentColor = defaults._messageShotSettings._embeddedThemeAccentColor.current();
		modified = true;
	}

	if (modified) {
		save();
	}
}

void AyuSettings::setSaveDeletedMessages(bool val) {
	if (_saveDeletedMessages.current() == val) return;
	_saveDeletedMessages = val;
	save();
}

void AyuSettings::setSaveMessagesHistory(bool val) {
	if (_saveMessagesHistory.current() == val) return;
	_saveMessagesHistory = val;
	save();
}

void AyuSettings::setSaveForBots(bool val) {
	if (_saveForBots.current() == val) return;
	_saveForBots = val;
	save();
}

void AyuSettings::setFiltersEnabled(bool val) {
	if (_filtersEnabled.current() == val) return;
	_filtersEnabled = val;
	save();
}

void AyuSettings::setFiltersEnabledInChats(bool val) {
	if (_filtersEnabledInChats.current() == val) return;
	_filtersEnabledInChats = val;
	save();
}

void AyuSettings::setHideFromBlocked(bool val) {
	if (_hideFromBlocked.current() == val) return;
	_hideFromBlocked = val;
	save();
}

void AyuSettings::setSemiTransparentDeletedMessages(bool val) {
	if (_semiTransparentDeletedMessages.current() == val) return;
	_semiTransparentDeletedMessages = val;
	save();
}

void AyuSettings::setDisableAds(bool val) {
	if (_disableAds.current() == val) return;
	_disableAds = val;
	save();
}

void AyuSettings::setDisableStories(bool val) {
	if (_disableStories.current() == val) return;
	_disableStories = val;
	save();
}

void AyuSettings::setDisableCustomBackgrounds(bool val) {
	if (_disableCustomBackgrounds.current() == val) return;
	_disableCustomBackgrounds = val;
	save();
}

void AyuSettings::setHidePremiumStatuses(bool val) {
	if (_hidePremiumStatuses.current() == val) return;
	_hidePremiumStatuses = val;
	save();
}

void AyuSettings::setShowOnlyAddedEmojisAndStickers(bool val) {
	if (_showOnlyAddedEmojisAndStickers.current() == val) return;
	_showOnlyAddedEmojisAndStickers = val;
	save();
}

void AyuSettings::setCollapseSimilarChannels(bool val) {
	if (_collapseSimilarChannels.current() == val) return;
	_collapseSimilarChannels = val;
	save();
}

void AyuSettings::setHideSimilarChannels(bool val) {
	if (_hideSimilarChannels.current() == val) return;
	_hideSimilarChannels = val;
	save();
}

void AyuSettings::setMessageBubbleRadius(int val) {
	if (_messageBubbleRadius.current() == val) return;
	_messageBubbleRadius = val;
	save();
}

void AyuSettings::setWideMultiplier(double val) {
	if (_wideMultiplier.current() == val) return;
	_wideMultiplier = val;
	// doesn't work because it should be set before style::StartManager()
	// AyuUiSettings::setWideMultiplier(val);
	// repaintApp();
	save();
}

void AyuSettings::setSpoofWebviewAsAndroid(bool val) {
	if (_spoofWebviewAsAndroid.current() == val) return;
	_spoofWebviewAsAndroid = val;
	save();
}

void AyuSettings::setDisableOpenLinkWarning(bool val) {
	if (_disableOpenLinkWarning.current() == val) return;
	_disableOpenLinkWarning = val;
	save();
}

void AyuSettings::setIncreaseWebviewHeight(bool val) {
	if (_increaseWebviewHeight.current() == val) return;
	_increaseWebviewHeight = val;
	save();
}

void AyuSettings::setIncreaseWebviewWidth(bool val) {
	if (_increaseWebviewWidth.current() == val) return;
	_increaseWebviewWidth = val;
	save();
}

void AyuSettings::setMaterialSwitches(bool val) {
	if (_materialSwitches.current() == val) return;
	_materialSwitches = val;
	AyuUiSettings::setMaterialSwitches(val);
	repaintApp();
	save();
}

void AyuSettings::setRemoveMessageTail(bool val) {
	if (_removeMessageTail.current() == val) return;
	_removeMessageTail = val;
	save();
}

void AyuSettings::setDisableNotificationsDelay(bool val) {
	if (_disableNotificationsDelay.current() == val) return;
	_disableNotificationsDelay = val;
	save();
}

void AyuSettings::setLocalPremium(bool val) {
	if (_localPremium.current() == val) return;
	_localPremium = val;
	save();
}

void AyuSettings::setShowChannelReactions(bool val) {
	if (_showChannelReactions.current() == val) return;
	_showChannelReactions = val;
	save();
}

void AyuSettings::setShowGroupReactions(bool val) {
	if (_showGroupReactions.current() == val) return;
	_showGroupReactions = val;
	save();
}

void AyuSettings::setShowPrivateChatReactions(bool val) {
	if (_showPrivateChatReactions.current() == val) return;
	_showPrivateChatReactions = val;
	save();
}

void AyuSettings::setAppIcon(const QString &val) {
	if (_appIcon.current() == val) return;
	_appIcon = val;
	save();
}

void AyuSettings::setSimpleQuotesAndReplies(bool val) {
	if (_simpleQuotesAndReplies.current() == val) return;
	_simpleQuotesAndReplies = val;
	save();
}

void AyuSettings::setHideFastShare(bool val) {
	if (_hideFastShare.current() == val) return;
	_hideFastShare = val;
	save();
}

void AyuSettings::setReplaceBottomInfoWithIcons(bool val) {
	if (_replaceBottomInfoWithIcons.current() == val) return;
	_replaceBottomInfoWithIcons = val;
	save();
}

void AyuSettings::setDeletedMark(const QString &val) {
	if (_deletedMark.current() == val) return;
	_deletedMark = val;
	save();
}

void AyuSettings::setEditedMark(const QString &val) {
	if (_editedMark.current() == val) return;
	_editedMark = val;
	save();
}

void AyuSettings::setUnlimitedRecentStickers(bool val) {
	if (_unlimitedRecentStickers.current() == val) return;
	_unlimitedRecentStickers = val;
	save();
}

void AyuSettings::setShowReactionsPanelInContextMenu(ContextMenuVisibility val) {
	if (_showReactionsPanelInContextMenu.current() == val) return;
	_showReactionsPanelInContextMenu = val;
	save();
}

void AyuSettings::setShowViewsPanelInContextMenu(ContextMenuVisibility val) {
	if (_showViewsPanelInContextMenu.current() == val) return;
	_showViewsPanelInContextMenu = val;
	save();
}

void AyuSettings::setShowHideMessageInContextMenu(ContextMenuVisibility val) {
	if (_showHideMessageInContextMenu.current() == val) return;
	_showHideMessageInContextMenu = val;
	save();
}

void AyuSettings::setShowUserMessagesInContextMenu(ContextMenuVisibility val) {
	if (_showUserMessagesInContextMenu.current() == val) return;
	_showUserMessagesInContextMenu = val;
	save();
}

void AyuSettings::setShowMessageDetailsInContextMenu(ContextMenuVisibility val) {
	if (_showMessageDetailsInContextMenu.current() == val) return;
	_showMessageDetailsInContextMenu = val;
	save();
}

void AyuSettings::setShowRepeatMessageInContextMenu(ContextMenuVisibility val) {
	if (_showRepeatMessageInContextMenu.current() == val) return;
	_showRepeatMessageInContextMenu = val;
	save();
}

void AyuSettings::setShowAddFilterInContextMenu(ContextMenuVisibility val) {
	if (_showAddFilterInContextMenu.current() == val) return;
	_showAddFilterInContextMenu = val;
	save();
}

void AyuSettings::setShowAttachButtonInMessageField(bool val) {
	if (_showAttachButtonInMessageField.current() == val) return;
	_showAttachButtonInMessageField = val;
	save();
}

void AyuSettings::setShowCommandsButtonInMessageField(bool val) {
	if (_showCommandsButtonInMessageField.current() == val) return;
	_showCommandsButtonInMessageField = val;
	save();
}

void AyuSettings::setShowEmojiButtonInMessageField(bool val) {
	if (_showEmojiButtonInMessageField.current() == val) return;
	_showEmojiButtonInMessageField = val;
	save();
}

void AyuSettings::setShowMicrophoneButtonInMessageField(bool val) {
	if (_showMicrophoneButtonInMessageField.current() == val) return;
	_showMicrophoneButtonInMessageField = val;
	save();
}

void AyuSettings::setShowAutoDeleteButtonInMessageField(bool val) {
	if (_showAutoDeleteButtonInMessageField.current() == val) return;
	_showAutoDeleteButtonInMessageField = val;
	save();
}

void AyuSettings::setShowGiftButtonInMessageField(bool val) {
	if (_showGiftButtonInMessageField.current() == val) return;
	_showGiftButtonInMessageField = val;
	save();
}

void AyuSettings::setShowAiEditorButtonInMessageField(bool val) {
	if (_showAiEditorButtonInMessageField.current() == val) return;
	_showAiEditorButtonInMessageField = val;
	save();
}

void AyuSettings::setShowAttachPopup(bool val) {
	if (_showAttachPopup.current() == val) return;
	_showAttachPopup = val;
	save();
}

void AyuSettings::setShowEmojiPopup(bool val) {
	if (_showEmojiPopup.current() == val) return;
	_showEmojiPopup = val;
	save();
}

void AyuSettings::setShowMyProfileInDrawer(bool val) {
	if (_showMyProfileInDrawer.current() == val) return;
	_showMyProfileInDrawer = val;
	save();
}

void AyuSettings::setShowBotsInDrawer(bool val) {
	if (_showBotsInDrawer.current() == val) return;
	_showBotsInDrawer = val;
	save();
}

void AyuSettings::setShowNewGroupInDrawer(bool val) {
	if (_showNewGroupInDrawer.current() == val) return;
	_showNewGroupInDrawer = val;
	save();
}

void AyuSettings::setShowNewChannelInDrawer(bool val) {
	if (_showNewChannelInDrawer.current() == val) return;
	_showNewChannelInDrawer = val;
	save();
}

void AyuSettings::setShowContactsInDrawer(bool val) {
	if (_showContactsInDrawer.current() == val) return;
	_showContactsInDrawer = val;
	save();
}

void AyuSettings::setShowCallsInDrawer(bool val) {
	if (_showCallsInDrawer.current() == val) return;
	_showCallsInDrawer = val;
	save();
}

void AyuSettings::setShowSavedMessagesInDrawer(bool val) {
	if (_showSavedMessagesInDrawer.current() == val) return;
	_showSavedMessagesInDrawer = val;
	save();
}

void AyuSettings::setShowLReadToggleInDrawer(bool val) {
	if (_showLReadToggleInDrawer.current() == val) return;
	_showLReadToggleInDrawer = val;
	save();
}

void AyuSettings::setShowSReadToggleInDrawer(bool val) {
	if (_showSReadToggleInDrawer.current() == val) return;
	_showSReadToggleInDrawer = val;
	save();
}

void AyuSettings::setShowNightModeToggleInDrawer(bool val) {
	if (_showNightModeToggleInDrawer.current() == val) return;
	_showNightModeToggleInDrawer = val;
	save();
}

void AyuSettings::setShowGhostToggleInDrawer(bool val) {
	if (_showGhostToggleInDrawer.current() == val) return;
	_showGhostToggleInDrawer = val;
	save();
}

void AyuSettings::setShowStreamerToggleInDrawer(bool val) {
	if (_showStreamerToggleInDrawer.current() == val) return;
	_showStreamerToggleInDrawer = val;
	save();
}

void AyuSettings::setShowGhostToggleInTray(bool val) {
	if (_showGhostToggleInTray.current() == val) return;
	_showGhostToggleInTray = val;
	save();
}

void AyuSettings::setShowStreamerToggleInTray(bool val) {
	if (_showStreamerToggleInTray.current() == val) return;
	_showStreamerToggleInTray = val;
	save();
}

void AyuSettings::setMonoFont(const QString &val) {
	if (_monoFont.current() == val) return;
	_monoFont = val;
	// doesn't work because `static const auto family = ...`
	// AyuUiSettings::setMonoFont(val);
	// repaintApp();
	save();
}

void AyuSettings::setHideNotificationCounters(bool val) {
	if (_hideNotificationCounters.current() == val) return;
	_hideNotificationCounters = val;
	save();
}

void AyuSettings::setHideNotificationBadge(bool val) {
	if (_hideNotificationBadge.current() == val) return;
	_hideNotificationBadge = val;
	Core::App().refreshApplicationIcon();
	Core::App().tray().updateIconCounters();
	Core::App().domain().notifyUnreadBadgeChanged();
	save();
}

void AyuSettings::setHideAllChatsFolder(bool val) {
	if (_hideAllChatsFolder.current() == val) return;
	_hideAllChatsFolder = val;
	save();
}

void AyuSettings::setChannelBottomButton(ChannelBottomButton val) {
	if (_channelBottomButton.current() == val) return;
	_channelBottomButton = val;
	save();
}

void AyuSettings::setQuickAdminShortcuts(bool val) {
	if (_quickAdminShortcuts.current() == val) return;
	_quickAdminShortcuts = val;
	save();
}

void AyuSettings::setDisableGreetingSticker(bool val) {
	if (_disableGreetingSticker.current() == val) return;
	_disableGreetingSticker = val;
	save();
}

void AyuSettings::setShowPeerId(PeerIdDisplay val) {
	if (_showPeerId.current() == val) return;
	_showPeerId = val;
	save();
}

void AyuSettings::setShowMessageSeconds(bool val) {
	if (_showMessageSeconds.current() == val) return;
	_showMessageSeconds = val;
	save();
}

void AyuSettings::setShowMessageShot(bool val) {
	if (_showMessageShot.current() == val) return;
	_showMessageShot = val;
	save();
}

void AyuSettings::setFilterZalgo(bool val) {
	if (_filterZalgo.current() == val) return;
	_filterZalgo = val;
	save();
}

void AyuSettings::setStickerConfirmation(bool val) {
	if (_stickerConfirmation.current() == val) return;
	_stickerConfirmation = val;
	save();
}

void AyuSettings::setGifConfirmation(bool val) {
	if (_gifConfirmation.current() == val) return;
	_gifConfirmation = val;
	save();
}

void AyuSettings::setVoiceConfirmation(bool val) {
	if (_voiceConfirmation.current() == val) return;
	_voiceConfirmation = val;
	save();
}

void AyuSettings::setRoundConfirmation(bool val) {
	if (_roundConfirmation.current() == val) return;
	_roundConfirmation = val;
	save();
}

void AyuSettings::setTranslationProvider(TranslationProvider val) {
	if ((val == TranslationProvider::Native)
		&& !Platform::IsTranslateProviderAvailable()) {
		val = TranslationProvider::Telegram;
	}
	if (_translationProvider.current() == val) return;
	_translationProvider = val;
	if (const auto manager = Ayu::Translator::TranslateManager::currentInstance()) {
		manager->resetCache();
	}
	save();
}

void AyuSettings::setAdaptiveCoverColor(bool val) {
	if (_adaptiveCoverColor.current() == val) return;
	_adaptiveCoverColor = val;
	save();
}

void AyuSettings::setImproveLinkPreviews(bool val) {
	if (_improveLinkPreviews.current() == val) return;
	_improveLinkPreviews = val;
	save();
}

void AyuSettings::setCrashReporting(bool val) {
	if (_crashReporting.current() == val) return;
	_crashReporting = val;
	save();
}

void AyuSettings::setAvatarCorners(int val) {
	if (_avatarCorners.current() == val) return;
	_avatarCorners = val;
	AyuUiSettings::setAvatarCorners(val);
	save();
}

void AyuSettings::setSingleCornerRadius(bool val) {
	if (_singleCornerRadius.current() == val) return;
	_singleCornerRadius = val;
	repaintApp();
	save();
}

void AyuSettings::setStreamerMode(bool val) {
	if (_streamerMode.current() == val) return;
	_streamerMode = val;
	AyuFeatures::StreamerMode::sync();
	save();
}

void AyuSettings::setHideTelegramOnViewOnce(bool val) {
	if (_hideTelegramOnViewOnce.current() == val) return;
	_hideTelegramOnViewOnce = val;
	AyuFeatures::StreamerMode::sync();
	save();
}

void AyuSettings::setMusicIslandEnabled(bool val) {
	if (_musicIslandEnabled.current() == val) return;
	_musicIslandEnabled = val;
	save();
}

void AyuSettings::setMusicIslandPosition(int val) {
	if (_musicIslandPosition.current() == val) return;
	_musicIslandPosition = val;
	save();
}

void AyuSettings::setMusicIslandHideTopBar(bool val) {
	if (_musicIslandHideTopBar.current() == val) return;
	_musicIslandHideTopBar = val;
	save();
}

void AyuSettings::setMusicIslandExpandMode(int val) {
	val = std::clamp(val, 0, 2);
	if (_musicIslandExpandMode.current() == val) return;
	_musicIslandExpandMode = val;
	save();
}

void AyuSettings::setMusicIslandCoverColors(bool val) {
	if (_musicIslandCoverColors.current() == val) return;
	_musicIslandCoverColors = val;
	save();
}

void AyuSettings::setMusicIslandBlurCover(bool val) {
	if (_musicIslandBlurCover.current() == val) return;
	_musicIslandBlurCover = val;
	save();
}

void AyuSettings::setMusicIslandEqualizer(bool val) {
	if (_musicIslandEqualizer.current() == val) return;
	_musicIslandEqualizer = val;
	save();
}

void AyuSettings::setMusicIslandClickOpensTrack(bool val) {
	if (_musicIslandClickOpensTrack.current() == val) return;
	_musicIslandClickOpensTrack = val;
	save();
}

void AyuSettings::setConnectionBypassMode(ConnectionBypassMode val) {
	if (_connectionBypassMode.current() == val) {
		return;
	}
	_connectionBypassMode = val;
	save();
	AyuFeatures::ConnectionBypass::reevaluate(true, true);
}

void AyuSettings::setConnectionBypassSecret(const QString &val) {
	if (_connectionBypassSecret.current() == val) {
		return;
	}
	_connectionBypassSecret = val;
	save();
	AyuFeatures::ConnectionBypass::reevaluate(true);
}

namespace {

void ScheduleBypassHostSave() {
	static auto timer = base::Timer([] {
		AyuSettings::getInstance().save();
	});
	timer.callOnce(2000);
}

} // namespace

QString AyuSettings::bypassLastGoodHost() const {
	return _bypassLastGoodHost;
}

void AyuSettings::setBypassLastGoodHost(const QString &host) {
	if (_bypassLastGoodHost == host) {
		return;
	}
	_bypassLastGoodHost = host;
	ScheduleBypassHostSave();
}

crl::time AyuSettings::bypassBadHostUntil(const QString &host) const {
	const auto it = _bypassBadHostsUntil.find(host);
	return (it != end(_bypassBadHostsUntil)) ? it->second : 0;
}

std::map<QString, crl::time> AyuSettings::bypassBadHostsSnapshot() const {
	return _bypassBadHostsUntil;
}

void AyuSettings::markBypassBadHost(
		const QString &host,
		crl::time untilUnixMs) {
	_bypassBadHostsUntil[host] = untilUnixMs;
	ScheduleBypassHostSave();
}

void AyuSettings::clearBypassBadHost(const QString &host) {
	const auto it = _bypassBadHostsUntil.find(host);
	if (it == end(_bypassBadHostsUntil)) {
		return;
	}
	_bypassBadHostsUntil.erase(it);
	ScheduleBypassHostSave();
}

std::vector<QString> AyuSettings::bypassServerMirrors() const {
	return _bypassServerMirrors;
}

void AyuSettings::setBypassServerMirrors(const std::vector<QString> &mirrors) {
	_bypassServerMirrors = mirrors;
}

std::vector<QString> AyuSettings::bypassServerMirrorHosts() const {
	return _bypassServerMirrorHosts;
}

void AyuSettings::setBypassServerMirrorHosts(
		const std::vector<QString> &hosts) {
	_bypassServerMirrorHosts = hosts;
}

void to_json(nlohmann::json &j, const AyuSettings &s) {
	auto ghostAccounts = nlohmann::json::object();
	for (const auto &[key, value] : s._ghostAccounts) {
		ghostAccounts[std::to_string(key)] = *value;
	}

	auto notificationAccounts = nlohmann::json::object();
	for (const auto &[key, value] : s._notificationAccounts) {
		notificationAccounts[std::to_string(key)] = *value;
	}

	j = nlohmann::json{
		{"ghostModeSettings", ghostAccounts},
		{"notificationAccounts", notificationAccounts},
		{"useGlobalGhostMode", s._useGlobalGhostMode.current()},
		{"saveDeletedMessages", s._saveDeletedMessages.current()},
		{"saveMessagesHistory", s._saveMessagesHistory.current()},
		{"saveForBots", s._saveForBots.current()},
		{"shadowBanIds", s._shadowBanIds},
		{"filtersEnabled", s._filtersEnabled.current()},
		{"filtersEnabledInChats", s._filtersEnabledInChats.current()},
		{"hideFromBlocked", s._hideFromBlocked.current()},
		{"semiTransparentDeletedMessages", s._semiTransparentDeletedMessages.current()},
		{"disableAds", s._disableAds.current()},
		{"disableStories", s._disableStories.current()},
		{"disableCustomBackgrounds", s._disableCustomBackgrounds.current()},
		{"hidePremiumStatuses", s._hidePremiumStatuses.current()},
		{"showOnlyAddedEmojisAndStickers", s._showOnlyAddedEmojisAndStickers.current()},
		{"collapseSimilarChannels", s._collapseSimilarChannels.current()},
		{"hideSimilarChannels", s._hideSimilarChannels.current()},
		{"messageBubbleRadius", s._messageBubbleRadius.current()},
		{"disableOpenLinkWarning", s._disableOpenLinkWarning.current()},
		{"wideMultiplier", s._wideMultiplier.current()},
		{"spoofWebviewAsAndroid", s._spoofWebviewAsAndroid.current()},
		{"increaseWebviewHeight", s._increaseWebviewHeight.current()},
		{"increaseWebviewWidth", s._increaseWebviewWidth.current()},
		{"materialSwitches", s._materialSwitches.current()},
		{"removeMessageTail", s._removeMessageTail.current()},
		{"disableNotificationsDelay", s._disableNotificationsDelay.current()},
		{"localPremium", s._localPremium.current()},
		{"showChannelReactions", s._showChannelReactions.current()},
		{"showGroupReactions", s._showGroupReactions.current()},
		{"showPrivateChatReactions", s._showPrivateChatReactions.current()},
		{"appIcon", s._appIcon.current()},
		{"simpleQuotesAndReplies", s._simpleQuotesAndReplies.current()},
		{"hideFastShare", s._hideFastShare.current()},
		{"replaceBottomInfoWithIcons", s._replaceBottomInfoWithIcons.current()},
		{"deletedMark", s._deletedMark.current()},
		{"editedMark", s._editedMark.current()},
		{"unlimitedRecentStickers", s._unlimitedRecentStickers.current()},
		{"showReactionsPanelInContextMenu", s._showReactionsPanelInContextMenu.current()},
		{"showViewsPanelInContextMenu", s._showViewsPanelInContextMenu.current()},
		{"showHideMessageInContextMenu", s._showHideMessageInContextMenu.current()},
		{"showUserMessagesInContextMenu", s._showUserMessagesInContextMenu.current()},
		{"showMessageDetailsInContextMenu", s._showMessageDetailsInContextMenu.current()},
		{"showRepeatMessageInContextMenu", s._showRepeatMessageInContextMenu.current()},
		{"showAddFilterInContextMenu", s._showAddFilterInContextMenu.current()},
		{"showAttachButtonInMessageField", s._showAttachButtonInMessageField.current()},
		{"showCommandsButtonInMessageField", s._showCommandsButtonInMessageField.current()},
		{"showEmojiButtonInMessageField", s._showEmojiButtonInMessageField.current()},
		{"showMicrophoneButtonInMessageField", s._showMicrophoneButtonInMessageField.current()},
		{"showAutoDeleteButtonInMessageField", s._showAutoDeleteButtonInMessageField.current()},
		{"showGiftButtonInMessageField", s._showGiftButtonInMessageField.current()},
		{"showAiEditorButtonInMessageField", s._showAiEditorButtonInMessageField.current()},
		{"showAttachPopup", s._showAttachPopup.current()},
		{"showEmojiPopup", s._showEmojiPopup.current()},
		{"showMyProfileInDrawer", s._showMyProfileInDrawer.current()},
		{"showBotsInDrawer", s._showBotsInDrawer.current()},
		{"showNewGroupInDrawer", s._showNewGroupInDrawer.current()},
		{"showNewChannelInDrawer", s._showNewChannelInDrawer.current()},
		{"showContactsInDrawer", s._showContactsInDrawer.current()},
		{"showCallsInDrawer", s._showCallsInDrawer.current()},
		{"showSavedMessagesInDrawer", s._showSavedMessagesInDrawer.current()},
		{"showLReadToggleInDrawer", s._showLReadToggleInDrawer.current()},
		{"showSReadToggleInDrawer", s._showSReadToggleInDrawer.current()},
		{"showNightModeToggleInDrawer", s._showNightModeToggleInDrawer.current()},
		{"showGhostToggleInDrawer", s._showGhostToggleInDrawer.current()},
		{"showStreamerToggleInDrawer", s._showStreamerToggleInDrawer.current()},
		{"showGhostToggleInTray", s._showGhostToggleInTray.current()},
		{"showStreamerToggleInTray", s._showStreamerToggleInTray.current()},
		{"monoFont", s._monoFont.current()},
		{"hideNotificationCounters", s._hideNotificationCounters.current()},
		{"hideNotificationBadge", s._hideNotificationBadge.current()},
		{"hideAllChatsFolder", s._hideAllChatsFolder.current()},
		{"channelBottomButton", s._channelBottomButton.current()},
		{"quickAdminShortcuts", s._quickAdminShortcuts.current()},
		{"disableGreetingSticker", s._disableGreetingSticker.current()},
		{"showPeerId", s._showPeerId.current()},
		{"showMessageSeconds", s._showMessageSeconds.current()},
		{"showMessageShot", s._showMessageShot.current()},
		{"filterZalgo", s._filterZalgo.current()},
		{"stickerConfirmation", s._stickerConfirmation.current()},
		{"gifConfirmation", s._gifConfirmation.current()},
		{"voiceConfirmation", s._voiceConfirmation.current()},
		{"roundConfirmation", s._roundConfirmation.current()},
		{"translationProvider", s._translationProvider.current()},
		{"adaptiveCoverColor", s._adaptiveCoverColor.current()},
		{"improveLinkPreviews", s._improveLinkPreviews.current()},
		{"crashReporting", s._crashReporting.current()},
		{"avatarCorners", s._avatarCorners.current()},
		{"singleCornerRadius", s._singleCornerRadius.current()},
		{"enableAccountProfiles", s._enableAccountProfiles.current()},
		{"maxAccountProfiles", s._maxAccountProfiles.current()},
		{"activeProfileId", s._activeProfileId.current()},
		{"startupAnimationEnabled", s._startupAnimationEnabled.current()},
		{"startupAnimationShowIcon", s._startupAnimationShowIcon.current()},
		{"startupAnimationType", s._startupAnimationType.current()},
		{"startupAnimationFps", s._startupAnimationFps.current()},
		{"startupSoundEnabled", s._startupSoundEnabled.current()},
		{"startupSoundPath", s._startupSoundPath.current().toStdString()},
		{"startupSoundVolume", s._startupSoundVolume.current()},
		{"accountProfiles", s._accountProfiles},
		{"enableCustomArchives", s._enableCustomArchives.current()},
		{"confirmFolderChanges", s._confirmFolderChanges.current()},
		{"customArchives", s._customArchives},
		{"customProfileBackgrounds", s._customProfileBackgrounds},
		{"streamerMode", s._streamerMode.current()},
		{"hideTelegramOnViewOnce", s._hideTelegramOnViewOnce.current()},
		{"musicIslandEnabled", s._musicIslandEnabled.current()},
		{"musicIslandPosition", s._musicIslandPosition.current()},
		{"musicIslandHideTopBar", s._musicIslandHideTopBar.current()},
		{"musicIslandExpandMode", s._musicIslandExpandMode.current()},
		{"musicIslandCoverColors", s._musicIslandCoverColors.current()},
		{"musicIslandBlurCover", s._musicIslandBlurCover.current()},
		{"musicIslandEqualizer", s._musicIslandEqualizer.current()},
		{"musicIslandClickOpensTrack", s._musicIslandClickOpensTrack.current()},
		{"connectionBypassMode", s._connectionBypassMode.current()},
		{"connectionBypassSecret", s._connectionBypassSecret.current()},
		{"messageShotSettings", s._messageShotSettings},
		{"cryptoFiatCurrency", s._cryptoFiatCurrency.current()},
		{"cryptoFiatCurrencyManual", s._cryptoFiatCurrencyManual.current()},
		{"showCryptoInDrawer", s._showCryptoInDrawer.current()},
		{"showSendBotInDrawer", s._showSendBotInDrawer.current()},
		{"showBTC", s._showBTC.current()},
		{"showETH", s._showETH.current()},
		{"showTON", s._showTON.current()},
		{"showSOL", s._showSOL.current()},
		{"showBNB", s._showBNB.current()},
		{"showFiatUSD", s._showFiatUSD.current()},
		{"showFiatEUR", s._showFiatEUR.current()},
		{"showFiatRUB", s._showFiatRUB.current()},
		{"totalTimeSpent", s._totalTimeSpent.current()},
		{"showTimeSpentInDrawer", s._showTimeSpentInDrawer.current()},
		{"whitegramAutoUpdate", s._whitegramAutoUpdate.current()},
		{"selfDestructSalt", s._selfDestructSalt.toStdString()},
		{"selfDestructHash", s._selfDestructHash.toStdString()},
		{"selfDestructFailedAttempts", s._selfDestructFailedAttempts},
		{"selfDestructLockoutLevel", s._selfDestructLockoutLevel},
		{"selfDestructLockoutUntil", s._selfDestructLockoutUntilUnixMs},
		{"totalDestructSalt", s._totalDestructSalt.toStdString()},
		{"totalDestructHash", s._totalDestructHash.toStdString()},
		{"totalDestructFailedAttempts", s._totalDestructFailedAttempts},
		{"totalDestructLockoutLevel", s._totalDestructLockoutLevel},
		{"totalDestructLockoutUntil", s._totalDestructLockoutUntilUnixMs},
		{"totalDestructCloudPasswords", [&] {
			auto passwords = nlohmann::json::object();
			for (const auto &[userId, encrypted] : s._totalDestructCloudPasswords) {
				passwords[std::to_string(userId)] = encrypted.toStdString();
			}
			return passwords;
		}()},
		{"bypassLastGoodHost", s._bypassLastGoodHost.toStdString()},
		{"bypassBadHostsUntil", [&] {
			auto badHosts = nlohmann::json::object();
			for (const auto &[host, until] : s._bypassBadHostsUntil) {
				badHosts[host.toStdString()] = until;
			}
			return badHosts;
		}()}
	};
}

void from_json(const nlohmann::json &j, AyuSettings &s) {
	AyuSettings defaults;

	if (j.contains("ghostModeSettings") && j["ghostModeSettings"].is_object()) {
		s._ghostAccounts.clear();
		for (auto &[key, value] : j["ghostModeSettings"].items()) {
			auto account = std::make_unique<GhostModeAccountSettings>();
			value.get_to(*account);
			s._ghostAccounts[std::stoull(key)] = std::move(account);
		}
	}

	if (j.contains("notificationAccounts") && j["notificationAccounts"].is_object()) {
		s._notificationAccounts.clear();
		for (auto &[key, value] : j["notificationAccounts"].items()) {
			auto account = std::make_unique<AccountNotificationSettings>();
			value.get_to(*account);
			s._notificationAccounts[std::stoull(key)] = std::move(account);
		}
	}

	s._useGlobalGhostMode = j.value("useGlobalGhostMode", defaults._useGlobalGhostMode.current());
	s._saveDeletedMessages = j.value("saveDeletedMessages", defaults._saveDeletedMessages.current());
	s._saveMessagesHistory = j.value("saveMessagesHistory", defaults._saveMessagesHistory.current());
	s._saveForBots = j.value("saveForBots", defaults._saveForBots.current());
	s._shadowBanIds = j.value("shadowBanIds", defaults._shadowBanIds);
	s._filtersEnabled = j.value("filtersEnabled", defaults._filtersEnabled.current());
	s._filtersEnabledInChats = j.value("filtersEnabledInChats", defaults._filtersEnabledInChats.current());
	s._hideFromBlocked = j.value("hideFromBlocked", defaults._hideFromBlocked.current());
	s._semiTransparentDeletedMessages = j.value("semiTransparentDeletedMessages", defaults._semiTransparentDeletedMessages.current());
	s._disableAds = j.value("disableAds", defaults._disableAds.current());
	s._disableStories = j.value("disableStories", defaults._disableStories.current());
	s._disableCustomBackgrounds = j.value("disableCustomBackgrounds", defaults._disableCustomBackgrounds.current());
	s._hidePremiumStatuses = j.value("hidePremiumStatuses", defaults._hidePremiumStatuses.current());
	s._showOnlyAddedEmojisAndStickers = j.value("showOnlyAddedEmojisAndStickers", defaults._showOnlyAddedEmojisAndStickers.current());
	s._collapseSimilarChannels = j.value("collapseSimilarChannels", defaults._collapseSimilarChannels.current());
	s._hideSimilarChannels = j.value("hideSimilarChannels", defaults._hideSimilarChannels.current());
	s._messageBubbleRadius = j.value("messageBubbleRadius", defaults._messageBubbleRadius.current());
	s._disableOpenLinkWarning = j.value("disableOpenLinkWarning", defaults._disableOpenLinkWarning.current());
	s._wideMultiplier = j.value("wideMultiplier", defaults._wideMultiplier.current());
	s._spoofWebviewAsAndroid = j.value("spoofWebviewAsAndroid", defaults._spoofWebviewAsAndroid.current());
	s._increaseWebviewHeight = j.value("increaseWebviewHeight", defaults._increaseWebviewHeight.current());
	s._increaseWebviewWidth = j.value("increaseWebviewWidth", defaults._increaseWebviewWidth.current());
	s._materialSwitches = j.value("materialSwitches", defaults._materialSwitches.current());
	s._removeMessageTail = j.value("removeMessageTail", defaults._removeMessageTail.current());
	s._disableNotificationsDelay = j.value("disableNotificationsDelay", defaults._disableNotificationsDelay.current());
	s._localPremium = j.value("localPremium", defaults._localPremium.current());
	s._showChannelReactions = j.value("showChannelReactions", defaults._showChannelReactions.current());
	s._showGroupReactions = j.value("showGroupReactions", defaults._showGroupReactions.current());
	s._showPrivateChatReactions = j.value("showPrivateChatReactions", defaults._showPrivateChatReactions.current());
	s._appIcon = j.value("appIcon", defaults._appIcon.current());
	s._simpleQuotesAndReplies = j.value("simpleQuotesAndReplies", defaults._simpleQuotesAndReplies.current());
	s._hideFastShare = j.value("hideFastShare", defaults._hideFastShare.current());
	s._replaceBottomInfoWithIcons = j.value("replaceBottomInfoWithIcons", defaults._replaceBottomInfoWithIcons.current());
	s._deletedMark = j.value("deletedMark", defaults._deletedMark.current());
	s._editedMark = j.value("editedMark", defaults._editedMark.current());
	s._unlimitedRecentStickers = j.value("unlimitedRecentStickers", defaults._unlimitedRecentStickers.current());
	s._showReactionsPanelInContextMenu = j.value("showReactionsPanelInContextMenu", defaults._showReactionsPanelInContextMenu.current());
	s._showViewsPanelInContextMenu = j.value("showViewsPanelInContextMenu", defaults._showViewsPanelInContextMenu.current());
	s._showHideMessageInContextMenu = j.value("showHideMessageInContextMenu", defaults._showHideMessageInContextMenu.current());
	s._showUserMessagesInContextMenu = j.value("showUserMessagesInContextMenu", defaults._showUserMessagesInContextMenu.current());
	s._showMessageDetailsInContextMenu = j.value("showMessageDetailsInContextMenu", defaults._showMessageDetailsInContextMenu.current());
	s._showRepeatMessageInContextMenu = j.value("showRepeatMessageInContextMenu", defaults._showRepeatMessageInContextMenu.current());
	s._showAddFilterInContextMenu = j.value("showAddFilterInContextMenu", defaults._showAddFilterInContextMenu.current());
	s._showAttachButtonInMessageField = j.value("showAttachButtonInMessageField", defaults._showAttachButtonInMessageField.current());
	s._showCommandsButtonInMessageField = j.value("showCommandsButtonInMessageField", defaults._showCommandsButtonInMessageField.current());
	s._showEmojiButtonInMessageField = j.value("showEmojiButtonInMessageField", defaults._showEmojiButtonInMessageField.current());
	s._showMicrophoneButtonInMessageField = j.value("showMicrophoneButtonInMessageField", defaults._showMicrophoneButtonInMessageField.current());
	s._showAutoDeleteButtonInMessageField = j.value("showAutoDeleteButtonInMessageField", defaults._showAutoDeleteButtonInMessageField.current());
	s._showGiftButtonInMessageField = j.value("showGiftButtonInMessageField", defaults._showGiftButtonInMessageField.current());
	s._showAiEditorButtonInMessageField = j.value("showAiEditorButtonInMessageField", defaults._showAiEditorButtonInMessageField.current());
	s._showAttachPopup = j.value("showAttachPopup", defaults._showAttachPopup.current());
	s._showEmojiPopup = j.value("showEmojiPopup", defaults._showEmojiPopup.current());
	s._showMyProfileInDrawer = j.value("showMyProfileInDrawer", defaults._showMyProfileInDrawer.current());
	s._showBotsInDrawer = j.value("showBotsInDrawer", defaults._showBotsInDrawer.current());
	s._showNewGroupInDrawer = j.value("showNewGroupInDrawer", defaults._showNewGroupInDrawer.current());
	s._showNewChannelInDrawer = j.value("showNewChannelInDrawer", defaults._showNewChannelInDrawer.current());
	s._showContactsInDrawer = j.value("showContactsInDrawer", defaults._showContactsInDrawer.current());
	s._showCallsInDrawer = j.value("showCallsInDrawer", defaults._showCallsInDrawer.current());
	s._showSavedMessagesInDrawer = j.value("showSavedMessagesInDrawer", defaults._showSavedMessagesInDrawer.current());
	s._showLReadToggleInDrawer = j.value("showLReadToggleInDrawer", defaults._showLReadToggleInDrawer.current());
	s._showSReadToggleInDrawer = j.value("showSReadToggleInDrawer", defaults._showSReadToggleInDrawer.current());
	s._showNightModeToggleInDrawer = j.value("showNightModeToggleInDrawer", defaults._showNightModeToggleInDrawer.current());
	s._showGhostToggleInDrawer = j.value("showGhostToggleInDrawer", defaults._showGhostToggleInDrawer.current());
	s._showStreamerToggleInDrawer = j.value("showStreamerToggleInDrawer", defaults._showStreamerToggleInDrawer.current());
	s._showGhostToggleInTray = j.value("showGhostToggleInTray", defaults._showGhostToggleInTray.current());
	s._showStreamerToggleInTray = j.value("showStreamerToggleInTray", defaults._showStreamerToggleInTray.current());
	s._monoFont = j.value("monoFont", defaults._monoFont.current());
	s._hideNotificationCounters = j.value("hideNotificationCounters", defaults._hideNotificationCounters.current());
	s._hideNotificationBadge = j.value("hideNotificationBadge", defaults._hideNotificationBadge.current());
	s._hideAllChatsFolder = j.value("hideAllChatsFolder", defaults._hideAllChatsFolder.current());
	s._channelBottomButton = j.value("channelBottomButton", defaults._channelBottomButton.current());
	s._quickAdminShortcuts = j.value("quickAdminShortcuts", defaults._quickAdminShortcuts.current());
	s._disableGreetingSticker = j.value("disableGreetingSticker", defaults._disableGreetingSticker.current());
	s._showPeerId = j.value("showPeerId", defaults._showPeerId.current());
	s._showMessageSeconds = j.value("showMessageSeconds", defaults._showMessageSeconds.current());
	s._showMessageShot = j.value("showMessageShot", defaults._showMessageShot.current());
	s._filterZalgo = j.value("filterZalgo", defaults._filterZalgo.current());
	s._stickerConfirmation = j.value("stickerConfirmation", defaults._stickerConfirmation.current());
	s._gifConfirmation = j.value("gifConfirmation", defaults._gifConfirmation.current());
	s._voiceConfirmation = j.value("voiceConfirmation", defaults._voiceConfirmation.current());
	s._roundConfirmation = j.value("roundConfirmation", defaults._roundConfirmation.current());
	s._translationProvider = j.value("translationProvider", defaults._translationProvider.current());
	s._adaptiveCoverColor = j.value("adaptiveCoverColor", defaults._adaptiveCoverColor.current());
	s._improveLinkPreviews = j.value("improveLinkPreviews", defaults._improveLinkPreviews.current());
	s._crashReporting = j.value("crashReporting", defaults._crashReporting.current());
	s._avatarCorners = j.value("avatarCorners", defaults._avatarCorners.current());
	s._singleCornerRadius = j.value("singleCornerRadius", defaults._singleCornerRadius.current());
	s._streamerMode = j.value("streamerMode", defaults._streamerMode.current());
	s._hideTelegramOnViewOnce = j.value("hideTelegramOnViewOnce", defaults._hideTelegramOnViewOnce.current());
	s._musicIslandEnabled = j.value(
		"musicIslandEnabled",
		defaults._musicIslandEnabled.current());
	s._musicIslandPosition = std::clamp(
		j.value("musicIslandPosition", defaults._musicIslandPosition.current()),
		0,
		3);
	s._musicIslandHideTopBar = j.value(
		"musicIslandHideTopBar",
		defaults._musicIslandHideTopBar.current());
	if (j.contains("musicIslandExpandMode")) {
		s._musicIslandExpandMode = std::clamp(
			j.value(
				"musicIslandExpandMode",
				defaults._musicIslandExpandMode.current()),
			0,
			2);
	} else if (j.contains("musicIslandExpandOnHover")) {
		s._musicIslandExpandMode = j.value("musicIslandExpandOnHover", true)
			? 0
			: 1;
	} else {
		s._musicIslandExpandMode = defaults._musicIslandExpandMode.current();
	}
	s._musicIslandCoverColors = j.value(
		"musicIslandCoverColors",
		defaults._musicIslandCoverColors.current());
	s._musicIslandBlurCover = j.value(
		"musicIslandBlurCover",
		defaults._musicIslandBlurCover.current());
	s._musicIslandEqualizer = j.value(
		"musicIslandEqualizer",
		defaults._musicIslandEqualizer.current());
	s._musicIslandClickOpensTrack = j.value(
		"musicIslandClickOpensTrack",
		defaults._musicIslandClickOpensTrack.current());
	s._connectionBypassMode = j.value(
		"connectionBypassMode",
		defaults._connectionBypassMode.current());
	s._connectionBypassSecret = j.value(
		"connectionBypassSecret",
		defaults._connectionBypassSecret.current());

	s._enableAccountProfiles = j.value("enableAccountProfiles", defaults._enableAccountProfiles.current());
	s._maxAccountProfiles = j.value("maxAccountProfiles", defaults._maxAccountProfiles.current());
	s._activeProfileId = j.value("activeProfileId", 0);
	s._startupAnimationEnabled = j.value("startupAnimationEnabled", defaults._startupAnimationEnabled.current());
	s._startupAnimationShowIcon = j.value(
		"startupAnimationShowIcon",
		defaults._startupAnimationShowIcon.current());
	s._startupAnimationType = j.value("startupAnimationType", defaults._startupAnimationType.current());
	s._startupAnimationFps = j.value("startupAnimationFps", defaults._startupAnimationFps.current());
	s._startupSoundEnabled = j.value(
		"startupSoundEnabled",
		defaults._startupSoundEnabled.current());
	s._startupSoundPath = QString::fromStdString(j.value(
		"startupSoundPath",
		defaults._startupSoundPath.current().toStdString()));
	s._startupSoundVolume = j.value(
		"startupSoundVolume",
		defaults._startupSoundVolume.current());
	if (j.contains("accountProfiles") && j["accountProfiles"].is_array()) {
		j["accountProfiles"].get_to(s._accountProfiles);
	}
	s._enableCustomArchives = j.value(
		"enableCustomArchives",
		defaults._enableCustomArchives.current());
	s._confirmFolderChanges = j.value(
		"confirmFolderChanges",
		defaults._confirmFolderChanges.current());
	if (j.contains("customArchives") && j["customArchives"].is_array()) {
		j["customArchives"].get_to(s._customArchives);
	}
	if (j.contains("customProfileBackgrounds")
		&& j["customProfileBackgrounds"].is_array()) {
		j["customProfileBackgrounds"].get_to(s._customProfileBackgrounds);
	}

	if (j.contains("messageShotSettings") && j["messageShotSettings"].is_object()) {
		j["messageShotSettings"].get_to(s._messageShotSettings);
	}

	s._cryptoFiatCurrency = j.value(
		"cryptoFiatCurrency",
		defaults._cryptoFiatCurrency.current());
	s._cryptoFiatCurrencyManual = j.value(
		"cryptoFiatCurrencyManual",
		defaults._cryptoFiatCurrencyManual.current());

	s._showCryptoInDrawer = j.value(
		"showCryptoInDrawer",
		defaults._showCryptoInDrawer.current());
	s._showSendBotInDrawer = j.value(
		"showSendBotInDrawer",
		j.value("showWalletInDrawer", defaults._showSendBotInDrawer.current()));
	s._showBTC = j.value("showBTC", defaults._showBTC.current());
	s._showETH = j.value("showETH", defaults._showETH.current());
	s._showTON = j.value("showTON", defaults._showTON.current());
	s._showSOL = j.value("showSOL", defaults._showSOL.current());
	s._showBNB = j.value("showBNB", defaults._showBNB.current());
	s._showFiatUSD = j.value("showFiatUSD", defaults._showFiatUSD.current());
	s._showFiatEUR = j.value("showFiatEUR", defaults._showFiatEUR.current());
	s._showFiatRUB = j.value("showFiatRUB", defaults._showFiatRUB.current());
	s._totalTimeSpent = j.value(
		"totalTimeSpent",
		defaults._totalTimeSpent.current());
	s._showTimeSpentInDrawer = j.value(
		"showTimeSpentInDrawer",
		defaults._showTimeSpentInDrawer.current());
	s._whitegramAutoUpdate = j.value(
		"whitegramAutoUpdate",
		defaults._whitegramAutoUpdate.current());
	s._selfDestructSalt = QString::fromStdString(
		j.value("selfDestructSalt", std::string()));
	s._selfDestructHash = QString::fromStdString(
		j.value("selfDestructHash", std::string()));
	s._selfDestructFailedAttempts = j.value(
		"selfDestructFailedAttempts",
		defaults._selfDestructFailedAttempts);
	s._selfDestructLockoutLevel = j.value(
		"selfDestructLockoutLevel",
		defaults._selfDestructLockoutLevel);
	s._selfDestructLockoutUntilUnixMs = j.value<std::int64_t>(
		"selfDestructLockoutUntil",
		defaults._selfDestructLockoutUntilUnixMs);
	s.normalizeSelfDestructPasswordLockout();
	s._hasSelfDestructPassword = !s._selfDestructHash.isEmpty() && !s._selfDestructSalt.isEmpty();

	s._totalDestructSalt = QString::fromStdString(
		j.value("totalDestructSalt", std::string()));
	s._totalDestructHash = QString::fromStdString(
		j.value("totalDestructHash", std::string()));
	s._totalDestructFailedAttempts = j.value(
		"totalDestructFailedAttempts",
		defaults._totalDestructFailedAttempts);
	s._totalDestructLockoutLevel = j.value(
		"totalDestructLockoutLevel",
		defaults._totalDestructLockoutLevel);
	s._totalDestructLockoutUntilUnixMs = j.value<std::int64_t>(
		"totalDestructLockoutUntil",
		defaults._totalDestructLockoutUntilUnixMs);
	s.normalizeTotalDestructPasswordLockout();
	s._hasTotalDestructPassword = !s._totalDestructHash.isEmpty() && !s._totalDestructSalt.isEmpty();

	s._totalDestructCloudPasswords.clear();
	if (j.contains("totalDestructCloudPasswords")
		&& j["totalDestructCloudPasswords"].is_object()) {
		for (auto &[key, value] : j["totalDestructCloudPasswords"].items()) {
			const auto userId = QString::fromStdString(key).toULongLong();
			if (userId > 0) {
				s._totalDestructCloudPasswords[userId]
					= QString::fromStdString(value.get<std::string>());
			}
		}
	}

	s._bypassLastGoodHost = QString::fromStdString(
		j.value("bypassLastGoodHost", std::string()));
	if (s._bypassLastGoodHost.endsWith(
			u".web.telegram.org"_q,
			Qt::CaseInsensitive)) {
		s._bypassLastGoodHost.clear();
	}
	s._bypassBadHostsUntil.clear();
	if (j.contains("bypassBadHostsUntil") && j["bypassBadHostsUntil"].is_object()) {
		for (auto &[key, value] : j["bypassBadHostsUntil"].items()) {
			s._bypassBadHostsUntil.emplace(
				QString::fromStdString(key),
				value.get<crl::time>());
		}
	}
	const auto now = QDateTime::currentMSecsSinceEpoch();
	for (auto it = s._bypassBadHostsUntil.begin()
		; it != s._bypassBadHostsUntil.end();) {
		if (it->second <= now) {
			it = s._bypassBadHostsUntil.erase(it);
		} else {
			++it;
		}
	}
}

void AyuSettings::setShowCryptoInDrawer(bool val) {
	if (_showCryptoInDrawer.current() == val) return;
	_showCryptoInDrawer = val;
	save();
}

void AyuSettings::setShowSendBotInDrawer(bool val) {
	if (_showSendBotInDrawer.current() == val) return;
	_showSendBotInDrawer = val;
	save();
}

void AyuSettings::setCryptoFiatCurrency(int val) {
	const auto changed = (_cryptoFiatCurrency.current() != val)
		|| !_cryptoFiatCurrencyManual.current();
	if (!changed) {
		return;
	}
	_cryptoFiatCurrencyManual = true;
	_cryptoFiatCurrency = val;
	save();
}

void AyuSettings::setCryptoFiatCurrencyFromLanguage(int val, bool force) {
	if (!force && _cryptoFiatCurrencyManual.current()) {
		return;
	}
	if (force) {
		_cryptoFiatCurrencyManual = false;
	}
	if (_cryptoFiatCurrency.current() == val) {
		return;
	}
	_cryptoFiatCurrency = val;
	save();
}

void AyuSettings::setShowBTC(bool val) {
	if (_showBTC.current() == val) return;
	_showBTC = val;
	save();
}

void AyuSettings::setShowETH(bool val) {
	if (_showETH.current() == val) return;
	_showETH = val;
	save();
}

void AyuSettings::setShowTON(bool val) {
	if (_showTON.current() == val) return;
	_showTON = val;
	save();
}

void AyuSettings::setShowSOL(bool val) {
	if (_showSOL.current() == val) return;
	_showSOL = val;
	save();
}

void AyuSettings::setShowBNB(bool val) {
	if (_showBNB.current() == val) return;
	_showBNB = val;
	save();
}

void AyuSettings::setShowFiatUSD(bool val) {
	if (_showFiatUSD.current() == val) return;
	_showFiatUSD = val;
	save();
}

void AyuSettings::setShowFiatEUR(bool val) {
	if (_showFiatEUR.current() == val) return;
	_showFiatEUR = val;
	save();
}

void AyuSettings::setShowFiatRUB(bool val) {
	if (_showFiatRUB.current() == val) return;
	_showFiatRUB = val;
	save();
}

void AyuSettings::setStartupAnimationEnabled(bool val) {
	if (_startupAnimationEnabled.current() == val) return;
	_startupAnimationEnabled = val;
	save();
}

void AyuSettings::setStartupAnimationShowIcon(bool val) {
	if (_startupAnimationShowIcon.current() == val) return;
	_startupAnimationShowIcon = val;
	save();
}

void AyuSettings::setStartupAnimationType(int val) {
	if (_startupAnimationType.current() == val) return;
	_startupAnimationType = val;
	save();
}

void AyuSettings::setStartupAnimationFps(int val) {
	if (_startupAnimationFps.current() == val) return;
	_startupAnimationFps = val;
	save();
}

void AyuSettings::setTotalTimeSpent(int64 val) {
	if (_totalTimeSpent.current() == val) return;
	_totalTimeSpent = val;
	save();
}

void AyuSettings::setShowTimeSpentInDrawer(bool val) {
	if (_showTimeSpentInDrawer.current() == val) return;
	_showTimeSpentInDrawer = val;
	save();
}

void AyuSettings::setWhitegramAutoUpdate(bool val) {
	if (_whitegramAutoUpdate.current() == val) return;
	_whitegramAutoUpdate = val;
	save();
}

namespace {

constexpr auto kSelfDestructMaxAttempts = 3;

constexpr auto kLegacyLockoutThreshold = crl::time(1'000'000'000'000LL);

constexpr std::array<int, 6> kSelfDestructLockoutSeconds = {
	5 * 60,
	10 * 60,
	30 * 60,
	60 * 60,
	2 * 60 * 60,
	4 * 60 * 60,
};

[[nodiscard]] crl::time selfDestructWallClockMsNow() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] QString MakeSelfDestructSalt() {
	return QString::number(base::RandomValue<uint64>(), 16);
}

[[nodiscard]] QString HashSelfDestructPassword(
		const QString &salt,
		const QString &password) {
	const auto bytes = QCryptographicHash::hash(
		(salt + password).toUtf8(),
		QCryptographicHash::Sha256);
	return QString::fromLatin1(bytes.toHex());
}

} // namespace

void AyuSettings::normalizeSelfDestructPasswordLockout() {
	constexpr auto kLegacyLockoutThreshold = crl::time(1'000'000'000'000LL);
	if (_selfDestructLockoutUntilUnixMs > 0
		&& _selfDestructLockoutUntilUnixMs < kLegacyLockoutThreshold) {
		_selfDestructLockoutUntilUnixMs = 0;
	}
	const auto now = QDateTime::currentMSecsSinceEpoch();
	if (_selfDestructLockoutUntilUnixMs > 0
		&& _selfDestructLockoutUntilUnixMs <= now) {
		_selfDestructLockoutUntilUnixMs = 0;
		_selfDestructFailedAttempts = 0;
	}
}

bool AyuSettings::checkSelfDestructPassword(const QString &password) const {
	if (!hasSelfDestructPassword()) {
		return false;
	}
	return HashSelfDestructPassword(_selfDestructSalt, password)
		== _selfDestructHash;
}

void AyuSettings::setSelfDestructPassword(const QString &password) {
	_selfDestructSalt = MakeSelfDestructSalt();
	_selfDestructHash = HashSelfDestructPassword(
		_selfDestructSalt,
		password);
	_hasSelfDestructPassword = true;
	resetSelfDestructPasswordLockout();
	save();
}

void AyuSettings::clearSelfDestructPassword() {
	_selfDestructSalt = QString();
	_selfDestructHash = QString();
	_hasSelfDestructPassword = false;
	resetSelfDestructPasswordLockout();
	save();
}

void AyuSettings::normalizeTotalDestructPasswordLockout() {
	constexpr auto kLegacyLockoutThreshold = crl::time(1'000'000'000'000LL);
	if (_totalDestructLockoutUntilUnixMs > 0
		&& _totalDestructLockoutUntilUnixMs < kLegacyLockoutThreshold) {
		_totalDestructLockoutUntilUnixMs = 0;
	}
	const auto now = QDateTime::currentMSecsSinceEpoch();
	if (_totalDestructLockoutUntilUnixMs > 0
		&& _totalDestructLockoutUntilUnixMs <= now) {
		_totalDestructLockoutUntilUnixMs = 0;
		_totalDestructFailedAttempts = 0;
	}
}

bool AyuSettings::checkTotalDestructPassword(const QString &password) const {
	if (!hasTotalDestructPassword()) {
		return false;
	}
	return HashSelfDestructPassword(_totalDestructSalt, password)
		== _totalDestructHash;
}

void AyuSettings::setTotalDestructPassword(const QString &password) {
	_totalDestructSalt = MakeSelfDestructSalt();
	_totalDestructHash = HashSelfDestructPassword(
		_totalDestructSalt,
		password);
	_hasTotalDestructPassword = true;
	resetTotalDestructPasswordLockout();
	save();
}

void AyuSettings::clearTotalDestructPassword() {
	_totalDestructSalt = QString();
	_totalDestructHash = QString();
	_hasTotalDestructPassword = false;
	_totalDestructCloudPasswords.clear();
	resetTotalDestructPasswordLockout();
	save();
}

void AyuSettings::setTotalDestructCloudPassword(
		uint64 userId,
		const QString &encryptedPassword) {
	_totalDestructCloudPasswords[userId] = encryptedPassword;
	save();
}

void AyuSettings::clearTotalDestructCloudPasswords() {
	_totalDestructCloudPasswords.clear();
	save();
}

std::map<uint64, QString> AyuSettings::totalDestructCloudPasswords() const {
	return _totalDestructCloudPasswords;
}

bool AyuSettings::isTotalDestructPasswordLockedOut() const {
	return _totalDestructLockoutUntilUnixMs > selfDestructWallClockMsNow();
}

crl::time AyuSettings::totalDestructPasswordLockoutRemaining() const {
	const auto remaining = _totalDestructLockoutUntilUnixMs
		- selfDestructWallClockMsNow();
	return remaining > 0 ? remaining : 0;
}

int AyuSettings::totalDestructPasswordAttemptsRemaining() const {
	return std::max(
		0,
		kSelfDestructMaxAttempts - _totalDestructFailedAttempts);
}

void AyuSettings::registerTotalDestructPasswordFailure() {
	if (isTotalDestructPasswordLockedOut()) {
		return;
	}
	++_totalDestructFailedAttempts;
	if (_totalDestructFailedAttempts < kSelfDestructMaxAttempts) {
		save();
		return;
	}
	_totalDestructFailedAttempts = 0;
	const auto level = std::min(
		_totalDestructLockoutLevel,
		int(kSelfDestructLockoutSeconds.size()) - 1);
	_totalDestructLockoutUntilUnixMs = selfDestructWallClockMsNow()
		+ crl::time(kSelfDestructLockoutSeconds[level]) * 1000;
	if (_totalDestructLockoutLevel + 1 < int(kSelfDestructLockoutSeconds.size())) {
		++_totalDestructLockoutLevel;
	}
	save();
}

void AyuSettings::resetTotalDestructPasswordLockout() {
	_totalDestructFailedAttempts = 0;
	_totalDestructLockoutLevel = 0;
	_totalDestructLockoutUntilUnixMs = 0;
}

void AyuSettings::syncTotalDestructPasswordLockout() {
	const auto before = _totalDestructLockoutUntilUnixMs;
	normalizeTotalDestructPasswordLockout();
	if (_totalDestructLockoutUntilUnixMs != before) {
		save();
	}
}

bool AyuSettings::isSelfDestructPasswordLockedOut() const {
	return _selfDestructLockoutUntilUnixMs > selfDestructWallClockMsNow();
}

crl::time AyuSettings::selfDestructPasswordLockoutRemaining() const {
	const auto remaining = _selfDestructLockoutUntilUnixMs
		- selfDestructWallClockMsNow();
	return remaining > 0 ? remaining : 0;
}

int AyuSettings::selfDestructPasswordAttemptsRemaining() const {
	return std::max(
		0,
		kSelfDestructMaxAttempts - _selfDestructFailedAttempts);
}

void AyuSettings::registerSelfDestructPasswordFailure() {
	if (isSelfDestructPasswordLockedOut()) {
		return;
	}
	++_selfDestructFailedAttempts;
	if (_selfDestructFailedAttempts < kSelfDestructMaxAttempts) {
		save();
		return;
	}
	_selfDestructFailedAttempts = 0;
	const auto level = std::min(
		_selfDestructLockoutLevel,
		int(kSelfDestructLockoutSeconds.size()) - 1);
	_selfDestructLockoutUntilUnixMs = selfDestructWallClockMsNow()
		+ crl::time(kSelfDestructLockoutSeconds[level]) * 1000;
	if (_selfDestructLockoutLevel + 1 < int(kSelfDestructLockoutSeconds.size())) {
		++_selfDestructLockoutLevel;
	}
	save();
}

void AyuSettings::resetSelfDestructPasswordLockout() {
	_selfDestructFailedAttempts = 0;
	_selfDestructLockoutLevel = 0;
	_selfDestructLockoutUntilUnixMs = 0;
}

void AyuSettings::syncSelfDestructPasswordLockout() {
	const auto before = _selfDestructLockoutUntilUnixMs;
	normalizeSelfDestructPasswordLockout();
	if (_selfDestructLockoutUntilUnixMs != before) {
		save();
	}
}

void AyuSettings::setStartupSoundEnabled(bool val) {
	if (_startupSoundEnabled.current() == val) return;
	_startupSoundEnabled = val;
	save();
}

void AyuSettings::setStartupSoundPath(const QString &val) {
	if (_startupSoundPath.current() == val) return;
	_startupSoundPath = val;
	save();
}

void AyuSettings::setStartupSoundVolume(int val) {
	const auto clamped = std::clamp(val, 0, 100);
	if (_startupSoundVolume.current() == clamped) {
		return;
	}
	_startupSoundVolume = clamped;
	save();
	if (_startupSoundTrack && _startupSoundTrack->isActive()) {
		previewStartupSound();
	}
}

QString AyuSettings::startupSoundBuiltinPath() {
	return u":/sounds/call_connect.mp3"_q;
}

QString AyuSettings::startupSoundEffectivePath() const {
	const auto &stored = _startupSoundPath.current();
	return stored.isEmpty() ? startupSoundBuiltinPath() : stored;
}

bool AyuSettings::isStartupSoundPlaying() const {
	return _startupSoundTrack && _startupSoundTrack->isActive();
}

void AyuSettings::stopStartupSound() {
	if (!_startupSoundTrack) {
		return;
	}
	if (Core::IsAppLaunched()) {
		_startupSoundTrack->detachFromDevice();
		_startupSoundTrack.reset();
	} else {
		_startupSoundTrack.release();
	}
}

void AyuSettings::playStartupSound() {
	if (!_startupSoundEnabled.current()) {
		return;
	}
	previewStartupSound();
}

void AyuSettings::previewStartupSound() {
	if (!Core::IsAppLaunched()) {
		return;
	}
	stopStartupSound();
	const auto volume = _startupSoundVolume.current();
	if (volume <= 0) {
		return;
	}
	const auto path = startupSoundEffectivePath();
	_startupSoundTrack = Media::Audio::Current().createTrack();
	if (!_startupSoundTrack) {
		return;
	}
	_startupSoundTrack->fillFromFile(path);
	if (_startupSoundTrack->failed()) {
		stopStartupSound();
		return;
	}
	_startupSoundTrack->playOnce(volume / 100.);
}

void AyuSettings::setActiveProfileId(int val) {
	if (_activeProfileId.current() == val) return;
	_activeProfileId = val;
	save();
}

void AyuSettings::setEnableAccountProfiles(bool val) {
	if (_enableAccountProfiles.current() == val) return;
	_enableAccountProfiles = val;
	save();
}

void AyuSettings::setEnableCustomArchives(bool val) {
	if (_enableCustomArchives.current() == val) {
		return;
	}
	_enableCustomArchives = val;
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::setConfirmFolderChanges(bool val) {
	if (_confirmFolderChanges.current() == val) {
		return;
	}
	_confirmFolderChanges = val;
	save();
}

void AyuSettings::setCustomArchives(const std::vector<CustomArchive> &val) {
	_customArchives = val;
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::addCustomArchive(const CustomArchive &val) {
	_customArchives.push_back(val);
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::addCustomArchive(const QString &name) {
	auto maxId = 0;
	auto maxOrder = 0;
	for (const auto &archive : _customArchives) {
		if (archive.id > maxId) {
			maxId = archive.id;
		}
		if (archive.order > maxOrder) {
			maxOrder = archive.order;
		}
	}
	auto archive = CustomArchive();
	archive.id = maxId + 1;
	archive.name = name;
	archive.order = maxOrder + 1;
	_customArchives.push_back(archive);
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::removeCustomArchive(int id) {
	std::erase_if(_customArchives, [id](const CustomArchive &archive) {
		return archive.id == id;
	});
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::updateCustomArchive(const CustomArchive &val) {
	for (auto &archive : _customArchives) {
		if (archive.id == val.id) {
			archive = val;
			break;
		}
	}
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::setCustomArchiveIcon(int id, const QString &icon) {
	for (auto &archive : _customArchives) {
		if (archive.id == id) {
			archive.icon = icon;
			break;
		}
	}
	_customArchivesChanges.fire({});
	save();
}

CustomArchive *AyuSettings::findCustomArchive(int id) {
	for (auto &archive : _customArchives) {
		if (archive.id == id) {
			return &archive;
		}
	}
	return nullptr;
}

const CustomArchive *AyuSettings::findCustomArchive(int id) const {
	for (const auto &archive : _customArchives) {
		if (archive.id == id) {
			return &archive;
		}
	}
	return nullptr;
}

int AyuSettings::findCustomArchiveIdForPeer(uint64 peerId) const {
	for (const auto &archive : _customArchives) {
		if (std::find(archive.peerIds.begin(), archive.peerIds.end(), peerId)
			!= archive.peerIds.end()) {
			return archive.id;
		}
	}
	return 0;
}

bool AyuSettings::isPeerInCustomArchive(uint64 peerId) const {
	return findCustomArchiveIdForPeer(peerId) != 0;
}

void AyuSettings::addPeerToCustomArchive(int archiveId, uint64 peerId) {
	auto changed = false;
	for (auto &archive : _customArchives) {
		const auto before = archive.peerIds.size();
		std::erase(archive.peerIds, peerId);
		std::erase(archive.pinnedPeerIds, peerId);
		if (archive.peerIds.size() != before) {
			changed = true;
		}
	}
	for (auto &archive : _customArchives) {
		if (archive.id != archiveId) {
			continue;
		}
		if (std::find(archive.peerIds.begin(), archive.peerIds.end(), peerId)
			== archive.peerIds.end()) {
			archive.peerIds.push_back(peerId);
			changed = true;
		}
		break;
	}
	if (!changed) {
		return;
	}
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::removePeerFromCustomArchive(int archiveId, uint64 peerId) {
	for (auto &archive : _customArchives) {
		if (archive.id != archiveId) {
			continue;
		}
		std::erase(archive.peerIds, peerId);
		std::erase(archive.pinnedPeerIds, peerId);
		break;
	}
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::removePeerFromAllCustomArchives(uint64 peerId) {
	auto changed = false;
	for (auto &archive : _customArchives) {
		const auto before = archive.peerIds.size();
		std::erase(archive.peerIds, peerId);
		std::erase(archive.pinnedPeerIds, peerId);
		if (archive.peerIds.size() != before) {
			changed = true;
		}
	}
	if (!changed) {
		return;
	}
	_customArchivesChanges.fire({});
	save();
}

void AyuSettings::setPeerPinnedInCustomArchive(
		int archiveId,
		uint64 peerId,
		bool pinned) {
	for (auto &archive : _customArchives) {
		if (archive.id != archiveId) {
			continue;
		}
		if (pinned) {
			if (std::find(
					archive.pinnedPeerIds.begin(),
					archive.pinnedPeerIds.end(),
					peerId) == archive.pinnedPeerIds.end()) {
				archive.pinnedPeerIds.push_back(peerId);
			}
			if (std::find(archive.peerIds.begin(), archive.peerIds.end(), peerId)
				== archive.peerIds.end()) {
				archive.peerIds.push_back(peerId);
			}
		} else {
			std::erase(archive.pinnedPeerIds, peerId);
		}
		break;
	}
	_customArchivesChanges.fire({});
	save();
}

const CustomProfileBackground *AyuSettings::customProfileBackground(
		uint64 peerId,
		CustomProfileBackground::Zone zone) const {
	for (const auto &entry : _customProfileBackgrounds) {
		if (entry.peerId == peerId && entry.zone == zone) {
			return &entry;
		}
	}
	return nullptr;
}

void AyuSettings::setCustomProfileBackground(
		uint64 peerId,
		CustomProfileBackground::Zone zone,
		const QString &path,
		bool isVideo) {
	for (auto &entry : _customProfileBackgrounds) {
		if (entry.peerId == peerId && entry.zone == zone) {
			if (entry.path == path && entry.isVideo == isVideo) {
				return;
			}
			entry.path = path;
			entry.isVideo = isVideo;
			_customProfileBackgroundChanges.fire({});
			save();
			return;
		}
	}
	_customProfileBackgrounds.push_back({
		.peerId = peerId,
		.zone = zone,
		.path = path,
		.isVideo = isVideo,
	});
	_customProfileBackgroundChanges.fire({});
	save();
}

void AyuSettings::clearCustomProfileBackground(
		uint64 peerId,
		CustomProfileBackground::Zone zone) {
	const auto i = ranges::find_if(_customProfileBackgrounds, [&](
			const CustomProfileBackground &entry) {
		return entry.peerId == peerId && entry.zone == zone;
	});
	if (i == end(_customProfileBackgrounds)) {
		return;
	}
	if (!i->path.isEmpty() && QFile::exists(i->path)) {
		QFile::remove(i->path);
	}
	_customProfileBackgrounds.erase(i);
	_customProfileBackgroundChanges.fire({});
	save();
}

void AyuSettings::clearAllCustomProfileBackgrounds(uint64 peerId) {
	for (auto i = begin(_customProfileBackgrounds); i != end(_customProfileBackgrounds);) {
		if (i->peerId != peerId) {
			++i;
			continue;
		}
		if (!i->path.isEmpty() && QFile::exists(i->path)) {
			QFile::remove(i->path);
		}
		i = _customProfileBackgrounds.erase(i);
	}
	_customProfileBackgroundChanges.fire({});
	save();
}

void AyuSettings::setAccountProfilesVisualVariant(AccountProfilesVisualVariant val) {
	if (_accountProfilesVisualVariant.current() == val) return;
	_accountProfilesVisualVariant = val;
	save();
}

void AyuSettings::setAccountProfiles(const std::vector<AccountProfile>& val) {
	_accountProfiles = val;
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::addAccountProfile(const AccountProfile& val) {
	_accountProfiles.push_back(val);
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::addAccountProfile(const QString &name) {
	int maxId = 0;
	for (const auto &p : _accountProfiles) {
		if (p.id > maxId) maxId = p.id;
	}
	AccountProfile p;
	p.id = maxId + 1;
	p.name = name;
	_accountProfiles.push_back(p);
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::removeAccountProfile(int id) {
	std::erase_if(_accountProfiles, [id](const AccountProfile &p) { return p.id == id; });
	if (_activeProfileId.current() == id) {
		_activeProfileId = 0;
	}
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::updateAccountProfile(const AccountProfile& val) {
	for (auto &p : _accountProfiles) {
		if (p.id == val.id) {
			p = val;
			break;
		}
	}
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::renameAccountProfile(int id, const QString &newName) {
	for (auto &p : _accountProfiles) {
		if (p.id == id) {
			p.name = newName;
			break;
		}
	}
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::setAccountProfileIcon(int id, const QString &icon) {
	for (auto &p : _accountProfiles) {
		if (p.id == id) {
			p.icon = icon;
			break;
		}
	}
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::setMaxAccountProfiles(int val) {
	val = std::clamp(val, 1, 50);
	if (_maxAccountProfiles.current() == val) return;
	_maxAccountProfiles = val;
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::toggleAccountInProfile(int profileId, uint64 accountId) {
	for (auto &p : _accountProfiles) {
		if (p.id == profileId) {
			auto it = std::find(p.accountIds.begin(), p.accountIds.end(), accountId);
			if (it != p.accountIds.end()) {
				p.accountIds.erase(it);
			} else {
				p.accountIds.push_back(accountId);
			}
			break;
		}
	}
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::setAccountProfileMap(const std::map<uint64, std::vector<int>>& val) {
	_accountProfileMap = val;
	_accountProfilesChanges.fire({});
	save();
}

void AyuSettings::assignAccountToProfile(uint64 accountId, int profileId) {
	auto &list = _accountProfileMap[accountId];
	if (std::find(list.begin(), list.end(), profileId) == list.end()) {
		list.push_back(profileId);
		_accountProfilesChanges.fire({});
		save();
	}
}

void AyuSettings::unassignAccountFromProfile(uint64 accountId, int profileId) {
	auto it = _accountProfileMap.find(accountId);
	if (it != _accountProfileMap.end()) {
		std::erase(it->second, profileId);
		_accountProfilesChanges.fire({});
		save();
	}
}

std::vector<int> AyuSettings::getAccountProfileIds(uint64 accountId) const {
	auto it = _accountProfileMap.find(accountId);
	return (it != _accountProfileMap.end()) ? it->second : std::vector<int>{};
}
