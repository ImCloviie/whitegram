// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "base/timer.h"
#include "rpl/variable.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Whitegram {

struct CoinPrice {
	QString symbol;
	QString name;
	double priceUsd = 0.0;
	double priceEur = 0.0;
	double priceRub = 0.0;
	double change24h = 0.0;

	friend inline bool operator==(const CoinPrice &a, const CoinPrice &b) {
		return a.symbol == b.symbol
			&& a.name == b.name
			&& a.priceUsd == b.priceUsd
			&& a.priceEur == b.priceEur
			&& a.priceRub == b.priceRub
			&& a.change24h == b.change24h;
	}
	friend inline bool operator!=(const CoinPrice &a, const CoinPrice &b) {
		return !(a == b);
	}
};

class CryptoProvider final {
public:
	static CryptoProvider &instance();

	void start();
	void stop();
	void refresh();

	[[nodiscard]] rpl::producer<std::vector<CoinPrice>> pricesValue() const;
	[[nodiscard]] const std::vector<CoinPrice> &currentPrices() const;

	[[nodiscard]] static QString formatPrice(double price, int fiatIndex);
	[[nodiscard]] static QString fiatSymbol(int fiatIndex);

private:
	CryptoProvider();
	~CryptoProvider();

	void fetchPrices();
	void parseResponse(const QByteArray &data);

	QNetworkAccessManager _manager;
	base::Timer _timer;
	rpl::variable<std::vector<CoinPrice>> _prices;
	bool _running = false;
};

} // namespace Whitegram
