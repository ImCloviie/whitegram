// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"

#include "ayu/libs/json.hpp"
#include "base/flat_map.h"
#include "core/application.h"
#include "history/history_item.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace Whitegram::E2EE {
namespace {

constexpr auto kNonceSize = size_t(12);
constexpr auto kTagSize = size_t(16);
constexpr auto kKeySize = size_t(32);
constexpr auto kP256X962Length = size_t(65);
constexpr auto kHkdfInfo = "Whitegram-E2EE-v1";

void HkdfSha256(
		uint8_t *out,
		size_t outLength,
		const uint8_t *secret,
		size_t secretLength,
		const uint8_t *salt,
		size_t saltLength,
		const uint8_t *info,
		size_t infoLength) {
	const auto context = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
	if (!context) {
		std::memset(out, 0, outLength);
		return;
	}
	auto length = outLength;
	static const auto kEmpty = uint8_t(0);
	const auto keyData = (secretLength == 0) ? &kEmpty : secret;
	const auto ok = (EVP_PKEY_derive_init(context) > 0)
		&& (EVP_PKEY_CTX_set_hkdf_md(context, EVP_sha256()) > 0)
		&& (saltLength == 0
			|| (EVP_PKEY_CTX_set1_hkdf_salt(context, salt, int(saltLength)) > 0))
		&& (EVP_PKEY_CTX_set1_hkdf_key(context, keyData, int(secretLength)) > 0)
		&& (infoLength == 0
			|| (EVP_PKEY_CTX_add1_hkdf_info(context, info, int(infoLength)) > 0))
		&& (EVP_PKEY_derive(context, out, &length) > 0);
	if (!ok) {
		std::memset(out, 0, outLength);
	}
	EVP_PKEY_CTX_free(context);
}

QByteArray AesGcmEncrypt(
		const QByteArray &key,
		const QByteArray &nonce,
		const QByteArray &plaintext,
		const QByteArray &aad,
		QByteArray &outTag) {
	if (key.size() != kKeySize || nonce.size() != kNonceSize) {
		return {};
	}
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return {};
	}
	auto ok = (EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, int(nonce.size()), nullptr) == 1)
		&& (EVP_EncryptInit_ex(
			context,
			nullptr,
			nullptr,
			reinterpret_cast<const uint8_t*>(key.constData()),
			reinterpret_cast<const uint8_t*>(nonce.constData())) == 1);

	auto outLength = 0;
	if (ok && !aad.isEmpty()) {
		ok = (EVP_EncryptUpdate(
			context,
			nullptr,
			&outLength,
			reinterpret_cast<const uint8_t*>(aad.constData()),
			int(aad.size())) == 1);
	}

	QByteArray ciphertext(plaintext.size(), '\0');
	auto totalLength = 0;
	if (ok && !plaintext.isEmpty()) {
		ok = (EVP_EncryptUpdate(
			context,
			reinterpret_cast<uint8_t*>(ciphertext.data()),
			&outLength,
			reinterpret_cast<const uint8_t*>(plaintext.constData()),
			int(plaintext.size())) == 1);
		totalLength = outLength;
	}

	ok = ok && (EVP_EncryptFinal_ex(
		context,
		reinterpret_cast<uint8_t*>(ciphertext.data()) + totalLength,
		&outLength) == 1);

	outTag.resize(kTagSize);
	ok = ok && (EVP_CIPHER_CTX_ctrl(
		context,
		EVP_CTRL_GCM_GET_TAG,
		int(kTagSize),
		outTag.data()) == 1);

	EVP_CIPHER_CTX_free(context);
	if (!ok) {
		return {};
	}
	return ciphertext;
}

