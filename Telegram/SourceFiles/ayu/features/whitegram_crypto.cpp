// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_crypto.h"

#include "ayu/ayu_settings.h"
#include "ayu/libs/json.hpp"
#include "base/basic_types.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QTimer>
#include <QUrl>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
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

// Minimum spacing between chart-history requests: rapid range
// switching fires a burst that trips the free CoinGecko rate limit
// within a few clicks, manifesting as a 5-10+ second "cooldown".
constexpr auto kChartMinIntervalMs = crl::time(8000);

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
//
// jsdelivr can fail (blocked network, momentary 5xx or rate limit), so
// unpkg mirrors the same files and is the fallback host. The file for
// the *current* day is published with a delay and answers 404 until it
// lands; the "latest" tag always resolves to the newest published day
// and is the last fallback for today's point.
constexpr auto kFiatCdnPrimary =
	"https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@";
constexpr auto kFiatCdnFallback =
	"https://unpkg.com/@fawazahmed0/currency-api@";

[[nodiscard]] QString FiatDayUrl(
		const char *base,
		const QString &tag,
		const QString &lowerCode) {
	return QString::fromLatin1(base)
		+ tag
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
, _historyTimer([=] { refetchWantedHistories(); })
, _rateLimitTimer([=] {
	const auto left = _rateLimitSecondsLeft.current();
	if (left > 1) {
		_rateLimitSecondsLeft = left - 1;
	} else {
		_rateLimitSecondsLeft = 0;
		_rateLimitTimer.cancel();
	}
})
, _chartCooldownTimer([=] { sendQueuedChartRequest(); }) {
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
	if (price >= 100000.0) {
		// Whole units only: the drawer rows are narrow, and with large
		// prices (BTC in rubles is seven digits before the dot) the
		// ".00" tail pushes the 24h change off the visible line.
		return FormatNumberWithSpaces(price, 0) + u" "_q + sym;
	} else if (price >= 1.0) {
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

		// Merge current-price / 24h-change data into the prices list:
		// when /simple/price is rate-limited, this batch (which is
		// fetched anyway for the sparklines) keeps the percentage
		// changes fresh instead of them silently disappearing.
		auto prices = _prices.current();
		auto pricesChanged = false;
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

			// Null-safe reads: /coins/markets answers null for missing
			// fields and .value() with a double default throws on null.
			const auto readDouble = [](const nlohmann::json &entry,
					const char *key) -> double {
				if (!entry.contains(key) || !entry[key].is_number()) {
					return 0.0;
				}
				return entry[key].get<double>();
			};
			const auto currentPrice = readDouble(coin, "current_price");
			const auto change = readDouble(
				coin,
				"price_change_percentage_24h");
			if (currentPrice > 0.0) {
				auto found = false;
				for (auto &existing : prices) {
					if (existing.id == id) {
						if (existing.priceUsd != currentPrice
							|| existing.change24h != change) {
							existing.priceUsd = currentPrice;
							existing.change24h = change;
							pricesChanged = true;
						}
						found = true;
						break;
					}
				}
				if (!found) {
					// The markets batch is authoritative for USD price
					// and 24h change; add the coin if /simple/price has
					// not delivered it yet (e.g. right after startup or
					// while it is rate-limited).
					prices.push_back(CoinPrice{
						.id = id,
						.symbol = symbol,
						.name = name,
						.priceUsd = currentPrice,
						.priceEur = 0.0,
						.priceRub = 0.0,
						.change24h = change,
					});
					pricesChanged = true;
				}
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
		if (pricesChanged) {
			// Publish the merged prices so the drawer rows refresh with
			// fresh 24h changes even when /simple/price is rate-limited.
			_prices = std::move(prices);
		}
	} catch (...) {
	}
}

void CryptoProvider::parseResponse(const QByteArray &data) {
	try {
		const auto j = nlohmann::json::parse(data.toStdString());

		// Null-safe field reader: /simple/price answers "null" for
		// missing fields, and a plain .value() with a double default
		// throws on null, silently killing the whole parse.
		const auto readDouble = [](const nlohmann::json &entry,
				const char *key) -> std::optional<double> {
			if (!entry.contains(key) || !entry[key].is_number()) {
				return std::nullopt;
			}
			return entry[key].get<double>();
		};

		auto previous = _prices.current();
		auto result = std::vector<CoinPrice>();
		result.reserve(kCoins.size());

		for (const auto &coin : kCoins) {
			if (!j.contains(coin.id) || !j[coin.id].is_object()) {
				continue;
			}
			const auto &entry = j[coin.id];
			auto price = CoinPrice{
				.id = QString::fromLatin1(coin.id),
				.symbol = QString::fromLatin1(coin.symbol),
				.name = QString::fromLatin1(coin.name),
				.priceUsd = readDouble(entry, "usd").value_or(0.0),
				.priceEur = readDouble(entry, "eur").value_or(0.0),
				.priceRub = readDouble(entry, "rub").value_or(0.0),
				.change24h = readDouble(
					entry,
					"usd_24h_change").value_or(0.0),
			};
			for (const auto &existing : previous) {
				if (existing.id != price.id) {
					continue;
				}
				// A partial (rate-limited) reply with missing or null
				// fields must not wipe what the markets batch or the
				// previous reply already delivered.
				if (price.change24h == 0.0
					&& existing.change24h != 0.0) {
					price.change24h = existing.change24h;
				}
				if (price.priceEur == 0.0 && existing.priceEur != 0.0) {
					price.priceEur = existing.priceEur;
				}
				if (price.priceRub == 0.0 && existing.priceRub != 0.0) {
					price.priceRub = existing.priceRub;
				}
				break;
			}
			result.push_back(std::move(price));
		}

		// Coins missing from this reply entirely keep their previous
		// entry instead of disappearing from the drawer.
		for (const auto &existing : previous) {
			auto found = false;
			for (const auto &price : result) {
				if (price.id == existing.id) {
					found = true;
					break;
				}
			}
			if (!found) {
				result.push_back(existing);
			}
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

void CryptoProvider::queueChartRequest(Fn<void()> request) {
	// Replace whatever was queued: rapid range switching must never
	// multiply requests, only the user's latest choice matters.
	_queuedChartRequest = std::move(request);
	if (_chartRequestInFlight) {
		return;
	}
	_chartCooldownTimer.callOnce(0);
}

void CryptoProvider::sendQueuedChartRequest() {
	if (_chartRequestInFlight || !_queuedChartRequest) {
		return;
	}
	_chartRequestInFlight = true;
	auto request = std::exchange(_queuedChartRequest, Fn<void()>());
	request();
}

void CryptoProvider::chartRequestFinished() {
	_chartRequestInFlight = false;
	if (_queuedChartRequest) {
		// Space consecutive requests out: bursts are what trips the
		// free CoinGecko rate limit after a few range switches.
		_chartCooldownTimer.callOnce(kChartMinIntervalMs);
	}
}

void CryptoProvider::setRateLimitSecondsLeft(int seconds) {
	if (seconds <= 0) {
		_rateLimitSecondsLeft = 0;
		_rateLimitTimer.cancel();
		return;
	}
	_rateLimitSecondsLeft = seconds;
	if (!_rateLimitTimer.isActive()) {
		_rateLimitTimer.callEach(1000);
	}
}

rpl::producer<int> CryptoProvider::rateLimitSecondsLeftValue() const {
	return _rateLimitSecondsLeft.value();
}

void CryptoProvider::requestCoinHistory(
		const QString &coinId,
		int days,
		Fn<void(std::vector<ChartPoint>, bool)> done) {
	queueChartRequest([=] {
		attemptCoinHistory(coinId, days, done, 1);
	});
}

void CryptoProvider::attemptCoinHistory(
		const QString &coinId,
		int days,
		Fn<void(std::vector<ChartPoint>, bool)> done,
		int attempt) {
	auto request = QNetworkRequest(QUrl(CoinHistoryUrl(coinId, days)));
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		QByteArray("WhitegramDesktop/1.0"));

	auto reply = _manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError) {
			const auto points = parseChart(reply->readAll(), false);
			setRateLimitSecondsLeft(0);
			chartRequestFinished();
			done(points, !points.empty());
		} else if (status == 429 || status == 403) {
			// Rate limited: surface the wait to the UI and retry with a
			// growing delay instead of failing straight away.
			// attempt: 1 -> 5s, 2 -> 10s, 3 -> 20s, 4 -> 40s, 5 -> 60s.
			const auto delayMs = int(std::min(
				5000 * (1 << (attempt - 1)),
				60000));
			setRateLimitSecondsLeft(delayMs / 1000);
			if (attempt < 6) {
				QTimer::singleShot(delayMs, [=] {
					attemptCoinHistory(coinId, days, done, attempt + 1);
				});
			} else {
				chartRequestFinished();
				done({}, false);
			}
		} else {
			chartRequestFinished();
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
	// per date (with CDN-host and "latest" fallbacks), merged when all
	// replies arrive.
	struct MergeState {
		std::map<QDate, double> values;
		int pending = 0;
	};
	const auto state = std::make_shared<MergeState>();
	const auto dates = RecentDates(days);
	state->pending = int(dates.size());

	for (const auto &date : dates) {
		fetchFiatDay(lower, date, 0, [=](std::optional<double> value) {
			if (value && *value > 0.0) {
				state->values[date] = *value;
			}
			if (--state->pending <= 0) {
				auto points = std::vector<ChartPoint>();
				points.reserve(state->values.size());
				for (const auto &[day, val] : state->values) {
					points.push_back(ChartPoint{
						.timestamp = QDateTime(day).toMSecsSinceEpoch(),
						.price = val,
					});
				}
				// At least two days of data make a drawable line.
				done(std::move(points), points.size() >= 2);
			}
		});
	}
}

void CryptoProvider::fetchFiatDay(
		const QString &lower,
		const QDate &date,
		int attempt,
		Fn<void(std::optional<double>)> done) {
	// attempt 0: primary CDN, dated file.
	// attempt 1: unpkg mirror, dated file.
	// attempt 2: primary CDN, "latest" tag (today only: the dated file
	//            for the current day 404s until it is published).
	// attempt 3: unpkg mirror, "latest" tag (today only).
	const auto today = QDate::currentDate();
	const auto dated = date.toString(u"yyyy-MM-dd"_q);
	const char *base = nullptr;
	auto tag = QString();
	switch (attempt) {
	case 0: base = kFiatCdnPrimary; tag = dated; break;
	case 1: base = kFiatCdnFallback; tag = dated; break;
	case 2:
	case 3:
		if (date != today) {
			// "latest" is only meaningful for today's point; for past
			// days there is nothing left to try.
			done(std::nullopt);
			return;
		}
		base = (attempt == 2) ? kFiatCdnPrimary : kFiatCdnFallback;
		tag = u"latest"_q;
		break;
	default:
		done(std::nullopt);
		return;
	}

	auto request = QNetworkRequest(QUrl(FiatDayUrl(base, tag, lower)));
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		QByteArray("WhitegramDesktop/1.0"));

	auto reply = _manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto failed = reply->error() != QNetworkReply::NoError;
		const auto data = failed ? QByteArray() : reply->readAll();
		reply->deleteLater();

		auto value = std::optional<double>();
		if (!failed) {
			try {
				const auto j = nlohmann::json::parse(data.toStdString());
				const auto key = lower.toStdString();
				if (j.contains(key)
					&& j[key].contains("usd")
					&& j[key]["usd"].is_number()) {
					value = j[key]["usd"].get<double>();
				}
			} catch (...) {
			}
		}
		if (value) {
			done(value);
		} else {
			// Failed or unparsable: try the next host/tag in the chain.
			fetchFiatDay(lower, date, attempt + 1, done);
		}
	});
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
