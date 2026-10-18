// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_crypto.h"

#include "ayu/ayu_settings.h"
#include "ayu/libs/json.hpp"
#include "base/basic_types.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QUrl>
#include <QtNetwork/QNetworkRequest>

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace Whitegram {

namespace {

constexpr auto kUpdateIntervalMs = crl::time(60000);
// History spans 7 days and only feeds the small sparkline charts, so it is
// refreshed at a slower pace.
constexpr auto kHistoryIntervalMs = crl::time(5 * 60000);

constexpr auto kApiBaseUrl = "https://api.coingecko.com/api/v3";

constexpr auto kSparklineDays = 7;

struct CoinDef {
	const char *id;
	const char *symbol;
	const char *name;
};

const auto kCoins = std::vector<CoinDef>{
	{ "bitcoin", "BTC", "Bitcoin" },
	{ "ethereum", "ETH", "Ethereum" },
	{ "the-open-network", "GRAM", "Gram" },
	{ "solana", "SOL", "Solana" },
	{ "binancecoin", "BNB", "BNB" },
};

struct FiatDef {
	const char *code; // lowercase, for URLs
	const char *symbol;
	const char *name;
};

const auto kFiats = std::vector<FiatDef>{
	{ "usd", "USD", "US Dollar" },
	{ "eur", "EUR", "Euro" },
	{ "rub", "RUB", "Russian Ruble" },
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

[[nodiscard]] QString SimplePriceUrl() {
	auto ids = QStringList();
	ids.reserve(kCoins.size());
	for (const auto &coin : kCoins) {
		ids.push_back(QString::fromLatin1(coin.id));
	}
	return QString::fromLatin1(kApiBaseUrl)
		+ u"/simple/price?ids="_q
		+ ids.join(QString(','))
		+ u"&vs_currencies=usd,eur,rub&include_24hr_change=true"_q;
}

// One batched call that carries current prices AND 7-day hourly
// sparklines for every coin at once. The free CoinGecko tier limits
// requests, and per-coin market_chart calls were tripping 429s as soon
// as more coins were enabled; this endpoint is one request regardless
// of how many coins are shown.
[[nodiscard]] QString MarketsUrl() {
	auto ids = QStringList();
	ids.reserve(kCoins.size());
	for (const auto &coin : kCoins) {
		ids.push_back(QString::fromLatin1(coin.id));
	}
	return QString::fromLatin1(kApiBaseUrl)
		+ u"/coins/markets?vs_currency=usd&ids="_q
		+ ids.join(QString(','))
		+ u"&sparkline=true&price_change_percentage=24h"_q;
}

[[nodiscard]] QString CoinHistoryUrl(const QString &coinId, int days) {
	return QString::fromLatin1(kApiBaseUrl)
		+ u"/coins/"_q
		+ coinId
		+ u"/market_chart?vs_currency=usd&days="_q
		+ QString::number(days);
}

// Fiat history: the free fawazahmed0 currency-api publishes one file per
// date; each lists "<base currency> -> everything" rates. Fetching the
// file of each day in the range and reading the "usd" field yields the
// daily USD-per-1-unit history the sparkline needs.
constexpr auto kFiatHistoryApiBase =
	"https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@";

[[nodiscard]] QString FiatDayUrl(
		const QString &lowerCode,
		const QDate &date) {
	return QString::fromLatin1(kFiatHistoryApiBase)
		+ date.toString(u"yyyy-MM-dd"_q)
		+ u"/v1/currencies/"_q
		+ lowerCode
		+ u".json"_q;
}

[[nodiscard]] std::vector<QDate> RecentDates(int days) {
	auto result = std::vector<QDate>();
	const auto today = QDate::currentDate();
	result.reserve(days + 1);
	for (auto i = days; i != 0; --i) {
		result.push_back(today.addDays(-i));
	}
	result.push_back(today);
	return result;
}

[[nodiscard]] QString AssetNameForId(const QString &id) {
	for (const auto &coin : kCoins) {
		if (id == QLatin1String(coin.id)) {
			return QString::fromLatin1(coin.name);
		}
	}
	for (const auto &fiat : kFiats) {
		if (id == QLatin1String(fiat.code)) {
			return QString::fromLatin1(fiat.name);
		}
	}
	return QString();
}

[[nodiscard]] QString AssetSymbolForId(const QString &id) {
	for (const auto &coin : kCoins) {
		if (id == QLatin1String(coin.id)) {
			return QString::fromLatin1(coin.symbol);
		}
	}
	for (const auto &fiat : kFiats) {
		if (id == QLatin1String(fiat.code)) {
			return QString::fromLatin1(fiat.symbol);
		}
	}
	return QString();
}

} // namespace

CryptoProvider &CryptoProvider::instance() {
	static CryptoProvider provider;
	return provider;
}

CryptoProvider::CryptoProvider()
: _timer([=] { fetchPrices(); })
, _historyTimer([=] { refetchWantedHistories(); }) {
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
	_historiesRunning = false;
	_timer.cancel();
	_historyTimer.cancel();
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

double CryptoProvider::coinPrice(const CoinPrice &coin, int fiatIndex) {
	switch (fiatIndex) {
	case 1: return coin.priceEur;
	case 2: return coin.priceRub;
	default: return coin.priceUsd;
	}
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
	auto request = QNetworkRequest(QUrl(SimplePriceUrl()));
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		QByteArray("WhitegramDesktop/1.0"));

	auto reply = _manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (reply->error() == QNetworkReply::NoError) {
			parseResponse(reply->readAll());
		}
		// A failed reply keeps the previous data: an error must never
		// clear the list (that made the whole drawer section vanish when
		// the API rate-limited us after enabling more coins).
		reply->deleteLater();
	});
}

