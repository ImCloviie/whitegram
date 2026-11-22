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
	void registerMyKey(not_null<Main::Session*> session, int attempt = 0);

	void fetchPeerPublicKeys(
		uint64 userId,
		Fn<void(std::vector<QByteArray> keys)> callback);
	void refreshPeerKeyIfNeeded(uint64 userId);

	// WHY: the server could swap a key; the first one seen is kept and a
	// changed one is used only after the user confirms its fingerprint.
	void withPeerKeys(
		std::shared_ptr<Ui::Show> show,
		uint64 userId,
		Fn<void(std::vector<QByteArray> keys)> done);

private:
	struct Fresh {
		std::vector<QByteArray> keys;
		crl::time at = 0;
	};

	KeyManager();
	~KeyManager() = default;

	void loadDiskCache();
	void saveDiskCache();
	[[nodiscard]] std::vector<QByteArray> pinnedKeys(uint64 userId) const;
	void pin(uint64 userId, std::vector<QByteArray> keys);

	std::map<uint64, std::vector<QByteArray>> _pinned;
	std::map<uint64, Fresh> _fresh;
	base::flat_set<uint64> _registeredUsers;
	base::flat_set<uint64> _registeringUsers;
	base::flat_set<uint64> _refreshedUsersInSession;
	std::map<
		uint64,
		std::vector<Fn<void(std::vector<QByteArray>)>>> _pendingKeyRequests;
};

} // namespace Whitegram::E2EE
