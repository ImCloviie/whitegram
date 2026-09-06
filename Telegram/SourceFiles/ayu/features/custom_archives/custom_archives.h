#pragma once

#include "data/data_types.h"

class PeerData;

namespace Dialogs {
class MainList;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace AyuFeatures::CustomArchives {

[[nodiscard]] FilterId FilterIdForFolder(int folderId);

[[nodiscard]] bool IsArchiveUnlocked(int archiveId);
void UnlockArchive(int archiveId);
void LockArchive(int archiveId);
[[nodiscard]] bool CheckArchiveSecret(int archiveId, const QString &secret);
[[nodiscard]] bool CheckArchivePin(int archiveId, const QString &pin);
[[nodiscard]] bool CheckArchivePassword(
	int archiveId,
	const QString &password);
void SetArchivePin(int archiveId, const QString &pin);
void SetArchivePassword(int archiveId, const QString &password);
void ClearArchivePin(int archiveId);
void ClearArchivePassword(int archiveId);
void ClearArchiveLock(int archiveId);

[[nodiscard]] bool ShouldHidePeerFromMainList(uint64 peerId);
[[nodiscard]] int UnreadCountForFolder(
	not_null<Main::Session*> session,
	int folderId);
[[nodiscard]] QString FolderSubtitle(
	not_null<Main::Session*> session,
	int folderId);
[[nodiscard]] QString FolderIconEmoji(const QString &iconPath);
[[nodiscard]] QString FolderTitle(int folderId);

void LeaveTelegramArchiveIfNeeded(not_null<PeerData*> peer);
void RestorePeersFromTelegramArchive(
	not_null<Main::Session*> session);
void RefreshAllFolderPeers(not_null<Main::Session*> session);

void SyncFolderChatsList(
	not_null<Main::Session*> session,
	int folderId);
void SyncAllFolderChatsLists(not_null<Main::Session*> session);
[[nodiscard]] not_null<Dialogs::MainList*> FolderChatsList(
	not_null<Main::Session*> session,
	int folderId);

void MovePeerToArchive(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	int archiveId);

void RemovePeerFromArchives(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void RemovePeerFromCustomFolders(not_null<PeerData*> peer);

void OpenArchive(
	not_null<Window::SessionController*> controller,
	int archiveId);
void OpenCustomFolderInNewWindow(
	not_null<Window::SessionController*> controller,
	int archiveId);

void ShowArchivesManager(
	not_null<Window::SessionController*> controller);

void RefreshPeerChatList(not_null<PeerData*> peer);

} // namespace AyuFeatures::CustomArchives
