#include "ayu/features/custom_archives/custom_archives.h"

#include "apiwrap.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/settings/custom_archives_box.h"
#include "base/assertion.h"
#include "base/flat_set.h"
#include "base/random.h"
#include "dialogs/dialogs_pinned_list.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "data/notify/data_peer_notify_settings.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/delayed_activation.h"
#include "window/window_controller.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"

#include <QCryptographicHash>
#include <set>

namespace AyuFeatures::CustomArchives {
namespace {

std::set<int> &UnlockedArchives() {
	static auto unlocked = std::set<int>();
	return unlocked;
}

[[nodiscard]] QString MakeSalt() {
	return QString::number(base::RandomValue<uint64>(), 16);
}

[[nodiscard]] QString HashSecret(const QString &salt, const QString &secret) {
	const auto bytes = QCryptographicHash::hash(
		(salt + secret).toUtf8(),
		QCryptographicHash::Sha256);
	return QString::fromLatin1(bytes.toHex());
}

void ApplyMute(not_null<PeerData*> peer, bool muted) {
	if (!muted) {
		return;
	}
	peer->owner().notifySettings().update(
		peer,
		Data::MuteValue{ .forever = true });
}

void OpenArchiveUnlocked(
		not_null<Window::SessionController*> controller,
		int archiveId) {
	SyncFolderChatsList(&controller->session(), archiveId);
	controller->window().hideSettingsAndLayer(anim::type::normal);
	controller->openCustomArchive(archiveId);
}

constexpr auto kCustomFolderFilterBase = FilterId(-1'000'000);

const auto kFolderIcons = std::vector<std::pair<QString, QString>>{
	{ u"folders/folders_home"_q, u"🏠"_q },
	{ u"folders/folders_work"_q, u"💼"_q },
	{ u"folders/folders_game"_q, u"🎮"_q },
	{ u"folders/folders_crown"_q, u"👑"_q },
	{ u"folders/folders_favorite"_q, u"⭐"_q },
	{ u"folders/folders_love"_q, u"❤️"_q },
	{ u"folders/folders_cat"_q, u"🐱"_q },
	{ u"folders/folders_book"_q, u"📖"_q },
	{ u"folders/folders_note"_q, u"🎵"_q },
	{ u"folders/folders_palette"_q, u"🎨"_q },
	{ u"folders/folders_sport"_q, u"⚽"_q },
	{ u"folders/folders_travel"_q, u"✈️"_q },
	{ u"folders/folders_light"_q, u"💡"_q },
	{ u"folders/folders_money"_q, u"💰"_q },
	{ u"folders/folders_study"_q, u"🎓"_q },
	{ u"folders/folders_flower"_q, u"🌸"_q },
	{ u"folders/folders_party"_q, u"🎉"_q },
	{ u"folders/folders_mask"_q, u"🎭"_q },
	{ u"folders/folders_like"_q, u"👍"_q },
	{ u"folders/folders_private"_q, u"🔒"_q },
	{ u"folders/folders_trade"_q, u"📊"_q },
	{ u"folders/folders_airplane"_q, u"🛫"_q },
	{ u"folders/folders_custom"_q, u"📁"_q },
};

} // namespace

FilterId FilterIdForFolder(int folderId) {
	Expects(folderId > 0);

	return kCustomFolderFilterBase - folderId;
}

bool IsArchiveUnlocked(int archiveId) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(archiveId);
	if (!archive || !archive->isLocked()) {
		return true;
	}
	return UnlockedArchives().find(archiveId) != UnlockedArchives().end();
}

void UnlockArchive(int archiveId) {
	UnlockedArchives().insert(archiveId);
}

void LockArchive(int archiveId) {
	UnlockedArchives().erase(archiveId);
}