std::optional<QByteArray> AesGcmDecrypt(
		const QByteArray &key,
		const QByteArray &nonce,
		const QByteArray &ciphertext,
		const QByteArray &tag,
		const QByteArray &aad) {
	if (key.size() != kKeySize || nonce.size() != kNonceSize || tag.size() != kTagSize) {
		return std::nullopt;
	}
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return std::nullopt;
	}
	auto ok = (EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, int(nonce.size()), nullptr) == 1)
		&& (EVP_DecryptInit_ex(
			context,
			nullptr,
			nullptr,
			reinterpret_cast<const uint8_t*>(key.constData()),
			reinterpret_cast<const uint8_t*>(nonce.constData())) == 1);

	auto outLength = 0;
	if (ok && !aad.isEmpty()) {
		ok = (EVP_DecryptUpdate(
			context,
			nullptr,
			&outLength,
			reinterpret_cast<const uint8_t*>(aad.constData()),
			int(aad.size())) == 1);
	}

	QByteArray plaintext(ciphertext.size(), '\0');
	auto totalLength = 0;
	if (ok && !ciphertext.isEmpty()) {
		ok = (EVP_DecryptUpdate(
			context,
			reinterpret_cast<uint8_t*>(plaintext.data()),
			&outLength,
			reinterpret_cast<const uint8_t*>(ciphertext.constData()),
			int(ciphertext.size())) == 1);
		totalLength = outLength;
	}

	ok = ok && (EVP_CIPHER_CTX_ctrl(
		context,
		EVP_CTRL_GCM_SET_TAG,
		int(kTagSize),
		const_cast<char*>(tag.constData())) == 1);

	ok = ok && (EVP_DecryptFinal_ex(
		context,
		reinterpret_cast<uint8_t*>(plaintext.data()) + totalLength,
		&outLength) == 1);

	EVP_CIPHER_CTX_free(context);
	if (!ok) {
		SecureCleanse(plaintext.data(), plaintext.size());
		return std::nullopt;
	}
	return plaintext;
}

std::optional<QByteArray> ComputeEcdh(
		const QByteArray &privateKeyBytes,
		const QByteArray &peerPublicKeyBytes) {
	if (privateKeyBytes.size() != kKeySize || peerPublicKeyBytes.size() != kP256X962Length) {
		return std::nullopt;
	}
	auto key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if (!key) {
		return std::nullopt;
	}
	const auto group = EC_KEY_get0_group(key);

	auto privBn = BN_bin2bn(
		reinterpret_cast<const uint8_t*>(privateKeyBytes.constData()),
		int(privateKeyBytes.size()),
		nullptr);
	if (!privBn || EC_KEY_set_private_key(key, privBn) != 1) {
		if (privBn) BN_free(privBn);
		EC_KEY_free(key);
		return std::nullopt;
	}

	auto point = EC_POINT_new(group);
	if (!point) {
		BN_free(privBn);
		EC_KEY_free(key);
		return std::nullopt;
	}

	const auto okPoint = (EC_POINT_oct2point(
		group,
		point,
		reinterpret_cast<const uint8_t*>(peerPublicKeyBytes.constData()),
		peerPublicKeyBytes.size(),
		nullptr) == 1);

	if (!okPoint) {
		EC_POINT_free(point);
		BN_free(privBn);
		EC_KEY_free(key);
		return std::nullopt;
	}

	QByteArray sharedSecret(32, '\0');
	const auto computedLen = ECDH_compute_key(
		sharedSecret.data(),
		sharedSecret.size(),
		point,
		key,
		nullptr);

	EC_POINT_free(point);
	BN_free(privBn);
	EC_KEY_free(key);

	if (computedLen != 32) {
		SecureCleanse(sharedSecret.data(), sharedSecret.size());
		return std::nullopt;
	}
	return sharedSecret;
}

QString IdentityKeyPath() {
	return cWorkingDir() + u"tdata/whitegram/keys/identity.key"_q;
}

constexpr auto kZw0 = QChar(ushort(0x200B));
constexpr auto kZw1 = QChar(ushort(0x200C));
constexpr auto kZw2 = QChar(ushort(0x200D));
constexpr auto kZw3 = QChar(ushort(0x2060));

const auto kZwStart = QString(u"\u200B\u200C\u200D\u200B"_q);

QString EncodeZeroWidth(const QByteArray &bytes) {
	if (bytes.size() > 65535) {
		return QString();
	}
	QString result;
	result.reserve(kZwStart.length() + 8 + bytes.size() * 4);
	result.append(kZwStart);

	const QChar map[4] = { kZw0, kZw1, kZw2, kZw3 };
	const auto length = uint16(bytes.size());
	const uint8 lenBytes[2] = { uint8(length >> 8), uint8(length & 0xFF) };
	for (const auto b : lenBytes) {
		result.append(map[(b >> 6) & 3]);
		result.append(map[(b >> 4) & 3]);
		result.append(map[(b >> 2) & 3]);
		result.append(map[b & 3]);
	}
	for (const auto byte : bytes) {
		const auto b = uint8(byte);
		result.append(map[(b >> 6) & 3]);
		result.append(map[(b >> 4) & 3]);
		result.append(map[(b >> 2) & 3]);
		result.append(map[b & 3]);
	}
	return result;
}

