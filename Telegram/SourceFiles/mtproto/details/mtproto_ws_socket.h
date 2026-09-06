/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_abstract_socket.h"

#include "base/timer.h"

#include <QtNetwork/QSslSocket>

namespace MTP::details {

class WsSocket final : public AbstractSocket {
public:
	WsSocket(
		not_null<QThread*> thread,
		const QNetworkProxy &proxy,
		bool protocolForFiles,
		int16 protocolDcId);

	void connectToHost(const QString &address, int port) override;
	bool isGoodStartNonce(bytes::const_span nonce) override;
	void timedOut() override;
	bool isConnected() override;
	bool hasBytesAvailable() override;
	int64 read(bytes::span buffer) override;
	void write(bytes::const_span prefix, bytes::const_span buffer) override;

	int32 debugState() override;
	QString debugPostfix() const override;

private:
	enum class State {
		Idle,
		ConnectingSsl,
		WaitingUpgrade,
		Connected,
		Failed,
	};

	void handleError(int errorCode);
	void onEncrypted();
	void onReadyRead();
	void sendUpgrade();
	[[nodiscard]] bool consumeUpgradeResponse();
	[[nodiscard]] bool parseFrames();
	void writeFrame(bytes::const_span payload, quint8 opcode);
	void fail();
	void tryNextHost();
	void startConnectToCurrentHost();
	void resetSocketForRetry();
	void onHostTimeout();
	[[nodiscard]] std::vector<QString> buildHosts() const;
	[[nodiscard]] crl::time connectBudget() const override;
	[[nodiscard]] int extractDc() const;
	[[nodiscard]] bool isTestDc() const;
	[[nodiscard]] bool isMediaDc() const;
	[[nodiscard]] QString pathForDc() const;

	const int16 _protocolDcId = 0;
	State _state = State::Idle;
	QSslSocket _socket;
	QByteArray _incoming;
	QByteArray _appData;
	QByteArray _secKey;
	QString _host;
	std::vector<QString> _hosts;
	int _hostIndex = 0;
	crl::time _connectBudget = 0;
	base::Timer _hostTimer;

};

} // namespace MTP::details
