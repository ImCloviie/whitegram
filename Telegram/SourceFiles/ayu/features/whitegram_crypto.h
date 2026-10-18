// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "ayu/features/whitegram_crypto_charts.h"
#include "base/basic_types.h"
#include "base/timer.h"
#include "rpl/variable.h"

#include <QtCore/QString>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

#include <utility>
#include <vector>

namespace Whitegram {

struct CoinPrice {
	QString id;
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

// A single historical point of a price chart.
using ChartPoint = ChartEntry;

// Price history of one asset (coin or fiat), in USD.
struct PriceHistory {
	QString id;       // CoinGecko coin id or fiat code ("USD", "EUR", "RUB").
	QString symbol;    // "BTC", "ETH", "USD", ...
	QString name;      // "Bitcoin", "Gram", ...
	bool fiat = false; // True for plain currencies.
	std::vector<ChartPoint> points;

	[[nodiscard]] bool empty() const {
		return points.empty();
	}
	[[nodiscard]] double first() const {
		return points.empty() ? 0.0 : points.front().price;
	}
	[[nodiscard]] double last() const {
		return points.empty() ? 0.0 : points.back().price;
	}

	friend inline bool operator==(
			const PriceHistory &a,
			const PriceHistory &b) {
		return a.id == b.id
			&& a.symbol == b.symbol
			&& a.fiat == b.fiat
			&& a.points.size() == b.points.size();
	}
	friend inline bool operator!=(
			const PriceHistory &a,
			const PriceHistory &b) {
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

	// Requests `days` days of history for a coin id ("bitcoin", ...) and
	// calls back with points in USD and a success flag; on failure the
	// points are empty and ok == false. The callback runs on the Qt event
	// loop (main thread).
	void requestCoinHistory(
		const QString &coinId,
		int days,
		Fn<void(std::vector<ChartPoint>, bool)> done);

	// Requests `days` days of history for a fiat code ("USD", "EUR",
	// "RUB") as USD per 1 unit of that currency. "USD" always returns a
	// flat line at 1.0. Same callback contract as above.
	void requestFiatHistory(
		const QString &fiatCode,
		int days,
		Fn<void(std::vector<ChartPoint>, bool)> done);

	[[nodiscard]] static QString formatPrice(double price, int fiatIndex);
	[[nodiscard]] static QString fiatSymbol(int fiatIndex);
	[[nodiscard]] static double coinPrice(
		const CoinPrice &coin,
		int fiatIndex);

	// Fetches 7 days of history for every asset in `coinIds` (CoinGecko
	// ids) plus the fiat currencies enabled in settings, and updates the
	// histories producer that feeds the sidebar sparklines. Repeated calls
	// with an unchanged set do not re-fetch; a 5 minute timer refreshes.
	void refreshHistories(const std::vector<QString> &coinIds);
	[[nodiscard]] rpl::producer<
		std::vector<PriceHistory>> historiesValue() const;
	[[nodiscard]] const std::vector<PriceHistory> &currentHistories() const;

private:
	CryptoProvider();
	~CryptoProvider();

	void fetchPrices();
	void parseResponse(const QByteArray &data);
	void setHistory(PriceHistory history);
	// One batched request that carries 7-day hourly sparklines for every
	// coin at once (replaces per-coin market_chart calls that were
	// tripping the free CoinGecko rate limit).
	void fetchMarketsSparklines();
	void parseMarketsResponse(const QByteArray &data);
	[[nodiscard]] std::vector<ChartPoint> parseChart(
		const QByteArray &data,
		bool invert) const;
	void refetchWantedHistories();
	[[nodiscard]] static std::vector<ChartPoint> FlatHistory(int days);

	QNetworkAccessManager _manager;
	base::Timer _timer;
	base::Timer _historyTimer;
	rpl::variable<std::vector<CoinPrice>> _prices;
	rpl::variable<std::vector<PriceHistory>> _histories;
	bool _running = false;
	bool _historiesRunning = false;

	std::vector<std::pair<QString, bool>> _wantedHistory;
	int _historyDays = 0;

};

} // namespace Whitegram
