// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_e2ee/whitegram_key_manager.h"

#include "ayu/features/whitegram_backend/whitegram_backend.h"
#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "ayu/libs/json.hpp"
#include "base/call_delayed.h"
#include "core/application.h"
#include "main/main_session.h"
#include "lang/lang_instance.h"
#include "logs.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"
#include "styles/style_layers.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Whitegram::E2EE {
namespace {

constexpr auto kRegisterAttempts = 10;
constexpr auto kRegisterRetryDelay = crl::time(30 * 1000);

[[nodiscard]] bool IsRussian() {
	const auto id = Lang::GetInstance().id();
	return id.startsWith(u"ru"_q) || Lang::GetInstance().baseId().startsWith(u"ru"_q);
}

// Grouped hex of the key hash, for the user to compare with the peer's.
[[nodiscard]] QString Fingerprint(const QByteArray &key) {
	const auto hex = QString::fromLatin1(QCryptographicHash::hash(
		key,
		QCryptographicHash::Sha256).toHex().left(32)).toUpper();
	auto result = QString();
	for (auto i = 0; i < hex.size(); i += 4) {
		result += (i ? u" "_q : QString()) + hex.mid(i, 4);
	}
	return result;
}

QString DiskCachePath() {
	return cWorkingDir() + u"tdata/whitegram/keys/peer_keys.json"_q;
}

} // namespace

KeyManager &KeyManager::Instance() {
	static KeyManager instance;
	return instance;
}

KeyManager::KeyManager() {
	loadDiskCache();
}

void KeyManager::init(not_null<Main::Session*> session) {
	registerMyKey(session);
}

void KeyManager::registerMyKey(
		not_null<Main::Session*> session,
		int attempt) {
	const auto userId = session->userId().bare;
	if (_registeredUsers.contains(userId) || _registeringUsers.contains(userId)) {
		return;
	}
	const auto myPubKey = GetMyPublicKey();
	if (myPubKey.isEmpty()) {
		return;
	}

	_registeringUsers.insert(userId);

	const auto baseUrl = Backend::BaseUrl();
	const auto url = QUrl(baseUrl + u"/api/v1/e2ee/keys/register"_q);

	auto request = QNetworkRequest(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setRawHeader("X-API-Key", Backend::ApiKey().toLatin1());
	Backend::ApplyTlsPinning(request);
	Backend::ApplyOwnerToken(request, userId);

	nlohmann::json body;
	body["userId"] = userId;
	body["publicKey"] = myPubKey.toBase64().toStdString();
	const auto data = QByteArray::fromStdString(body.dump());

	auto manager = new QNetworkAccessManager();
	auto reply = manager->post(request, data);

	QObject::connect(reply, &QNetworkReply::finished, [=] {
		KeyManager::Instance()._registeringUsers.erase(userId);
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError) {
			KeyManager::Instance()._registeredUsers.insert(userId);
		} else if (status == 401 && attempt + 1 < kRegisterAttempts) {
			// The device token may still be on its way on a first launch.
			base::call_delayed(kRegisterRetryDelay, session, [=] {
				KeyManager::Instance().registerMyKey(session, attempt + 1);
			});
		}
		reply->deleteLater();
		manager->deleteLater();
	});
}

void KeyManager::refreshPeerKeyIfNeeded(uint64 userId) {
	if (userId == 0 || _refreshedUsersInSession.contains(userId)) {
		return;
	}
	_refreshedUsersInSession.insert(userId);
	fetchPeerPublicKey(userId, nullptr, true);
}

void KeyManager::fetchPeerPublicKey(
		uint64 userId,
		Fn<void(const QByteArray &pubKey)> callback,
		bool forceRefresh) {
	if (!forceRefresh) {
		if (const auto cached = cachedPeerPublicKey(userId)) {
			if (callback) {
				callback(*cached);
			}
			return;
		}
	}

	const auto it = _pendingKeyRequests.find(userId);
	if (it != _pendingKeyRequests.end()) {
		if (callback) {
			it->second.push_back(std::move(callback));
		}
		return;
	}

	if (callback) {
		_pendingKeyRequests[userId].push_back(std::move(callback));
	} else {
		_pendingKeyRequests[userId] = {};
	}

	const auto baseUrl = Backend::BaseUrl();
	const auto url = QUrl(baseUrl + u"/api/v1/e2ee/keys/"_q + QString::number(userId));

	auto request = QNetworkRequest(url);
	request.setRawHeader("X-API-Key", Backend::ApiKey().toLatin1());
	Backend::ApplyTlsPinning(request);

	auto manager = new QNetworkAccessManager();
	auto reply = manager->get(request);

	QTimer::singleShot(2500, reply, [=] {
		if (reply->isRunning()) {
			reply->abort();
		}
	});

	QObject::connect(reply, &QNetworkReply::finished, [=] {
		QByteArray pubKey;
		if (reply->error() == QNetworkReply::NoError) {
			const auto respData = reply->readAll();
			try {
				const auto j = nlohmann::json::parse(respData.constData());
				const auto keyB64 = QString::fromStdString(j.value("publicKey", ""));
				pubKey = QByteArray::fromBase64(keyB64.toLatin1());
				if (!pubKey.isEmpty()) {
					KeyManager::Instance().cachePeerPublicKey(userId, pubKey);
				}
			} catch (...) {
			}
		}

		auto callbacks = std::vector<Fn<void(const QByteArray&)>>();
		const auto itReq = KeyManager::Instance()._pendingKeyRequests.find(userId);
		if (itReq != KeyManager::Instance()._pendingKeyRequests.end()) {
			callbacks = std::move(itReq->second);
			KeyManager::Instance()._pendingKeyRequests.erase(itReq);
		}

		for (const auto &cb : callbacks) {
			if (cb) {
				cb(EnsureUncompressedPublicKey(pubKey));
			}
		}

		reply->deleteLater();
		manager->deleteLater();
	});
}

