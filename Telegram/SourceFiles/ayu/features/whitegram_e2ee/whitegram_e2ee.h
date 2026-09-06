// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <optional>

class HistoryItem;

namespace Whitegram::E2EE {

inline constexpr auto kPrefix = "wg1:";
inline constexpr auto kPasscodePrefix = "wgp1:";
inline constexpr auto kPlaceholderText =
	"🔒 Одноразовое сообщение Whitegram\n"
	"Это сообщение защищено сквозным E2E-шифрованием и доступно для просмотра только в клиенте Whitegram.\n";

struct KeyPair {
	QByteArray publicKey;  // 65 bytes uncompressed or 33 bytes compressed X9.62
	QByteArray privateKey; // 32 bytes raw private key
};

struct MessageContent {
	QString text;
	int ttl = 0; // 0 = Single view, >0 = seconds, -1 = never (persistent E2EE)
	uint64 senderId = 0;
	uint64 recipientId = 0;
	crl::time timestamp = 0;
};

// Key generation and management
[[nodiscard]] KeyPair GenerateKeyPair();
[[nodiscard]] KeyPair GetOrGenerateIdentityKeyPair();
[[nodiscard]] QByteArray GetMyPublicKey();

// Core Encryption & Decryption
[[nodiscard]] QString Encrypt(
	const QString &text,
	int ttlSeconds,
	uint64 senderId,
	uint64 recipientId,
	const QByteArray &recipientPublicKey);

[[nodiscard]] std::optional<MessageContent> Decrypt(
	const QString &encryptedPayload,
	const QByteArray &myPrivateKey);

// Passcode-based fallback encryption
[[nodiscard]] QString EncryptWithPasscode(
	const QString &text,
	int ttlSeconds,
	uint64 senderId,
	uint64 recipientId,
	const QString &passcode);

[[nodiscard]] std::optional<MessageContent> DecryptWithPasscode(
	const QString &encryptedPayload,
	const QString &passcode);

// Detection helpers
[[nodiscard]] bool IsEncryptedMessage(const QString &text);
[[nodiscard]] bool IsPasscodeEncryptedMessage(const QString &text);
[[nodiscard]] QString ExtractPayload(const QString &text);
[[nodiscard]] std::optional<QByteArray> ExtractPayloadBytes(const QString &text);
[[nodiscard]] bool IsWhitegramOneTimeMessage(not_null<const HistoryItem*> item);

void SaveEncryptedPayload(not_null<const HistoryItem*> item, const QString &payload);
[[nodiscard]] QString GetEncryptedPayload(not_null<const HistoryItem*> item);

void SaveLocalSentMessage(const QString &text, const MessageContent &content);
[[nodiscard]] std::optional<MessageContent> GetLocalSentMessage(const QString &text);

// Memory wiping utility
void SecureCleanse(void *ptr, size_t len);
void SecureCleanseString(QString &str);
void SecureCleanseByteArray(QByteArray &bytes);

} // namespace Whitegram::E2EE
