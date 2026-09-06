// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_e2ee/whitegram_key_manager.h"

#include "ayu/features/whitegram_backend/whitegram_backend.h"
#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "ayu/libs/json.hpp"
#include "core/application.h"
#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Whitegram::E2EE {
namespace {

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

void KeyManager::registerMyKey(not_null<Main::Session*> session) {
	if (_registered) {
		return;
	}
	const auto myPubKey = GetMyPublicKey();
	if (myPubKey.isEmpty()) {
		return;
	}

	const auto userId = session->userId().bare;
	const auto baseUrl = Backend::BaseUrl();
	const auto url = QUrl(baseUrl + u"/api/v1/e2ee/keys/register"_q);

	auto request = QNetworkRequest(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setRawHeader("X-API-Key", Backend::ApiKey().toLatin1());

	nlohmann::json body;
	body["userId"] = userId;
	body["publicKey"] = myPubKey.toBase64().toStdString();
	const auto data = QByteArray::fromStdString(body.dump());

	auto manager = new QNetworkAccessManager();
	auto reply = manager->post(request, data);

	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (reply->error() == QNetworkReply::NoError) {
			KeyManager::Instance()._registered = true;
		}
		reply->deleteLater();
		manager->deleteLater();
	});
}

void KeyManager::fetchPeerPublicKey(
		uint64 userId,
		Fn<void(const QByteArray &pubKey)> callback) {
	if (const auto cached = cachedPeerPublicKey(userId)) {
		if (callback) {
			callback(*cached);
		}
		return;
	}

	const auto baseUrl = Backend::BaseUrl();
	const auto url = QUrl(baseUrl + u"/api/v1/e2ee/keys/"_q + QString::number(userId));

	auto request = QNetworkRequest(url);
	request.setRawHeader("X-API-Key", Backend::ApiKey().toLatin1());

	auto manager = new QNetworkAccessManager();
	auto reply = manager->get(request);

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
		if (callback) {
			callback(pubKey);
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
	if (pubKey.isEmpty()) {
		return;
	}
	_peerKeysCache[userId] = pubKey;
	saveDiskCache();
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
			_peerKeysCache[uid] = QByteArray::fromBase64(b64.toLatin1());
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