bool CheckArchiveSecret(int archiveId, const QString &secret) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(archiveId);
	if (!archive || secret.isEmpty()) {
		return false;
	}
	if (archive->hasPin()
		&& HashSecret(archive->pinSalt, secret) == archive->pinHash) {
		return true;
	}
	if (archive->hasPassword()
		&& HashSecret(archive->passwordSalt, secret) == archive->passwordHash) {
		return true;
	}
	return false;
}

bool CheckArchivePin(int archiveId, const QString &pin) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(archiveId);
	if (!archive || !archive->hasPin() || pin.isEmpty()) {
		return false;
	}
	return HashSecret(archive->pinSalt, pin) == archive->pinHash;
}

bool CheckArchivePassword(int archiveId, const QString &password) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(archiveId);
	if (!archive || !archive->hasPassword() || password.isEmpty()) {
		return false;
	}
	return HashSecret(archive->passwordSalt, password) == archive->passwordHash;
}

void SetArchivePin(int archiveId, const QString &pin) {
	auto &settings = AyuSettings::getInstance();
	auto archive = settings.findCustomArchive(archiveId);
	if (!archive) {
		return;
	}
	auto updated = *archive;
	updated.pinSalt = MakeSalt();
	updated.pinHash = HashSecret(updated.pinSalt, pin);
	settings.updateCustomArchive(updated);
	UnlockArchive(archiveId);
}

void SetArchivePassword(int archiveId, const QString &password) {
	auto &settings = AyuSettings::getInstance();
	auto archive = settings.findCustomArchive(archiveId);
	if (!archive) {
		return;
	}
	auto updated = *archive;
	updated.passwordSalt = MakeSalt();
	updated.passwordHash = HashSecret(updated.passwordSalt, password);
	settings.updateCustomArchive(updated);
	UnlockArchive(archiveId);
}

void ClearArchivePin(int archiveId) {
	auto &settings = AyuSettings::getInstance();
	auto archive = settings.findCustomArchive(archiveId);
	if (!archive) {
		return;
	}
	auto updated = *archive;
	updated.pinSalt.clear();
	updated.pinHash.clear();
	settings.updateCustomArchive(updated);
	if (!updated.isLocked()) {
		LockArchive(archiveId);
	}
}

void ClearArchivePassword(int archiveId) {
	auto &settings = AyuSettings::getInstance();
	auto archive = settings.findCustomArchive(archiveId);
	if (!archive) {
		return;
	}
	auto updated = *archive;
	updated.passwordSalt.clear();
	updated.passwordHash.clear();
	settings.updateCustomArchive(updated);
	if (!updated.isLocked()) {
		LockArchive(archiveId);
	}
}

void ClearArchiveLock(int archiveId) {
	auto &settings = AyuSettings::getInstance();
	auto archive = settings.findCustomArchive(archiveId);
	if (!archive) {
		return;
	}
	auto updated = *archive;
	updated.pinSalt.clear();
	updated.pinHash.clear();
	updated.passwordSalt.clear();
	updated.passwordHash.clear();
	settings.updateCustomArchive(updated);
	LockArchive(archiveId);
}

bool ShouldHidePeerFromMainList(uint64 peerId) {
	const auto &settings = AyuSettings::getInstance();
	if (!settings.enableCustomArchives()) {
		return false;
	}
	for (const auto &archive : settings.customArchives()) {
		if (!archive.hideFromMainList) {
			continue;
		}
		if (std::find(archive.peerIds.begin(), archive.peerIds.end(), peerId)
			!= archive.peerIds.end()) {
			return true;
		}
	}
	return false;
}

int UnreadCountForFolder(
		not_null<Main::Session*> session,
		int folderId) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(folderId);
	if (!archive) {
		return 0;
	}
	auto unread = 0;
	for (const auto peerIdValue : archive->peerIds) {
		const auto peer = session->data().peerLoaded(PeerId(peerIdValue));
		if (!peer) {
			continue;
		}
		const auto history = session->data().historyLoaded(peer);
		if (!history) {
			continue;
		}
		const auto state = history->chatListBadgesState();
		unread += state.unreadCounter;
	}
	return unread;
}