std::optional<QByteArray> DecodeZeroWidth(const QString &text) {
	auto decodeChar = [](QChar ch) -> int {
		if (ch == kZw0) return 0;
		if (ch == kZw1) return 1;
		if (ch == kZw2) return 2;
		if (ch == kZw3) return 3;
		return -1;
	};

	const auto sIdx = text.indexOf(kZwStart);
	if (sIdx >= 0) {
		const auto dataStart = sIdx + kZwStart.length();
		if (text.length() >= dataStart + 8) {
			uint8 lenBytes[2] = { 0, 0 };
			auto ok = true;
			for (auto j = 0; j < 2; ++j) {
				const auto b0 = decodeChar(text[dataStart + j * 4]);
				const auto b1 = decodeChar(text[dataStart + j * 4 + 1]);
				const auto b2 = decodeChar(text[dataStart + j * 4 + 2]);
				const auto b3 = decodeChar(text[dataStart + j * 4 + 3]);
				if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0) {
					ok = false;
					break;
				}
				lenBytes[j] = uint8((b0 << 6) | (b1 << 4) | (b2 << 2) | b3);
			}
			if (ok) {
				const auto payloadLen = (int(lenBytes[0]) << 8) | int(lenBytes[1]);
				const auto payloadStart = dataStart + 8;
				if (payloadLen > 0 && text.length() >= payloadStart + payloadLen * 4) {
					QByteArray out;
					out.reserve(payloadLen);
					auto payloadOk = true;
					for (auto i = 0; i < payloadLen; ++i) {
						const auto idx = payloadStart + i * 4;
						const auto b0 = decodeChar(text[idx]);
						const auto b1 = decodeChar(text[idx + 1]);
						const auto b2 = decodeChar(text[idx + 2]);
						const auto b3 = decodeChar(text[idx + 3]);
						if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0) {
							payloadOk = false;
							break;
						}
						out.append(char(uint8((b0 << 6) | (b1 << 4) | (b2 << 2) | b3)));
					}
					if (payloadOk && !out.isEmpty()) {
						return out;
					}
				}
			}
		}
	}

	return std::nullopt;
}

} // namespace

void SecureCleanse(void *ptr, size_t len) {
	if (ptr && len > 0) {
		OPENSSL_cleanse(ptr, len);
	}
}

void SecureCleanseString(QString &str) {
	if (!str.isEmpty()) {
		OPENSSL_cleanse(str.data(), str.size() * sizeof(QChar));
		str.clear();
	}
}

void SecureCleanseByteArray(QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		OPENSSL_cleanse(bytes.data(), bytes.size());
		bytes.clear();
	}
}

KeyPair GenerateKeyPair() {
	auto key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if (!key || EC_KEY_generate_key(key) != 1) {
		if (key) EC_KEY_free(key);
		return {};
	}
	const auto group = EC_KEY_get0_group(key);
	const auto point = EC_KEY_get0_public_key(key);
	const auto privBn = EC_KEY_get0_private_key(key);

	QByteArray pubBytes(kP256X962Length, '\0');
	const auto written = EC_POINT_point2oct(
		group,
		point,
		POINT_CONVERSION_UNCOMPRESSED,
		reinterpret_cast<uint8_t*>(pubBytes.data()),
		pubBytes.size(),
		nullptr);

	QByteArray privBytes(kKeySize, '\0');
	BN_bn2binpad(
		privBn,
		reinterpret_cast<uint8_t*>(privBytes.data()),
		int(kKeySize));

	EC_KEY_free(key);

	if (written != kP256X962Length) {
		return {};
	}

	return {
		.publicKey = pubBytes,
		.privateKey = privBytes,
	};
}

KeyPair GetOrGenerateIdentityKeyPair() {
	const auto path = IdentityKeyPath();
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) {
		const auto data = file.readAll();
		file.close();
		try {
			const auto j = nlohmann::json::parse(data.constData());
			const auto pubB64 = QString::fromStdString(j.value("pub", ""));
			const auto privB64 = QString::fromStdString(j.value("priv", ""));
			const auto pub = QByteArray::fromBase64(pubB64.toLatin1());
			const auto priv = QByteArray::fromBase64(privB64.toLatin1());
			if (pub.size() == kP256X962Length && priv.size() == kKeySize) {
				return { .publicKey = pub, .privateKey = priv };
			}
		} catch (...) {
		}
	}

	// Generate fresh keypair and save
	const auto pair = GenerateKeyPair();
	if (pair.publicKey.isEmpty() || pair.privateKey.isEmpty()) {
		return {};
	}

	QFileInfo(path).dir().mkpath(u"."_q);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		nlohmann::json j;
		j["pub"] = pair.publicKey.toBase64().toStdString();
		j["priv"] = pair.privateKey.toBase64().toStdString();
		const auto serialized = j.dump(2);
		file.write(serialized.data(), serialized.size());
		file.close();
	}
	return pair;
}