void CryptoProvider::fetchMarketsSparklines() {
	auto request = QNetworkRequest(QUrl(MarketsUrl()));
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		QByteArray("WhitegramDesktop/1.0"));

	auto reply = _manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (reply->error() == QNetworkReply::NoError) {
			parseMarketsResponse(reply->readAll());
		}
		reply->deleteLater();
	});
}

void CryptoProvider::parseMarketsResponse(const QByteArray &data) {
	if (!_historiesRunning) {
		return;
	}
	try {
		const auto j = nlohmann::json::parse(data.toStdString());
		if (!j.is_array()) {
			return;
		}

		// The sparkline is hourly over 7 days ending at the request
		// moment; rebuild timestamps backwards from now.
		const auto now = QDateTime::currentMSecsSinceEpoch();
		for (const auto &coin : j) {
			if (!coin.is_object() || !coin.contains("id")) {
				continue;
			}
			const auto id = QString::fromStdString(
				coin.value("id", std::string()));
			const auto symbol = AssetSymbolForId(id);
			const auto name = AssetNameForId(id);
			if (symbol.isEmpty()) {
				// Not one of our coins.
				continue;
			}
			if (!coin.contains("sparkline_in_7d")
				|| !coin["sparkline_in_7d"].is_object()
				|| !coin["sparkline_in_7d"].contains("price")
				|| !coin["sparkline_in_7d"]["price"].is_array()) {
				continue;
			}
			const auto &spark =
				coin["sparkline_in_7d"]["price"];
			auto points = std::vector<ChartPoint>();
			points.reserve(spark.size());
			const auto count = int(spark.size());
			for (auto i = 0; i != count; ++i) {
				if (!spark[i].is_number()) {
					continue;
				}
				points.push_back(ChartPoint{
					.timestamp = now
						- qint64(count - 1 - i) * 3600LL * 1000LL,
					.price = spark[i].get<double>(),
				});
			}
			setHistory(PriceHistory{
				.id = id,
				.symbol = symbol,
				.name = name,
				.fiat = false,
				.points = std::move(points),
			});
		}
	} catch (...) {
	}
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
				.id = QString::fromLatin1(coin.id),
				.symbol = QString::fromLatin1(coin.symbol),
				.name = QString::fromLatin1(coin.name),
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

std::vector<ChartPoint> CryptoProvider::parseChart(
		const QByteArray &data,
		bool invert) const {
	auto result = std::vector<ChartPoint>();
	try {
		const auto j = nlohmann::json::parse(data.toStdString());
		if (!j.contains("prices") || !j["prices"].is_array()) {
			return result;
		}
		const auto &prices = j["prices"];
		result.reserve(prices.size());
		for (const auto &point : prices) {
			if (!point.is_array() || point.size() < 2) {
				continue;
			}
			const auto time = point[0].is_number()
				? point[0].get<qint64>()
				: qint64(0);
			auto price = point[1].is_number()
				? point[1].get<double>()
				: 0.0;
			if (invert && price > 0.0) {
				price = 1.0 / price;
			}
			result.push_back(ChartPoint{
				.timestamp = time,
				.price = price,
			});
		}
	} catch (...) {
	}
	return result;
}

std::vector<ChartPoint> CryptoProvider::FlatHistory(int days) {
	auto result = std::vector<ChartPoint>();
	const auto now = QDateTime::currentMSecsSinceEpoch();
	const auto step = qint64(3600 * 1000); // 1 hour
	const auto count = days * 24;
	result.reserve(count);
	for (auto i = count; i != 0; --i) {
		result.push_back(ChartPoint{
			.timestamp = now - i * step,
			.price = 1.0,
		});
	}
	return result;
}

void CryptoProvider::setHistory(PriceHistory history) {
	auto current = _histories.current();
	auto replaced = false;
	for (auto &existing : current) {
		if (existing.id == history.id) {
			existing = std::move(history);
			replaced = true;
			break;
		}
	}
	if (!replaced) {
		current.push_back(std::move(history));
	}
	_histories = std::move(current);
}

rpl::producer<std::vector<PriceHistory>> CryptoProvider::historiesValue()
const {
	return _histories.value();
}

const std::vector<PriceHistory> &CryptoProvider::currentHistories() const {
	return _histories.current();
}

void CryptoProvider::requestCoinHistory(
		const QString &coinId,
		int days,
		Fn<void(std::vector<ChartPoint>, bool)> done) {
	auto request = QNetworkRequest(QUrl(CoinHistoryUrl(coinId, days)));
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		QByteArray("WhitegramDesktop/1.0"));

	auto reply = _manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (reply->error() == QNetworkReply::NoError) {
			const auto points = parseChart(reply->readAll(), false);
			done(points, !points.empty());
		} else {
			done({}, false);
		}
		reply->deleteLater();
	});
}