QString FolderSubtitle(
		not_null<Main::Session*> session,
		int folderId) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(folderId);
	if (!archive) {
		return QString();
	}
	if (archive->isLocked() && !IsArchiveUnlocked(folderId)) {
		return QString::fromUtf8("Защищённая папка");
	}
	if (archive->peerIds.empty()) {
		return QString::fromUtf8("Пустая папка");
	}
	auto names = QStringList();
	constexpr auto kMax = 3;
	for (const auto peerIdValue : archive->peerIds) {
		if (names.size() >= kMax) {
			break;
		}
		const auto peer = session->data().peerLoaded(PeerId(peerIdValue));
		if (peer) {
			names.push_back(peer->name());
		}
	}
	if (names.isEmpty()) {
		return QString::fromUtf8("%1 чат(ов)").arg(int(archive->peerIds.size()));
	}
	auto text = names.join(u", "_q);
	if (int(archive->peerIds.size()) > names.size()) {
		text += u"…"_q;
	}
	return text;
}

QString FolderIconEmoji(const QString &iconPath) {
	for (const auto &[path, emoji] : kFolderIcons) {
		if (path == iconPath) {
			return emoji;
		}
	}
	return u"📂"_q;
}

QString FolderTitle(int folderId) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(folderId);
	return archive ? archive->name : QString::fromUtf8("Папка");
}

not_null<Dialogs::MainList*> FolderChatsList(
		not_null<Main::Session*> session,
		int folderId) {
	return session->data().chatsFilters().chatsList(
		FilterIdForFolder(folderId));
}

void SyncFolderChatsList(
		not_null<Main::Session*> session,
		int folderId) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(folderId);
	if (!archive) {
		return;
	}
	const auto filterId = FilterIdForFolder(folderId);
	const auto list = FolderChatsList(session, folderId);

	auto current = base::flat_set<not_null<History*>>();
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			current.emplace(history);
		}
	}

	auto wanted = base::flat_set<not_null<History*>>();
	auto wantedOrder = std::vector<not_null<History*>>();
	wantedOrder.reserve(archive->peerIds.size());
	for (const auto peerIdValue : archive->peerIds) {
		const auto peer = session->data().peer(PeerId(peerIdValue));
		const auto history = session->data().history(peer);
		wanted.emplace(history);
		wantedOrder.push_back(history);
	}

	for (const auto history : current) {
		if (!wanted.contains(history)) {
			history->removeFromChatList(filterId, list);
		}
	}
	for (const auto history : wantedOrder) {
		if (!current.contains(history)) {
			history->addToChatList(filterId, list);
		}
	}

	auto pinned = std::vector<not_null<History*>>();
	pinned.reserve(archive->pinnedPeerIds.size());
	for (const auto peerIdValue : archive->pinnedPeerIds) {
		const auto peer = session->data().peerLoaded(PeerId(peerIdValue));
		if (!peer) {
			continue;
		}
		const auto history = session->data().historyLoaded(peer);
		if (history && wanted.contains(history)) {
			pinned.push_back(history);
		}
	}
	list->pinned()->applyList(pinned);
	list->setLoaded(true);
}

void SyncAllFolderChatsLists(not_null<Main::Session*> session) {
	if (!AyuSettings::getInstance().enableCustomArchives()) {
		return;
	}
	for (const auto &folder : AyuSettings::getInstance().customArchives()) {
		SyncFolderChatsList(session, folder.id);
	}
}

void LeaveTelegramArchiveIfNeeded(not_null<PeerData*> peer) {
	const auto history = peer->owner().historyLoaded(peer);
	if (!history || !history->folder()) {
		return;
	}
	peer->session().api().toggleHistoryArchived(history, false, [] {});
}

