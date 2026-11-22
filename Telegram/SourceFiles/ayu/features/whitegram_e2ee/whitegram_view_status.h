/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Whitegram::E2EE {

struct ViewInfo {
	int64 openedAt = 0;
	int ttl = 0;

	[[nodiscard]] int64 expiresAt() const;
};

[[nodiscard]] int64 ServerNowMs();

void ReportOpened(
	not_null<Main::Session*> session,
	const QString &payload,
	int ttl);
void WatchViewStatus(not_null<Main::Session*> session, const QString &hash);
[[nodiscard]] std::optional<ViewInfo> ViewStatus(const QString &hash);
[[nodiscard]] rpl::producer<QString> ViewStatusUpdates();

void RevokeOnce(not_null<HistoryItem*> item);

} // namespace Whitegram::E2EE