void CryptoProvider::requestFiatHistory(
		const QString &fiatCode,
		int days,
		Fn<void(std::vector<ChartPoint>, bool)> done) {
	const auto lower = fiatCode.toLower();
	if (lower == u"usd"_q) {
		// USD per 1 USD is always 1.
		done(FlatHistory(days), true);
		return;
	}

	// Daily fiat history from the fawazahmed0 currency-api: one request
	// per date, merged when all replies arrive.
	struct MergeState {
		std::map<QDate, double> values;
		int pending = 0;
	};
	const auto state = std::make_shared<MergeState>();
	const auto dates = RecentDates(days);
	state->pending = int(dates.size());

	for (const auto &date : dates) {
		auto request = QNetworkRequest(QUrl(FiatDayUrl(lower, date)));
		request.setHeader(
			QNetworkRequest::UserAgentHeader,
			QByteArray("WhitegramDesktop/1.0"));

		auto reply = _manager.get(request);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			if (reply->error() == QNetworkReply::NoError) {
				try {
					const auto j = nlohmann::json::parse(
						reply->readAll().toStdString());
					const auto key = lower.toStdString();
					if (j.contains(key)
						&& j[key].contains("usd")
						&& j[key]["usd"].is_number()) {
						state->values[date] =
							j[key]["usd"].get<double>();
					}
				} catch (...) {
				}
			}
			reply->deleteLater();
			if (--state->pending <= 0) {
				auto points = std::vector<ChartPoint>();
				points.reserve(state->values.size());
				for (const auto &[day, value] : state->values) {
					points.push_back(ChartPoint{
						.timestamp = QDateTime(day).toMSecsSinceEpoch(),
						.price = value,
					});
				}
				// At least two days of data make a drawable line.
				done(std::move(points), points.size() >= 2);
			}
		});
	}
}

void CryptoProvider::refetchWantedHistories() {
	if (!_historiesRunning || _wantedHistory.empty()) {
		return;
	}
	const auto days = (_historyDays > 0) ? _historyDays : kSparklineDays;

	// All coins in one batched /coins/markets request: it returns 7-day
	// sparklines for every coin at once, so the number of shown coins no
	// longer multiplies the request count (the free CoinGecko tier
	// rate-limits per-coin market_chart calls).
	auto anyCoins = false;
	for (const auto &[id, isFiat] : _wantedHistory) {
		if (!isFiat) {
			anyCoins = true;
			break;
		}
	}
	if (anyCoins) {
		fetchMarketsSparklines();
	}

	for (const auto &[id, isFiat] : _wantedHistory) {
		if (!isFiat) {
			continue;
		}
		const auto symbol = AssetSymbolForId(id);
		const auto name = AssetNameForId(id);
		if (id == u"usd"_q) {
			// A flat line at 1.0: USD per 1 USD.
			setHistory(PriceHistory{
				.id = id,
				.symbol = symbol,
				.name = name,
				.fiat = true,
				.points = FlatHistory(days),
			});
			continue;
		}
		// EUR/RUB: daily history from the currency-api, written into
		// _histories as soon as all day replies arrive. Errors are
		// ignored here: the row just keeps whatever data it had.
		requestFiatHistory(id, days, [=](
				std::vector<ChartPoint> points,
				bool ok) {
			if (!ok) {
				return;
			}
			setHistory(PriceHistory{
				.id = id,
				.symbol = symbol,
				.name = name,
				.fiat = true,
				.points = std::move(points),
			});
		});
	}
}

void CryptoProvider::refreshHistories(const std::vector<QString> &coinIds) {
	const auto &s = AyuSettings::getInstance();
	const auto historyDays = s.cryptoChartDays();

	// Build the list of (id, isFiat) pairs the sparklines need.
	auto wanted = std::vector<std::pair<QString, bool>>();
	wanted.reserve(coinIds.size() + 3);
	for (const auto &id : coinIds) {
		wanted.emplace_back(id, false);
	}
	if (s.showFiatUSD()) {
		wanted.emplace_back(u"usd"_q, true);
	}
	if (s.showFiatEUR()) {
		wanted.emplace_back(u"eur"_q, true);
	}
	if (s.showFiatRUB()) {
		wanted.emplace_back(u"rub"_q, true);
	}

	// Nothing changed since the last request: keep the current data.
	if (wanted == _wantedHistory
		&& historyDays == _historyDays
		&& _historiesRunning) {
		return;
	}
	_wantedHistory = wanted;
	_historyDays = historyDays;
	_historiesRunning = true;
	if (!_historyTimer.isActive()) {
		_historyTimer.callEach(kHistoryIntervalMs);
	}
	refetchWantedHistories();
}

} // namespace Whitegram
