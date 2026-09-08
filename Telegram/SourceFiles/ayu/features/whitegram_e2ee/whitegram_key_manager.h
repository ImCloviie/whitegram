// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "base/basic_types.h"
#include "base/flat_set.h"
#include "rpl/producer.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <map>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Whitegram::E2EE {

class KeyManager final {
public:
	static KeyManager &Instance();

	void init(not_null<Main::Session*> session);
	void registerMyKey(not_null<Main::Session*> session);

	void fetchPeerPublicKey(
		uint64 userId,
		Fn<void(const QByteArray &pubKey)> callback,
		bool forceRefresh = false);

	void refreshPeerKeyIfNeeded(uint64 userId);

	[[nodiscard]] std::optional<QByteArray> cachedPeerPublicKey(uint64 userId) const;
	void cachePeerPublicKey(uint64 userId, const QByteArray &pubKey);

private:
	KeyManager();
	~KeyManager() = default;

	void loadDiskCache();
	void saveDiskCache();

	std::map<uint64, QByteArray> _peerKeysCache;
	base::flat_set<uint64> _registeredUsers;
	base::flat_set<uint64> _registeringUsers;
	base::flat_set<uint64> _refreshedUsersInSession;
	std::map<uint64, std::vector<Fn<void(const QByteArray &)>>> _pendingKeyRequests;
};

} // namespace Whitegram::E2EE