std::optional<QByteArray> KeyManager::cachedPeerPublicKey(uint64 userId) const {
	const auto it = _peerKeysCache.find(userId);
	if (it != _peerKeysCache.end()) {
		return it->second;
	}
	return std::nullopt;
}

void KeyManager::cachePeerPublicKey(uint64 userId, const QByteArray &pubKey) {
	const auto uncompressed = EnsureUncompressedPublicKey(pubKey);
	if (uncompressed.isEmpty()) {
		return;
	}
	const auto i = _peerKeysCache.find(userId);
	if (i != _peerKeysCache.end() && i->second != uncompressed) {
		// Kept aside: withPeerKey asks the user before it is trusted.
		LOG(("Whitegram E2EE: key of %1 changed on the server.").arg(userId));
		_changedPeerKeys[userId] = uncompressed;
		return;
	}
	_changedPeerKeys.erase(userId);
	_peerKeysCache[userId] = uncompressed;
	saveDiskCache();
}

void KeyManager::acceptChangedKey(uint64 userId) {
	const auto i = _changedPeerKeys.find(userId);
	if (i == _changedPeerKeys.end()) {
		return;
	}
	_peerKeysCache[userId] = i->second;
	_changedPeerKeys.erase(i);
	saveDiskCache();
}

void KeyManager::withPeerKey(
		std::shared_ptr<Ui::Show> show,
		uint64 userId,
		Fn<void(const QByteArray &pubKey)> done) {
	const auto proceed = [=](const QByteArray &fetched) {
		const auto pinned = cachedPeerPublicKey(userId);
		const auto key = pinned ? *pinned : fetched;
		const auto i = _changedPeerKeys.find(userId);
		if (i == _changedPeerKeys.end()) {
			done(key);
			return;
		}
		const auto changed = i->second;
		const auto ru = IsRussian();
		show->showBox(Ui::MakeConfirmBox({
			.text = (ru
				? u"Ключ шифрования собеседника изменился. Так бывает после переустановки, но это может быть и подмена.\n\nСтарый: %1\nНовый: %2\n\nСверьте новый отпечаток с собеседником. Использовать новый ключ?"_q
				: u"The contact's encryption key has changed. This happens after a reinstall, but it may also be a substitution.\n\nOld: %1\nNew: %2\n\nCompare the new fingerprint with your contact. Use the new key?"_q
				).arg(Fingerprint(key), Fingerprint(changed)),
			.confirmed = [=](Fn<void()> close) {
				KeyManager::Instance().acceptChangedKey(userId);
				close();
				done(changed);
			},
			.confirmText = (ru ? u"Использовать новый"_q : u"Use the new key"_q),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	if (const auto cached = cachedPeerPublicKey(userId)) {
		proceed(*cached);
	} else {
		fetchPeerPublicKey(userId, proceed);
	}
}

void KeyManager::loadDiskCache() {
	QFile file(DiskCachePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto data = file.readAll();
	file.close();
	try {
		const auto j = nlohmann::json::parse(data.constData());
		for (auto it = j.begin(); it != j.end(); ++it) {
			const auto uid = std::stoull(it.key());
			const auto b64 = QString::fromStdString(it.value().get<std::string>());
			const auto key = EnsureUncompressedPublicKey(QByteArray::fromBase64(b64.toLatin1()));
			if (!key.isEmpty()) {
				_peerKeysCache[uid] = key;
			}
		}
	} catch (...) {
	}
}

void KeyManager::saveDiskCache() {
	const auto path = DiskCachePath();
	QFileInfo(path).dir().mkpath(u"."_q);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	nlohmann::json j;
	for (const auto &[uid, key] : _peerKeysCache) {
		j[std::to_string(uid)] = key.toBase64().toStdString();
	}
	const auto str = j.dump(2);
	file.write(str.data(), str.size());
	file.close();
}

} // namespace Whitegram::E2EE