QByteArray GetMyPublicKey() {
	return GetOrGenerateIdentityKeyPair().publicKey;
}

QString Encrypt(
		const QString &text,
		int ttlSeconds,
		uint64 senderId,
		uint64 recipientId,
		const QByteArray &recipientPublicKey) {
	QByteArray aesKey(kKeySize, '\0');
	QByteArray packet;
	QByteArray aad;

	if (recipientPublicKey.size() == kP256X962Length) {
		RAND_bytes(reinterpret_cast<uint8_t*>(aesKey.data()), int(aesKey.size()));
		const auto ephemeral = GenerateKeyPair();
		if (ephemeral.publicKey.isEmpty() || ephemeral.privateKey.isEmpty()) {
			return {};
		}
		const auto myPubKey = GetMyPublicKey();

		const auto secretR = ComputeEcdh(ephemeral.privateKey, recipientPublicKey);
		if (!secretR) {
			return {};
		}
		QByteArray wrapKeyR(kKeySize, '\0');
		static constexpr auto kInfoR = "Whitegram-Wrap-Recipient";
		HkdfSha256(
			reinterpret_cast<uint8_t*>(wrapKeyR.data()),
			wrapKeyR.size(),
			reinterpret_cast<const uint8_t*>(secretR->constData()),
			secretR->size(),
			reinterpret_cast<const uint8_t*>(ephemeral.publicKey.constData()),
			ephemeral.publicKey.size(),
			reinterpret_cast<const uint8_t*>(kInfoR),
			std::strlen(kInfoR));

		QByteArray nonceR(kNonceSize, '\0');
		RAND_bytes(reinterpret_cast<uint8_t*>(nonceR.data()), int(nonceR.size()));
		QByteArray tagR;
		const auto encKeyR = AesGcmEncrypt(wrapKeyR, nonceR, aesKey, ephemeral.publicKey, tagR);
		SecureCleanse(wrapKeyR.data(), wrapKeyR.size());
		if (encKeyR.isEmpty()) {
			return {};
		}

		QByteArray nonceS(kNonceSize, '\0');
		QByteArray tagS;
		QByteArray encKeyS;
		if (myPubKey.size() == kP256X962Length) {
			const auto secretS = ComputeEcdh(ephemeral.privateKey, myPubKey);
			if (secretS) {
				QByteArray wrapKeyS(kKeySize, '\0');
				static constexpr auto kInfoS = "Whitegram-Wrap-Sender";
				HkdfSha256(
					reinterpret_cast<uint8_t*>(wrapKeyS.data()),
					wrapKeyS.size(),
					reinterpret_cast<const uint8_t*>(secretS->constData()),
					secretS->size(),
					reinterpret_cast<const uint8_t*>(ephemeral.publicKey.constData()),
					ephemeral.publicKey.size(),
					reinterpret_cast<const uint8_t*>(kInfoS),
					std::strlen(kInfoS));

				RAND_bytes(reinterpret_cast<uint8_t*>(nonceS.data()), int(nonceS.size()));
				encKeyS = AesGcmEncrypt(wrapKeyS, nonceS, aesKey, ephemeral.publicKey, tagS);
				SecureCleanse(wrapKeyS.data(), wrapKeyS.size());
			}
		}

		if (tagS.size() != kTagSize) {
			tagS = QByteArray(kTagSize, '\0');
		}
		if (encKeyS.size() != kKeySize) {
			encKeyS = QByteArray(kKeySize, '\0');
		}

		aad = ephemeral.publicKey;
		packet.append(char(4));
		packet.append(ephemeral.publicKey);
		packet.append(nonceR);
		packet.append(tagR);
		packet.append(encKeyR);
		packet.append(nonceS);
		packet.append(tagS);
		packet.append(encKeyS);
	} else {
		RAND_bytes(reinterpret_cast<uint8_t*>(aesKey.data()), int(aesKey.size()));
		packet.append(char(3));
		packet.append(aesKey);
		aad = QByteArray("Whitegram-Standalone-v3", 23);
	}

	QByteArray nonce(kNonceSize, '\0');
	RAND_bytes(reinterpret_cast<uint8_t*>(nonce.data()), int(nonce.size()));

	nlohmann::json container;
	container["t"] = text.toUtf8().toStdString();
	container["ttl"] = ttlSeconds;
	container["s"] = senderId;
	container["r"] = recipientId;
	container["ts"] = crl::now();

	const auto jsonDump = container.dump();
	const auto plaintext = QByteArray(jsonDump.data(), int(jsonDump.size()));

	QByteArray tag;
	const auto ciphertext = AesGcmEncrypt(aesKey, nonce, plaintext, aad, tag);
	SecureCleanse(aesKey.data(), aesKey.size());

	if (ciphertext.isEmpty()) {
		return {};
	}

	packet.append(nonce);
	packet.append(tag);
	packet.append(ciphertext);

	const auto zw = EncodeZeroWidth(packet);
	const auto placeholder = QString::fromUtf8(kPlaceholderText);
	const auto fullResult = placeholder + zw;

	MessageContent content;
	content.text = text;
	content.ttl = ttlSeconds;
	content.senderId = senderId;
	content.recipientId = recipientId;
	content.timestamp = crl::now();
	SaveLocalSentMessage(fullResult, content);

	return fullResult;
}

