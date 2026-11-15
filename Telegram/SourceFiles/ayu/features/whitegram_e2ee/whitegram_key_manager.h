// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "base/basic_types.h"
#include "base/flat_set.h"
#include "rpl/producer.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <memory>
#include <map>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

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

	// WHY: the server could swap a key; the first one seen is kept and a
	// changed one is used only after the user confirms its fingerprint.
	void withPeerKey(
		std::shared_ptr<Ui::Show> show,
		uint64 userId,
		Fn<void(const QByteArray &pubKey)> done);

private:
	KeyManager();
	~KeyManager() = default;

	void loadDiskCache();
	void saveDiskCache();

	void acceptChangedKey(uint64 userId);

	std::map<uint64, QByteArray> _peerKeysCache;
	std::map<uint64, QByteArray> _changedPeerKeys;
	base::flat_set<uint64> _registeredUsers;
	base::flat_set<uint64> _registeringUsers;
	base::flat_set<uint64> _refreshedUsersInSession;
	std::map<uint64, std::vector<Fn<void(const QByteArray &)>>> _pendingKeyRequests;
};

} // namespace Whitegram::E2EE
