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
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Whitegram::E2EE {
namespace {

constexpr auto kRegisterAttempts = 10;
constexpr auto kRegisterRetryDelay = crl::time(30 * 1000);
constexpr auto kFetchTimeout = 6 * 1000;
constexpr auto kFreshFor = crl::time(20 * 1000);

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
	fetchPeerPublicKeys(userId, nullptr);
}

void KeyManager::fetchPeerPublicKeys(
		uint64 userId,
		Fn<void(std::vector<QByteArray> keys)> callback) {
	const auto it = _pendingKeyRequests.find(userId);
	if (it != _pendingKeyRequests.end()) {
		it->second.push_back(std::move(callback));
		return;
	}
	_pendingKeyRequests[userId].push_back(std::move(callback));

	const auto url = QUrl(Backend::BaseUrl()
		+ u"/api/v1/e2ee/keys/"_q
		+ QString::number(userId));
	auto request = QNetworkRequest(url);
	request.setRawHeader("X-API-Key", Backend::ApiKey().toLatin1());
	request.setTransferTimeout(kFetchTimeout);
	Backend::ApplyTlsPinning(request);

	auto manager = new QNetworkAccessManager();
	auto reply = manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		auto keys = std::vector<QByteArray>();
		auto answered = false;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError) {
			answered = true;
			try {
				const auto j = nlohmann::json::parse(reply->readAll().constData());
				const auto add = [&](const std::string &b64) {
					const auto key = EnsureUncompressedPublicKey(
						QByteArray::fromBase64(QByteArray::fromStdString(b64)));
					if (!key.isEmpty() && !ranges::contains(keys, key)) {
						keys.push_back(key);
					}
				};
				if (j.contains("publicKeys") && j["publicKeys"].is_array()) {
					for (const auto &entry : j["publicKeys"]) {
						if (entry.is_string()) {
							add(entry.get<std::string>());
						}
					}
				}
				if (keys.empty()) {
					add(j.value("publicKey", ""));
				}
			} catch (...) {
			}
		} else if (status == 404) {
			answered = true;
		}
		auto &self = KeyManager::Instance();
		if (answered) {
			self._fresh[userId] = Fresh{ .keys = keys, .at = crl::now() };
		}
		auto callbacks = std::vector<Fn<void(std::vector<QByteArray>)>>();
		if (const auto i = self._pendingKeyRequests.find(userId)
			; i != self._pendingKeyRequests.end()) {
			callbacks = std::move(i->second);
			self._pendingKeyRequests.erase(i);
		}
		for (const auto &callback : callbacks) {
			if (callback) {
				callback(answered ? keys : std::vector<QByteArray>());
			}
		}
		reply->deleteLater();
		manager->deleteLater();
	});
}

std::vector<QByteArray> KeyManager::pinnedKeys(uint64 userId) const {
	const auto i = _pinned.find(userId);
	return (i != _pinned.end()) ? i->second : std::vector<QByteArray>();
}

void KeyManager::pin(uint64 userId, std::vector<QByteArray> keys) {
	if (keys.empty()) {
		return;
	}
	_pinned[userId] = std::move(keys);
	saveDiskCache();
}

void KeyManager::withPeerKeys(
		std::shared_ptr<Ui::Show> show,
		uint64 userId,
		Fn<void(std::vector<QByteArray> keys)> done) {
	const auto proceed = [=](std::vector<QByteArray> fetched) {
		auto &manager = KeyManager::Instance();
		const auto pinned = manager.pinnedKeys(userId);
		if (fetched.empty()) {
			done(pinned);
			return;
		} else if (pinned.empty()) {
			manager.pin(userId, fetched);
			done(fetched);
			return;
		}
		auto added = std::vector<QByteArray>();
		for (const auto &key : fetched) {
			if (!ranges::contains(pinned, key)) {
				added.push_back(key);
			}
		}
		if (added.empty()) {
			manager.pin(userId, fetched);
			done(fetched);
			return;
		}
		auto fingerprints = QStringList();
		for (const auto &key : added) {
			fingerprints.push_back(Fingerprint(key));
		}
		const auto ru = IsRussian();
		show->showBox(Ui::MakeConfirmBox({
			.text = (ru
				? u"У собеседника новое устройство или ключ шифрования изменился. Так бывает после переустановки или входа с другого компьютера, но это может быть и подмена.\n\nНовый отпечаток:\n%1\n\nСверьте его с собеседником. Отправить с новым ключом?"_q
				: u"Your contact has a new device or a changed encryption key. This happens after a reinstall or a login on another computer, but it may also be a substitution.\n\nNew fingerprint:\n%1\n\nCompare it with your contact. Send with the new key?"_q
				).arg(fingerprints.join(u"\n"_q)),
			.confirmed = [=](Fn<void()> close) {
				KeyManager::Instance().pin(userId, fetched);
				close();
				done(fetched);
			},
			.confirmText = (ru ? u"Отправить"_q : u"Send"_q),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	const auto i = _fresh.find(userId);
	if (i != _fresh.end() && crl::now() - i->second.at < kFreshFor) {
		proceed(i->second.keys);
	} else {
		fetchPeerPublicKeys(userId, proceed);
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
			auto keys = std::vector<QByteArray>();
			const auto add = [&](const std::string &b64) {
				const auto key = EnsureUncompressedPublicKey(
					QByteArray::fromBase64(QByteArray::fromStdString(b64)));
				if (!key.isEmpty() && !ranges::contains(keys, key)) {
					keys.push_back(key);
				}
			};
			if (it.value().is_array()) {
				for (const auto &entry : it.value()) {
					if (entry.is_string()) {
						add(entry.get<std::string>());
					}
				}
			} else if (it.value().is_string()) {
				add(it.value().get<std::string>());
			}
			if (!keys.empty()) {
				_pinned[uid] = std::move(keys);
			}
		}
	} catch (...) {
	}
}

void KeyManager::saveDiskCache() {
	const auto path = DiskCachePath();
	QFileInfo(path).dir().mkpath(u"."_q);
	nlohmann::json j = nlohmann::json::object();
	for (const auto &[uid, keys] : _pinned) {
		auto list = nlohmann::json::array();
		for (const auto &key : keys) {
			list.push_back(key.toBase64().toStdString());
		}
		j[std::to_string(uid)] = list;
	}
	const auto str = j.dump(2);
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(str.data(), qint64(str.size()));
		file.commit();
	}
}

} // namespace Whitegram::E2EE