std::optional<MessageContent> Decrypt(
		const QString &encryptedPayload,
		const QByteArray &myPrivateKey) {
	const auto packetOpt = ExtractPayloadBytes(encryptedPayload);
	if (!packetOpt) {
		return std::nullopt;
	}

	const auto &packet = *packetOpt;
	if (packet.size() < 1) {
		return std::nullopt;
	}

	const auto version = uint8(packet[0]);
	QByteArray aesKey(kKeySize, '\0');
	QByteArray aad;
	auto offset = 1;

	if (version == 4) {
		constexpr auto kKeyBlockSize = kNonceSize + kTagSize + kKeySize;
		if (packet.size() < 1 + kP256X962Length + kKeyBlockSize * 2 + kNonceSize + kTagSize + 1) {
			return std::nullopt;
		}
		const auto ephPub = packet.mid(offset, kP256X962Length);
		offset += kP256X962Length;

		const auto nonceR = packet.mid(offset, kNonceSize);
		offset += kNonceSize;
		const auto tagR = packet.mid(offset, kTagSize);
		offset += kTagSize;
		const auto encKeyR = packet.mid(offset, kKeySize);
		offset += kKeySize;

		const auto nonceS = packet.mid(offset, kNonceSize);
		offset += kNonceSize;
		const auto tagS = packet.mid(offset, kTagSize);
		offset += kTagSize;
		const auto encKeyS = packet.mid(offset, kKeySize);
		offset += kKeySize;

		const auto sharedSecret = ComputeEcdh(myPrivateKey, ephPub);
		if (!sharedSecret) {
			return std::nullopt;
		}

		auto keyFound = false;

		{
			QByteArray wrapKeyR(kKeySize, '\0');
			static constexpr auto kInfoR = "Whitegram-Wrap-Recipient";
			HkdfSha256(
				reinterpret_cast<uint8_t*>(wrapKeyR.data()),
				wrapKeyR.size(),
				reinterpret_cast<const uint8_t*>(sharedSecret->constData()),
				sharedSecret->size(),
				reinterpret_cast<const uint8_t*>(ephPub.constData()),
				ephPub.size(),
				reinterpret_cast<const uint8_t*>(kInfoR),
				std::strlen(kInfoR));

			const auto decKey = AesGcmDecrypt(wrapKeyR, nonceR, encKeyR, tagR, ephPub);
			SecureCleanse(wrapKeyR.data(), wrapKeyR.size());
			if (decKey.has_value() && decKey->size() == kKeySize) {
				aesKey = *decKey;
				keyFound = true;
			}
		}

		if (!keyFound) {
			QByteArray wrapKeyS(kKeySize, '\0');
			static constexpr auto kInfoS = "Whitegram-Wrap-Sender";
			HkdfSha256(
				reinterpret_cast<uint8_t*>(wrapKeyS.data()),
				wrapKeyS.size(),
				reinterpret_cast<const uint8_t*>(sharedSecret->constData()),
				sharedSecret->size(),
				reinterpret_cast<const uint8_t*>(ephPub.constData()),
				ephPub.size(),
				reinterpret_cast<const uint8_t*>(kInfoS),
				std::strlen(kInfoS));

			const auto decKey = AesGcmDecrypt(wrapKeyS, nonceS, encKeyS, tagS, ephPub);
			SecureCleanse(wrapKeyS.data(), wrapKeyS.size());
			if (decKey.has_value() && decKey->size() == kKeySize) {
				aesKey = *decKey;
				keyFound = true;
			}
		}

		if (!keyFound) {
			return std::nullopt;
		}
		aad = ephPub;
	} else if (version == 1) {
		if (packet.size() < 1 + kP256X962Length + kNonceSize + kTagSize + 1) {
			return std::nullopt;
		}
		const auto ephPub = packet.mid(offset, kP256X962Length);
		offset += kP256X962Length;
		const auto sharedSecret = ComputeEcdh(myPrivateKey, ephPub);
		if (!sharedSecret) {
			return std::nullopt;
		}
		HkdfSha256(
			reinterpret_cast<uint8_t*>(aesKey.data()),
			aesKey.size(),
			reinterpret_cast<const uint8_t*>(sharedSecret->constData()),
			sharedSecret->size(),
			reinterpret_cast<const uint8_t*>(ephPub.constData()),
			ephPub.size(),
			reinterpret_cast<const uint8_t*>(kHkdfInfo),
			std::strlen(kHkdfInfo));
		aad = ephPub;
	} else if (version == 3) {
		if (packet.size() < 1 + kKeySize + kNonceSize + kTagSize + 1) {
			return std::nullopt;
		}
		aesKey = packet.mid(offset, kKeySize);
		offset += kKeySize;
		aad = QByteArray("Whitegram-Standalone-v3", 23);
	} else {
		return std::nullopt;
	}

	const auto nonce = packet.mid(offset, kNonceSize);
	offset += kNonceSize;
	const auto tag = packet.mid(offset, kTagSize);
	offset += kTagSize;
	const auto ciphertext = packet.mid(offset);

	const auto plaintext = AesGcmDecrypt(aesKey, nonce, ciphertext, tag, aad);
	SecureCleanse(aesKey.data(), aesKey.size());

	if (!plaintext) {
		return std::nullopt;
	}

	try {
		const auto j = nlohmann::json::parse(plaintext->constData());
		MessageContent content;
		content.text = QString::fromUtf8(j.value("t", "").c_str());
		content.ttl = j.value("ttl", 0);
		content.senderId = j.value("s", uint64(0));
		content.recipientId = j.value("r", uint64(0));
		content.timestamp = j.value("ts", crl::time(0));
		return content;
	} catch (...) {
		return std::nullopt;
	}
}

