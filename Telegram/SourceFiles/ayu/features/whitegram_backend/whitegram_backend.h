// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "ayu/ayu_settings.h"

namespace Whitegram::Backend {

using Zone = CustomProfileBackground::Zone;

[[nodiscard]] QString BaseUrl();
[[nodiscard]] QString ApiKey();

void UploadProfileBackground(
	uint64 userId,
	Zone zone,
	const QString &path,
	bool isVideo);
void RemoveProfileBackground(uint64 userId, Zone zone);
void FetchProfileBackgrounds(uint64 userId);

[[nodiscard]] bool HasCachedBackground(uint64 userId, Zone zone);
[[nodiscard]] std::optional<Zone> CachedTopZone(uint64 userId);
[[nodiscard]] std::optional<Zone> CachedBottomZone(uint64 userId);
[[nodiscard]] bool CachedUsesFullBackground(uint64 userId);
[[nodiscard]] QString CachedBackgroundPath(uint64 userId, Zone zone);
[[nodiscard]] bool CachedBackgroundIsVideo(uint64 userId, Zone zone);

[[nodiscard]] rpl::producer<uint64> profileBackgroundUpdates();

void InitClientInfo();
void FetchClientInfo();
[[nodiscard]] QString BetaBadgeText();
[[nodiscard]] rpl::producer<QString> betaBadgeTextValue();
[[nodiscard]] QString LocalPreBetaVersion();

void CheckUpdateAvailability();
[[nodiscard]] bool IsUpdateAvailable();
[[nodiscard]] rpl::producer<bool> updateAvailableValue();
void TriggerUpdateDownload();
void InstallReadyUpdate();
void RefreshUpdateAvailability();
void ClearUpdateAvailable();

} // namespace Whitegram::Backend
