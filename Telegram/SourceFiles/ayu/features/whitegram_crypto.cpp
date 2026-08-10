// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_crypto.h"

#include "ayu/ayu_settings.h"
#include "ayu/libs/json.hpp"

#include <QUrl>
#include <QtNetwork/QNetworkRequest>

namespace Whitegram {

namespace {

constexpr auto kUpdateIntervalMs = crl::time(60000);

constexpr auto kApiUrl =
	"https://api.coingecko.com/api/v3/simple/price"
	"?ids=bitcoin,ethereum,the-open-network,solana,binancecoin"
	"&vs_currencies=usd,eur,rub"
	"&include_24hr_change=true";

struct CoinDef {
	const char *id;
	const char *symbol;
	const char *name;
};

const auto kCoins = std::vector<CoinDef>{
	{ "bitcoin", "BTC", "Bitcoin" },
	{ "ethereum", "ETH", "Ethereum" },
	{ "the-open-network", "TON", "Toncoin" },
	{ "solana", "SOL", "Solana" },
	{ "binancecoin", "BNB", "BNB" },
};

QString FormatNumberWithSpaces(double price, int precision) {
	auto str = QString::number(price, 'f', precision);
	auto dotIndex = str.indexOf('.');
	if (dotIndex == -1) {
		dotIndex = str.length();
	}
	for (auto i = dotIndex - 3; i > (str.startsWith('-') ? 1 : 0); i -= 3) {
		str.insert(i, u" "_q);
	}
	return str;
}

} // namespace

CryptoProvider &CryptoProvider::instance() {
	static CryptoProvider provider;
	return provider;
}

CryptoProvider::CryptoProvider()
: _timer([=] { fetchPrices(); }) {
}

CryptoProvider::~CryptoProvider() {
	stop();
}

void CryptoProvider::start() {
	if (_running) {
		return;
	}
	_running = true;
	fetchPrices();
	_timer.callEach(kUpdateIntervalMs);
}

void CryptoProvider::stop() {
	_running = false;
	_timer.cancel();
}

void CryptoProvider::refresh() {
	fetchPrices();
}

rpl::producer<std::vector<CoinPrice>> CryptoProvider::pricesValue() const {
	return _prices.value();
}

const std::vector<CoinPrice> &CryptoProvider::currentPrices() const {
	return _prices.current();
}

QString CryptoProvider::formatPrice(double price, int fiatIndex) {
	const auto sym = fiatSymbol(fiatIndex);
	if (price >= 1.0) {
		return FormatNumberWithSpaces(price, 2) + u" "_q + sym;
	} else if (price >= 0.01) {
		return FormatNumberWithSpaces(price, 4) + u" "_q + sym;
	}
	return FormatNumberWithSpaces(price, 6) + u" "_q + sym;
}

QString CryptoProvider::fiatSymbol(int fiatIndex) {
	switch (fiatIndex) {
	case 1: return QString::fromUtf8("€");
	case 2: return QString::fromUtf8("₽");
	default: return QString::fromUtf8("$");
	}
}

void CryptoProvider::fetchPrices() {
	auto request = QNetworkRequest(QUrl(QString::fromUtf8(kApiUrl)));
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		QByteArray("WhitegramDesktop/1.0"));

	auto reply = _manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (reply->error() == QNetworkReply::NoError) {
			parseResponse(reply->readAll());
		}
		reply->deleteLater();
	});
}

void CryptoProvider::parseResponse(const QByteArray &data) {
	try {
		const auto j = nlohmann::json::parse(data.toStdString());
		auto result = std::vector<CoinPrice>();
		result.reserve(kCoins.size());

		for (const auto &coin : kCoins) {
			if (!j.contains(coin.id)) {
				continue;
			}
			const auto &entry = j[coin.id];
			auto price = CoinPrice{
				.symbol = QString::fromUtf8(coin.symbol),
				.name = QString::fromUtf8(coin.name),
				.priceUsd = entry.value("usd", 0.0),
				.priceEur = entry.value("eur", 0.0),
				.priceRub = entry.value("rub", 0.0),
				.change24h = entry.value("usd_24h_change", 0.0),
			};
			result.push_back(std::move(price));
		}

		_prices = std::move(result);
	} catch (...) {
	}
}

} // namespace Whitegram