QString EncryptWithPasscode(
		const QString &text,
		int ttlSeconds,
		uint64 senderId,
		uint64 recipientId,
		const QString &passcode) {
	if (passcode.isEmpty()) {
		return {};
	}

	// 1. Generate 16-byte salt and 12-byte nonce
	QByteArray salt(16, '\0');
	RAND_bytes(reinterpret_cast<uint8_t*>(salt.data()), int(salt.size()));

	QByteArray nonce(kNonceSize, '\0');
	RAND_bytes(reinterpret_cast<uint8_t*>(nonce.data()), int(nonce.size()));

	// 2. Derive 32-byte key via PKCS5_PBKDF2_HMAC
	QByteArray aesKey(kKeySize, '\0');
	const auto passUtf8 = passcode.toUtf8();
	PKCS5_PBKDF2_HMAC(
		passUtf8.constData(),
		int(passUtf8.size()),
		reinterpret_cast<const uint8_t*>(salt.constData()),
		int(salt.size()),
		100000,
		EVP_sha256(),
		int(aesKey.size()),
		reinterpret_cast<uint8_t*>(aesKey.data()));

	// 3. Serialize container
	nlohmann::json container;
	container["t"] = text.toUtf8().toStdString();
	container["ttl"] = ttlSeconds;
	container["s"] = senderId;
	container["r"] = recipientId;
	container["ts"] = crl::now();

	const auto jsonDump = container.dump();
	const auto plaintext = QByteArray(jsonDump.data(), int(jsonDump.size()));

	// 4. Encrypt
	QByteArray tag;
	const auto ciphertext = AesGcmEncrypt(aesKey, nonce, plaintext, salt, tag);
	SecureCleanse(aesKey.data(), aesKey.size());

	if (ciphertext.isEmpty()) {
		return {};
	}

	// 5. Pack: [version: 1B = 2] [salt: 16B] [nonce: 12B] [tag: 16B] [ciphertext: NB]
	QByteArray packet;
	packet.append(char(2));
	packet.append(salt);
	packet.append(nonce);
	packet.append(tag);
	packet.append(ciphertext);

	const auto zw = EncodeZeroWidth(packet);
	const auto placeholder = QString::fromUtf8(kPlaceholderText);
	const auto b64 = QString::fromLatin1(packet.toBase64(QByteArray::Base64UrlEncoding));
	return placeholder
		+ zw
		+ QString::fromLatin1("\n")
		+ QString::fromLatin1(kPasscodePrefix)
		+ b64;
}

