/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_ws_socket.h"

#include "base/invoke_queued.h"
#include "base/timer.h"
#include "logs.h"
#include "mtproto/connection_abstract.h"
#include "mtproto/details/mtproto_bypass_hosts.h"
#include "mtproto/details/mtproto_ws_socket.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtNetwork/QSslConfiguration>

#include <atomic>
#include <cstring>

namespace MTP::details {
namespace {

constexpr auto kOpcodeContinuation = quint8(0x0);
constexpr auto kOpcodeText = quint8(0x1);
constexpr auto kOpcodeBinary = quint8(0x2);
constexpr auto kOpcodeClose = quint8(0x8);
constexpr auto kOpcodePing = quint8(0x9);
constexpr auto kOpcodePong = quint8(0xA);
constexpr auto kMaxFramePayload = quint64(4 * 1024 * 1024);
constexpr auto kMaxUpgradeResponse = 16 * 1024;
constexpr auto kWsPort = 443;
constexpr auto kWsPlainPort = 80;
constexpr auto kPerHostTimeout = crl::time(3500);
constexpr auto kFrontedHostTimeout = crl::time(7000);
constexpr auto kFrontTcpTryTimeout = crl::time(1500);
constexpr auto kMaxBypassConnectBudget = crl::time(75 * 1000);
constexpr auto kFrontIp = "149.154.167.220";
constexpr auto kFrontSni = "sprinthost.ru";
constexpr auto kAcceptGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr auto kUserAgent =
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
	"AppleWebKit/537.36 (KHTML, like Gecko) "
	"Chrome/131.0.0.0 Safari/537.36";

// Remembers which way to the front IP worked last, like tg-ws-proxy does.
std::atomic<bool> FrontingFirst = true;
std::atomic<crl::time> FrontDownUntil = 0;
std::atomic<crl::time> FrontLastOk = 0;
constexpr auto kFrontDownCooldown = crl::time(2 * 60 * 1000);

// Remembers whether the mirrors answered over plain ws:// last time.
std::atomic<bool> MirrorPlainFirst = false;

[[nodiscard]] bool IsOfficialHost(const QString &host) {
	return host.endsWith(u".web.telegram.org"_q, Qt::CaseInsensitive);
}

} // namespace

WsSocket::WsSocket(
	not_null<QThread*> thread,
	const QNetworkProxy &proxy,
	bool protocolForFiles,
	int16 protocolDcId)
: AbstractSocket(thread)
, _protocolDcId(protocolDcId)
, _hostTimer([=] { onHostTimeout(); })
, _tcpTimer([=] { onTcpTimeout(); }) {
	_socket.moveToThread(thread);
	_socket.setProxy(proxy);
	_socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
	{
		// WHY: OpenSSL 3.5 adds an MLKEM key share, a 1.5 KB ClientHello
		// spans two TCP segments and DPI drops it; classic groups keep
		// the hello at ~330 bytes.
		auto config = _socket.sslConfiguration();
		config.setBackendConfigurationOption(
			QByteArrayLiteral("Groups"),
			QByteArrayLiteral("X25519:P-256:P-384"));
		_socket.setSslConfiguration(config);
	}
	if (protocolForFiles) {
		_socket.setSocketOption(
			QAbstractSocket::SendBufferSizeSocketOption,
			kFilesSendBufferSize);
		_socket.setSocketOption(
			QAbstractSocket::ReceiveBufferSizeSocketOption,
			kFilesReceiveBufferSize);
	}
	const auto wrap = [&](auto handler) {
		return [=](auto &&...args) {
			InvokeQueued(this, [=] { handler(args...); });
		};
	};
	using Error = QAbstractSocket::SocketError;
	connect(
		&_socket,
		&QAbstractSocket::connected,
		wrap([=] {
			_tcpTimer.cancel();
			if (_target.plain) {
				onEncrypted();
			}
		}));
	connect(
		&_socket,
		&QSslSocket::encrypted,
		wrap([=] { onEncrypted(); }));
	connect(
		&_socket,
		&QSslSocket::readyRead,
		wrap([=] { onReadyRead(); }));
	connect(
		&_socket,
		&QSslSocket::disconnected,
		wrap([=] {
			if (_state == State::Connected) {
				_disconnected.fire({});
			}
		}));
	connect(
		&_socket,
		&QAbstractSocket::errorOccurred,
		wrap([=](Error e) { handleError(e); }));
}

int WsSocket::extractDc() const {
	auto dc = (_protocolDcId < 0) ? (-_protocolDcId) : int(_protocolDcId);
	if (dc > kTestModeDcIdShift) {
		dc -= kTestModeDcIdShift;
	}
	if (dc == 203) {
		dc = 2;
	}
	if (dc < 1 || dc > 5) {
		dc = 2;
	}
	return dc;
}

bool WsSocket::isTestDc() const {
	const auto dc = (_protocolDcId < 0) ? (-_protocolDcId) : int(_protocolDcId);
	return dc > kTestModeDcIdShift;
}

bool WsSocket::isMediaDc() const {
	return _protocolDcId < 0;
}

QString WsSocket::pathForDc() const {
	return isTestDc() ? u"/apiws_test"_q : u"/apiws"_q;
}

std::vector<WsSocket::Target> WsSocket::buildTargets() const {
	const auto dc = extractDc();
	auto result = std::vector<Target>();
	auto front = std::vector<Target>();
	// WHY: DPI lets a TLS hello for a Russian domain through, and the
	// front IP still serves kws hosts of DC 2 and 4 behind it.
	if (!isTestDc() && (dc == 2 || dc == 4)) {
		const auto primary = u"kws%1.web.telegram.org"_q.arg(dc);
		const auto media = u"kws%1-1.web.telegram.org"_q.arg(dc);
		const auto names = isMediaDc()
			? std::vector<QString>{ media, primary }
			: std::vector<QString>{ primary };
		const auto frontedFirst = FrontingFirst.load();
		const auto ip = QString::fromLatin1(kFrontIp);
		for (const auto &name : names) {
			for (const auto fronted : { frontedFirst, !frontedFirst }) {
				front.push_back({
					.host = name,
					.address = ip,
					.sni = fronted ? QString::fromLatin1(kFrontSni) : name,
					.fronted = fronted,
				});
			}
		}
	}
	const auto hosts = ResolveBypassHosts({
		.dc = dc,
		.isMedia = isMediaDc(),
		.isTestDc = isTestDc(),
	});
	const auto plainFirst = MirrorPlainFirst.load();
	for (const auto &host : hosts) {
		if (IsOfficialHost(host)) {
			result.push_back({ .host = host, .address = host, .sni = host });
			continue;
		}
		// WHY: since 16.11.2026 mirror SNI is blocked, plain ws:// on port 80 passes.
		for (const auto plain : { plainFirst, !plainFirst }) {
			result.push_back({
				.host = host,
				.address = host,
				.sni = host,
				.plain = plain,
			});
		}
	}
	const auto frontDown = (crl::now() < FrontDownUntil.load());
	result.insert(
		frontDown ? end(result) : begin(result),
		begin(front),
		end(front));
	return result;
}

void WsSocket::connectToHost(const QString &address, int port) {
	(void)address;
	(void)port;
	if (_state != State::Idle) {
		return;
	}
	_hosts = buildTargets();
	_hostIndex = 0;
	auto budget = crl::time(0);
	for (const auto &target : _hosts) {
		budget += (target.address != target.host)
			? kFrontedHostTimeout
			: kPerHostTimeout;
	}
	_connectBudget = std::clamp(
		budget,
		kPerHostTimeout,
		kMaxBypassConnectBudget);
	startConnectToCurrentHost();
}

crl::time WsSocket::connectBudget() const {
	return _connectBudget;
}

void WsSocket::onHostTimeout() {
	if (_state == State::Failed || _state == State::Connected) {
		return;
	}
	LOG(("Bypass WS: timeout on %1 (%2/%3), switching host"
		).arg(debugTarget()
		).arg(_hostIndex + 1
		).arg(int(_hosts.size())));
	_hostTimer.cancel();
	_tcpTimer.cancel();
	_socket.abort();
	tryNextHost();
}

QString WsSocket::debugTarget() const {
	return (_target.address == _target.host)
		? (_target.plain ? (_target.host + u":80"_q) : _target.host)
		: u"%1 via %2 sni %3"_q.arg(
			_target.host,
			_target.address,
			_target.sni);
}

void WsSocket::startConnectToCurrentHost() {
	if (_hostIndex < 0 || _hostIndex >= int(_hosts.size())) {
		fail();
		return;
	}
	_target = _hosts[_hostIndex];
	_host = _target.host;
	_incoming.clear();
	_appData.clear();
	_secKey.clear();
	_state = State::ConnectingSsl;
	LOG(("Bypass WS: connecting %1 (%2/%3, dc path %4)"
		).arg(debugTarget()
		).arg(_hostIndex + 1
		).arg(int(_hosts.size())
		).arg(pathForDc()));
	const auto direct = (_target.address != _target.host);
	const auto timeout = direct ? kFrontedHostTimeout : kPerHostTimeout;
	_targetDeadline = crl::now() + timeout;
	_hostTimer.callOnce(timeout);
	connectCurrentTarget();
}

void WsSocket::connectCurrentTarget() {
	// WHY: the network drops about every third SYN to the front IP while a
	// good connect takes under half a second, so retry quickly instead.
	if (_target.address != _target.host) {
		_tcpTimer.callOnce(kFrontTcpTryTimeout);
	}
	if (_target.plain) {
		_socket.connectToHost(
			_target.address,
			kWsPlainPort,
			QIODevice::ReadWrite,
			QAbstractSocket::IPv4Protocol);
		return;
	}
	// WHY: a fronted hello names another domain, so the certificate can't
	// match; MTProto itself authenticates the server.
	_socket.setPeerVerifyMode(_target.fronted
		? QSslSocket::VerifyNone
		: QSslSocket::VerifyPeer);
	_socket.setPeerVerifyName(_target.sni);
	_socket.connectToHostEncrypted(
		_target.address,
		kWsPort,
		_target.sni,
		QIODevice::ReadWrite,
		QAbstractSocket::IPv4Protocol);
}

void WsSocket::onTcpTimeout() {
	if (_state != State::ConnectingSsl
		|| _socket.state() == QAbstractSocket::ConnectedState) {
		return;
	}
	if (_targetDeadline - crl::now() < crl::time(300)) {
		return;
	}
	LOG(("Bypass WS: no TCP answer from %1, retrying").arg(debugTarget()));
	_socket.abort();
	connectCurrentTarget();
}

void WsSocket::resetSocketForRetry() {
	_tcpTimer.cancel();
	_hostTimer.cancel();
	_state = State::Idle;
	_socket.abort();
	_incoming.clear();
	_appData.clear();
	_secKey.clear();
}

void WsSocket::tryNextHost() {
	if (_state == State::Failed || _state == State::Connected) {
		return;
	}
	const auto isFront = [](const Target &target) {
		return (target.address != target.host);
	};
	if (isFront(_target)) {
		const auto next = _hostIndex + 1;
		const auto lastFront = (next >= int(_hosts.size()))
			|| !isFront(_hosts[next]);
		const auto now = crl::now();
		if (lastFront && now - FrontLastOk.load() > kFrontDownCooldown) {
			FrontDownUntil = now + kFrontDownCooldown;
		}
	} else if (!_host.isEmpty()) {
		// A mirror is bad only once both its TLS and plain tries failed.
		const auto next = _hostIndex + 1;
		if (next >= int(_hosts.size()) || _hosts[next].host != _host) {
			NotifyBypassHostResult(_host, false);
		}
	}
	++_hostIndex;
	if (_hostIndex >= int(_hosts.size())) {
		LOG(("Bypass WS: all %1 hosts failed").arg(int(_hosts.size())));
		fail();
		return;
	}
	resetSocketForRetry();
	startConnectToCurrentHost();
}

void WsSocket::onEncrypted() {
	if (_state != State::ConnectingSsl) {
		return;
	}
	_state = State::WaitingUpgrade;
	sendUpgrade();
}

void WsSocket::sendUpgrade() {
	auto keyBytes = QByteArray(16, Qt::Uninitialized);
	auto *rng = QRandomGenerator::global();
	for (auto i = 0; i != keyBytes.size(); ++i) {
		keyBytes[i] = char(rng->bounded(256));
	}
	_secKey = keyBytes.toBase64();

	auto request = QByteArray();
	request += "GET " + pathForDc().toLatin1() + " HTTP/1.1\r\n";
	request += "Host: " + _host.toLatin1() + "\r\n";
	request += "Upgrade: websocket\r\n";
	request += "Connection: Upgrade\r\n";
	request += "Sec-WebSocket-Key: " + _secKey + "\r\n";
	request += "Sec-WebSocket-Version: 13\r\n";
	request += "Sec-WebSocket-Protocol: binary\r\n";
	request += "Origin: https://web.telegram.org\r\n";
	request += "User-Agent: " + QByteArray(kUserAgent) + "\r\n";
	request += "\r\n";
	_socket.write(request);
}

bool WsSocket::consumeUpgradeResponse() {
	const auto end = _incoming.indexOf("\r\n\r\n");
	if (end < 0) {
		return (_incoming.size() < kMaxUpgradeResponse);
	}
	const auto head = _incoming.left(end);
	const auto lines = head.split('\n');
	const auto status = lines.isEmpty()
		? QByteArray()
		: lines.front().trimmed();
	if (!status.contains(" 101")) {
		LOG(("Bypass WS: bad upgrade status for %1: %2").arg(
			debugTarget(),
			QString::fromLatin1(status)));
		return false;
	}
	const auto expected = QCryptographicHash::hash(
		_secKey + QByteArray::fromRawData(kAcceptGuid, int(strlen(kAcceptGuid))),
		QCryptographicHash::Sha1).toBase64();
	auto accepted = false;
	for (const auto &line : lines) {
		const auto colon = line.indexOf(':');
		if (colon < 0) {
			continue;
		}
		if (line.left(colon).trimmed().toLower() != "sec-websocket-accept") {
			continue;
		}
		accepted = (line.mid(colon + 1).trimmed() == expected);
		break;
	}
	if (!accepted) {
		return false;
	}
	_incoming.remove(0, end + 4);
	_state = State::Connected;
	_hostTimer.cancel();
	LOG(("Bypass WS: connected to %1").arg(debugTarget()));
	NotifyBypassConnected();
	if (_target.address == _target.host) {
		NotifyBypassHostResult(_host, true);
		if (!IsOfficialHost(_host)) {
			MirrorPlainFirst = _target.plain;
		}
	} else {
		FrontingFirst = _target.fronted;
		FrontDownUntil = 0;
		FrontLastOk = crl::now();
	}
	_connected.fire({});
	return true;
}

void WsSocket::onReadyRead() {
	if (_state == State::WaitingUpgrade) {
		_incoming.append(_socket.readAll());
		if (!consumeUpgradeResponse()) {
			tryNextHost();
			return;
		} else if (_state != State::Connected) {
			return;
		}
	} else if (_state == State::Connected) {
		_incoming.append(_socket.readAll());
	} else {
		return;
	}
	if (!parseFrames()) {
		fail();
		return;
	}
	if (!_appData.isEmpty()) {
		_readyRead.fire({});
	}
}

bool WsSocket::parseFrames() {
	auto offset = 0;
	const auto size = _incoming.size();
	const auto data = reinterpret_cast<const uchar*>(_incoming.constData());
	while (true) {
		if (size - offset < 2) {
			break;
		}
		const auto first = data[offset];
		const auto second = data[offset + 1];
		const auto opcode = quint8(first & 0x0F);
		const auto masked = ((second & 0x80) != 0);
		auto length = quint64(second & 0x7F);
		auto header = 2;
		if (length == 126) {
			if (size - offset < 4) {
				break;
			}
			length = (quint64(data[offset + 2]) << 8)
				| quint64(data[offset + 3]);
			header = 4;
		} else if (length == 127) {
			if (size - offset < 10) {
				break;
			}
			length = 0;
			for (auto i = 0; i != 8; ++i) {
				length = (length << 8) | quint64(data[offset + 2 + i]);
			}
			header = 10;
		}
		if (length > kMaxFramePayload) {
			return false;
		}
		if (masked) {
			header += 4;
		}
		if (quint64(size - offset) < quint64(header) + length) {
			break;
		}
		auto payload = QByteArray(
			_incoming.constData() + offset + header,
			int(length));
		if (masked) {
			const auto mask = data + offset + header - 4;
			for (auto i = 0; i != int(length); ++i) {
				payload[i] = char(uchar(payload[i]) ^ mask[i % 4]);
			}
		}
		offset += header + int(length);

		switch (opcode) {
		case kOpcodeContinuation:
		case kOpcodeBinary:
		case kOpcodeText:
			if (!payload.isEmpty()) {
				_appData.append(payload);
			}
			break;
		case kOpcodeClose:
			return false;
		case kOpcodePing:
			writeFrame(
				bytes::make_span(
					reinterpret_cast<const bytes::type*>(payload.constData()),
					payload.size()),
				kOpcodePong);
			break;
		case kOpcodePong:
			break;
		default:
			return false;
		}
	}
	if (offset > 0) {
		_incoming.remove(0, offset);
	}
	return true;
}

void WsSocket::writeFrame(bytes::const_span payload, quint8 opcode) {
	const auto length = quint64(payload.size());
	auto frame = QByteArray();
	frame.reserve(int(length) + 14);
	frame.append(char(0x80 | opcode));
	if (length < 126) {
		frame.append(char(0x80 | char(length)));
	} else if (length <= 0xFFFF) {
		frame.append(char(0x80 | 126));
		frame.append(char((length >> 8) & 0xFF));
		frame.append(char(length & 0xFF));
	} else {
		frame.append(char(0x80 | 127));
		for (auto i = 7; i >= 0; --i) {
			frame.append(char((length >> (i * 8)) & 0xFF));
		}
	}
	auto maskBytes = QByteArray(4, Qt::Uninitialized);
	auto *rng = QRandomGenerator::global();
	for (auto i = 0; i != 4; ++i) {
		maskBytes[i] = char(rng->bounded(256));
	}
	frame.append(maskBytes);
	const auto from = reinterpret_cast<const char*>(payload.data());
	for (auto i = 0; i != int(length); ++i) {
		frame.append(char(from[i] ^ maskBytes[i % 4]));
	}
	_socket.write(frame);
}

bool WsSocket::isGoodStartNonce(bytes::const_span nonce) {
	Expects(nonce.size() >= 2 * sizeof(uint32));

	const auto bytes = nonce.data();
	const auto zero = *reinterpret_cast<const uchar*>(bytes);
	const auto first = *reinterpret_cast<const uint32*>(bytes);
	const auto second = *(reinterpret_cast<const uint32*>(bytes) + 1);
	const auto reserved01 = 0x000000EFU;
	const auto reserved11 = 0x44414548U;
	const auto reserved12 = 0x54534F50U;
	const auto reserved13 = 0x20544547U;
	const auto reserved14 = 0xEEEEEEEEU;
	const auto reserved15 = 0xDDDDDDDDU;
	const auto reserved16 = 0x02010316U;
	const auto reserved21 = 0x00000000U;
	return (zero != reserved01)
		&& (first != reserved11)
		&& (first != reserved12)
		&& (first != reserved13)
		&& (first != reserved14)
		&& (first != reserved15)
		&& (first != reserved16)
		&& (second != reserved21);
}

void WsSocket::timedOut() {
	if (_state == State::Failed || _state == State::Connected) {
		return;
	}
	onHostTimeout();
}

bool WsSocket::isConnected() {
	return (_state == State::Connected)
		&& (_socket.state() == QAbstractSocket::ConnectedState);
}

bool WsSocket::hasBytesAvailable() {
	return !_appData.isEmpty();
}

int64 WsSocket::read(bytes::span buffer) {
	const auto size = std::min(int64(buffer.size()), int64(_appData.size()));
	if (size <= 0) {
		return 0;
	}
	memcpy(buffer.data(), _appData.constData(), size);
	_appData.remove(0, int(size));
	return size;
}

void WsSocket::write(bytes::const_span prefix, bytes::const_span buffer) {
	Expects(!buffer.empty());

	if (_state != State::Connected) {
		return;
	}
	if (prefix.empty()) {
		writeFrame(buffer, kOpcodeBinary);
		return;
	}
	auto combined = bytes::vector(prefix.size() + buffer.size());
	bytes::copy(bytes::make_span(combined).subspan(0, prefix.size()), prefix);
	bytes::copy(
		bytes::make_span(combined).subspan(prefix.size()),
		buffer);
	writeFrame(bytes::make_span(combined), kOpcodeBinary);
}

int32 WsSocket::debugState() {
	return int32(_socket.state());
}

QString WsSocket::debugPostfix() const {
	return u" (wss:%1)"_q.arg(_host.isEmpty()
		? u"pending"_q
		: _host);
}

void WsSocket::handleError(int errorCode) {
	if (_state == State::Idle || _state == State::Failed) {
		return;
	}
	if (_state == State::Connected) {
		logError(errorCode, _socket.errorString());
		LOG(("Bypass WS: error on %1 after connect: %2").arg(
			debugTarget(),
			_socket.errorString()));
		fail();
		return;
	}
	LOG(("Bypass WS: error on %1: %2").arg(
		debugTarget(),
		_socket.errorString()));
	tryNextHost();
}

void WsSocket::fail() {
	if (_state == State::Failed) {
		return;
	}
	_hostTimer.cancel();
	_tcpTimer.cancel();
	const auto wasConnected = (_state == State::Connected);
	LOG(("Bypass WS: failed after %1/%2 hosts").arg(
		std::min(_hostIndex + 1, int(_hosts.size()))
	).arg(int(_hosts.size())));
	_state = State::Failed;
	// WHY: a drop of a working tunnel is not a sign that bypass is useless,
	// falling back to blocked TCP for minutes would only cut the user off.
	if (!wasConnected) {
		NotifyBypassHostsExhausted();
	}
	_error.fire({});
}

} // namespace MTP::details
