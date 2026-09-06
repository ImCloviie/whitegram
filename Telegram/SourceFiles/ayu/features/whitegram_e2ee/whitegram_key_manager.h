// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "base/basic_types.h"
#include "rpl/producer.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <map>

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
		Fn<void(const QByteArray &pubKey)> callback);

	[[nodiscard]] std::optional<QByteArray> cachedPeerPublicKey(uint64 userId) const;
	void cachePeerPublicKey(uint64 userId, const QByteArray &pubKey);

private:
	KeyManager();
	~KeyManager() = default;

	void loadDiskCache();
	void saveDiskCache();

	std::map<uint64, QByteArray> _peerKeysCache;
	bool _registered = false;
};

} // namespace Whitegram::E2EE