std::optional<MessageContent> DecryptWithPasscode(
		const QString &encryptedPayload,
		const QString &passcode) {
	const auto packetOpt = ExtractPayloadBytes(encryptedPayload);
	if (!packetOpt) {
		return std::nullopt;
	}

	const auto &packet = *packetOpt;
	// Min size: 1 (ver) + 16 (salt) + 12 (nonce) + 16 (tag) + 1 (cipher) = 46 bytes
	if (packet.size() < 46) {
		return std::nullopt;
	}
	if (packet[0] != char(2) && packet[0] != char(1)) {
		return std::nullopt;
	}

	auto offset = 1;
	const auto salt = packet.mid(offset, 16);
	offset += 16;
	const auto nonce = packet.mid(offset, kNonceSize);
	offset += kNonceSize;
	const auto tag = packet.mid(offset, kTagSize);
	offset += kTagSize;
	const auto ciphertext = packet.mid(offset);

	// Derive key
	QByteArray aesKey(kKeySize, '\0');
	const auto passUtf8 = passcode.toUtf8();
	PKCS5_PBKDF2_HMAC(
		passUtf8.constData(),
		int(passUtf8.size()),
		reinterpret_cast<const uint8_t*>(salt.constData()),
		int(salt.size()),
		100000,
		EVP_sha256(),
		int(aesKey.size()),
		reinterpret_cast<uint8_t*>(aesKey.data()));

	// Decrypt
	const auto plaintext = AesGcmDecrypt(aesKey, nonce, ciphertext, tag, salt);
	SecureCleanse(aesKey.data(), aesKey.size());

	if (!plaintext) {
		return std::nullopt;
	}

	try {
		const auto j = nlohmann::json::parse(plaintext->constData());
		MessageContent content;
		content.text = QString::fromUtf8(j.value("t", "").c_str());
		content.ttl = j.value("ttl", 0);
		content.senderId = j.value("s", uint64(0));
		content.recipientId = j.value("r", uint64(0));
		content.timestamp = j.value("ts", crl::time(0));
		return content;
	} catch (...) {
		return std::nullopt;
	}
}