void RestorePeersFromTelegramArchive(
		not_null<Main::Session*> session) {
	const auto &settings = AyuSettings::getInstance();
	if (!settings.enableCustomArchives()) {
		return;
	}
	for (const auto &folder : settings.customArchives()) {
		for (const auto peerIdValue : folder.peerIds) {
			if (const auto peer = session->data().peerLoaded(
					PeerId(peerIdValue))) {
				LeaveTelegramArchiveIfNeeded(peer);
			}
		}
	}
}

void RefreshAllFolderPeers(not_null<Main::Session*> session) {
	auto seen = base::flat_set<uint64>();
	for (const auto &folder : AyuSettings::getInstance().customArchives()) {
		for (const auto peerIdValue : folder.peerIds) {
			if (!seen.emplace(peerIdValue).second) {
				continue;
			}
			RefreshPeerChatList(session->data().peer(PeerId(peerIdValue)));
		}
	}
}

void RefreshPeerChatList(not_null<PeerData*> peer) {
	const auto history = peer->owner().history(peer);
	const auto should = history->shouldBeInChatList();
	if (should == history->inChatList()) {
		if (should) {
			history->updateChatListSortPosition();
		}
		return;
	} else if (!should) {
		history->updateChatListExistence();
		return;
	}
	if (!history->chatListTimeId() && !history->fixedOnTopIndex()) {
		history->owner().histories().requestDialogEntry(history);
		return;
	}
	history->updateChatListSortPosition();
}

void MovePeerToArchive(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		int archiveId) {
	auto &settings = AyuSettings::getInstance();
	if (!settings.enableCustomArchives()) {
		return;
	}
	const auto archive = settings.findCustomArchive(archiveId);
	if (!archive) {
		return;
	}
	const auto peerId = peer->id.value;
	const auto muted = archive->muted;

	settings.addPeerToCustomArchive(archiveId, peerId);
	ApplyMute(peer, muted);
	LeaveTelegramArchiveIfNeeded(peer);
	RefreshPeerChatList(peer);
	SyncFolderChatsList(&controller->session(), archiveId);
}

void RemovePeerFromCustomFolders(not_null<PeerData*> peer) {
	auto &settings = AyuSettings::getInstance();
	const auto peerId = peer->id.value;
	const auto folderId = settings.findCustomArchiveIdForPeer(peerId);
	if (!settings.isPeerInCustomArchive(peerId)) {
		return;
	}
	settings.removePeerFromAllCustomArchives(peerId);
	RefreshPeerChatList(peer);
	if (folderId > 0) {
		SyncFolderChatsList(&peer->session(), folderId);
	}
}

void RemovePeerFromArchives(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	RemovePeerFromCustomFolders(peer);
}

void OpenArchive(
		not_null<Window::SessionController*> controller,
		int archiveId) {
	if (IsArchiveUnlocked(archiveId)) {
		OpenArchiveUnlocked(controller, archiveId);
		return;
	}
	controller->show(Box(
		CustomArchiveUnlockBox,
		controller,
		archiveId,
		[=] {
			UnlockArchive(archiveId);
			OpenArchiveUnlocked(controller, archiveId);
		}));
}

void OpenCustomFolderInNewWindow(
		not_null<Window::SessionController*> controller,
		int archiveId) {
	if (archiveId <= 0) {
		return;
	}
	const auto open = [=] {
		Ui::PreventDelayedActivation();
		auto id = Window::SeparateId(
			Window::SeparateType::CustomFolder,
			&controller->session());
		id.customFolderId = archiveId;
		controller->showInNewWindow(id);
	};
	if (IsArchiveUnlocked(archiveId)) {
		open();
		return;
	}
	controller->show(Box(
		CustomArchiveUnlockBox,
		controller,
		archiveId,
		[=] {
			UnlockArchive(archiveId);
			open();
		}));
}

void ShowArchivesManager(
		not_null<Window::SessionController*> controller) {
	controller->show(Box(CustomArchivesBox, controller));
}

} // namespace AyuFeatures::CustomArchives
