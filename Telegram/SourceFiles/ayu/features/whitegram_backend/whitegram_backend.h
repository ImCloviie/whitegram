// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "ayu/ayu_settings.h"

namespace Whitegram::Backend {

using Zone = CustomProfileBackground::Zone;

// The largest photo or video a profile background can be. A video is
// played from memory, so no file of it is kept open.
inline constexpr auto kMaxProfileBackgroundSize = qint64(32 * 1024 * 1024);
// The largest file the server stores, it refuses one of exactly the size
// above.
inline constexpr auto kMaxProfileBackgroundUploadSize
	= kMaxProfileBackgroundSize - 1;

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

// The server keeps the ids of the clients that registered themselves,
// that is how the badge of a Whitegram user is known.
void RegisterUser(uint64 userId);
void FetchWhitegramUser(uint64 userId);
[[nodiscard]] bool IsWhitegramUser(uint64 userId);
[[nodiscard]] rpl::producer<uint64> whitegramUserUpdates();

// The badges given out through the admin panel of the backend: either a
// named type (developer, sponsor) or a custom badge with a server icon.
struct AdminBadge {
	// "developer", "sponsor" or "custom".
	QString type;
	// The id of the custom badge, for type == "custom".
	QString customId;
	// The icon variant of a developer or sponsor badge, e.g. "momentum".
	QString variant;
	// The toast text of the badge.
	QString text;

	[[nodiscard]] bool isDeveloper() const {
		return type == u"developer"_q;
	}
	[[nodiscard]] bool isSponsor() const {
		return type == u"sponsor"_q;
	}
	[[nodiscard]] bool isCustom() const {
		return type == u"custom"_q;
	}

	friend inline bool operator==(
		const AdminBadge &,
		const AdminBadge &) = default;
};
void FetchBadges();
[[nodiscard]] std::optional<AdminBadge> AdminBadgeFor(uint64 userId);

// The icon of a custom badge or of a built-in variant, downloaded from
// the backend and cached.
[[nodiscard]] QImage CustomBadgeIcon(const QString &customId);
[[nodiscard]] QString CustomBadgeText(const QString &customId);
[[nodiscard]] QImage VariantIcon(const QString &variant);

[[nodiscard]] rpl::producer<> badgeUpdates();
[[nodiscard]] QString BetaBadgeText();
[[nodiscard]] rpl::producer<QString> betaBadgeTextValue();
[[nodiscard]] QString LocalPreBetaVersion();

void CheckUpdateAvailability();
void SyncTimeSpent(uint64 userId, int64 localMinutes);
[[nodiscard]] bool IsUpdateAvailable();
[[nodiscard]] rpl::producer<bool> updateAvailableValue();
void TriggerUpdateDownload();
void InstallReadyUpdate();
void RefreshUpdateAvailability();
void ClearUpdateAvailable();

} // namespace Whitegram::Backend