std::optional<QByteArray> ExtractPayloadBytes(const QString &text) {
	if (text.contains(kZwStart)) {
		const auto zw = DecodeZeroWidth(text);
		if (zw.has_value() && !zw->isEmpty()) {
			return zw;
		}
	}
	const auto prefix = QString::fromLatin1(kPrefix);
	auto idx = text.indexOf(prefix);
	if (idx >= 0) {
		auto endIdx = text.indexOf('\n', idx);
		auto b64 = (endIdx >= 0)
			? text.mid(idx + prefix.length(), endIdx - (idx + prefix.length())).trimmed()
			: text.mid(idx + prefix.length()).trimmed();
		b64.remove(' ');
		b64.remove('\r');
		b64.remove('\n');
		b64.remove('\t');
		auto decoded = QByteArray::fromBase64(b64.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
		if (decoded.isEmpty()) {
			decoded = QByteArray::fromBase64(b64.toLatin1());
		}
		if (!decoded.isEmpty()) {
			return decoded;
		}
	}
	const auto passPrefix = QString::fromLatin1(kPasscodePrefix);
	idx = text.indexOf(passPrefix);
	if (idx >= 0) {
		auto endIdx = text.indexOf('\n', idx);
		auto b64 = (endIdx >= 0)
			? text.mid(idx + passPrefix.length(), endIdx - (idx + passPrefix.length())).trimmed()
			: text.mid(idx + passPrefix.length()).trimmed();
		b64.remove(' ');
		b64.remove('\r');
		b64.remove('\n');
		b64.remove('\t');
		auto decoded = QByteArray::fromBase64(b64.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
		if (decoded.isEmpty()) {
			decoded = QByteArray::fromBase64(b64.toLatin1());
		}
		if (!decoded.isEmpty()) {
			return decoded;
		}
	}
	return std::nullopt;
}

bool IsEncryptedMessage(const QString &text) {
	if (text.contains(QString::fromLatin1(kPrefix))
		|| text.contains(QString::fromLatin1(kPasscodePrefix))) {
		return true;
	}
	if (text.contains(kZwStart)) {
		return true;
	}
	const auto trimmed = text.trimmed();
	if (trimmed.startsWith(u"🔒 Одноразовое сообщение Whitegram"_q)) {
		return true;
	}
	return false;
}

bool IsPasscodeEncryptedMessage(const QString &text) {
	if (text.contains(QString::fromLatin1(kPasscodePrefix))) {
		return true;
	}
	const auto packet = ExtractPayloadBytes(text);
	if (packet && !packet->isEmpty()) {
		return uint8((*packet)[0]) == 2;
	}
	return false;
}

QString ExtractPayload(const QString &text) {
	const auto packet = ExtractPayloadBytes(text);
	if (!packet) {
		return QString();
	}
	return QString::fromLatin1(packet->toBase64(QByteArray::Base64UrlEncoding));
}

namespace {

base::flat_map<const HistoryItem*, QString> gPayloadsByItem;
base::flat_map<FullMsgId, QString> gPayloadsById;
base::flat_map<QString, MessageContent> gLocalSentMessages;

} // namespace

void SaveEncryptedPayload(not_null<const HistoryItem*> item, const QString &payload) {
	if (payload.trimmed().startsWith(u"🔒 Одноразовое сообщение\n👁️ Нажмите, чтобы просмотреть"_q)) {
		return;
	}
	if (!ExtractPayloadBytes(payload).has_value()) {
		return;
	}
	gPayloadsByItem[item.get()] = payload;
	const auto fullId = item->fullId();
	if (fullId.msg) {
		gPayloadsById[fullId] = payload;
	}
}

QString GetEncryptedPayload(not_null<const HistoryItem*> item) {
	const auto it = gPayloadsByItem.find(item.get());
	if (it != gPayloadsByItem.end() && !it->second.isEmpty()) {
		return it->second;
	}
	const auto fullId = item->fullId();
	if (fullId.msg) {
		const auto itId = gPayloadsById.find(fullId);
		if (itId != gPayloadsById.end()) {
			return itId->second;
		}
	}
	return QString();
}

void SaveLocalSentMessage(const QString &text, const MessageContent &content) {
	gLocalSentMessages[text] = content;
	gLocalSentMessages[text.trimmed()] = content;
	if (const auto bytes = ExtractPayloadBytes(text)) {
		gLocalSentMessages[QString::fromLatin1(bytes->toHex())] = content;
		gLocalSentMessages[QString::fromLatin1(bytes->toBase64())] = content;
		gLocalSentMessages[QString::fromLatin1(bytes->toBase64(QByteArray::Base64UrlEncoding))] = content;
	}
	const auto placeholder = QString::fromUtf8(kPlaceholderText);
	gLocalSentMessages[placeholder] = content;
	gLocalSentMessages[placeholder.trimmed()] = content;
}

std::optional<MessageContent> GetLocalSentMessage(const QString &text) {
	if (text.isEmpty()) {
		return std::nullopt;
	}
	const auto it = gLocalSentMessages.find(text);
	if (it != gLocalSentMessages.end()) {
		return it->second;
	}
	const auto itTrimmed = gLocalSentMessages.find(text.trimmed());
	if (itTrimmed != gLocalSentMessages.end()) {
		return itTrimmed->second;
	}
	if (const auto bytes = ExtractPayloadBytes(text)) {
		const auto hex = QString::fromLatin1(bytes->toHex());
		const auto itHex = gLocalSentMessages.find(hex);
		if (itHex != gLocalSentMessages.end()) {
			return itHex->second;
		}
		const auto b64 = QString::fromLatin1(bytes->toBase64(QByteArray::Base64UrlEncoding));
		const auto itB64 = gLocalSentMessages.find(b64);
		if (itB64 != gLocalSentMessages.end()) {
			return itB64->second;
		}
	}
	return std::nullopt;
}

bool IsWhitegramOneTimeMessage(not_null<const HistoryItem*> item) {
	const auto payload = GetEncryptedPayload(item);
	if (!payload.isEmpty()) {
		return true;
	}
	return IsEncryptedMessage(item->originalText().text);
}

} // namespace Whitegram::E2EE
