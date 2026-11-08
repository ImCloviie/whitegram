// This file is part of Whitegram for Desktop.
#include "ayu/ui/utils/weather_engine.h"

#include "ayu/ui/utils/net_fetch.h"
#include "logs.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>

#include <cmath>
#include <memory>

namespace Ayu::Ui::Weather {
namespace {

constexpr auto kTimeoutMs = 6000;
constexpr auto kHours = 6;

QJsonDocument FetchDocument(const QUrl &url) {
	return QJsonDocument::fromJson(Net::FetchSync({
		.url = url,
		.userAgent = u"WhitegramDesktop/1.0 github.com/ImCloviie/Whitegram"_q,
		.headers = { { "Accept", "application/json" } },
		.timeoutMs = kTimeoutMs,
	}));
}

QJsonObject GetJson(const QUrl &url) {
	return FetchDocument(url).object();
}

int WmoFromWwo(int code) {
	switch (code) {
	case 113: return 0;
	case 116: return 2;
	case 119:
	case 122: return 3;
	case 143:
	case 248:
	case 260: return 45;
	case 263:
	case 266: return 51;
	case 281:
	case 284: return 56;
	case 176:
	case 293:
	case 296: return 61;
	case 299:
	case 302: return 63;
	case 305:
	case 308: return 65;
	case 182:
	case 185:
	case 311:
	case 314:
	case 317:
	case 350:
	case 362:
	case 365:
	case 374:
	case 377: return 66;
	case 179:
	case 227:
	case 320:
	case 323:
	case 326: return 71;
	case 329:
	case 332: return 73;
	case 230:
	case 335:
	case 338: return 75;
	case 353: return 80;
	case 356: return 81;
	case 359: return 82;
	case 368: return 85;
	case 371: return 86;
	case 200:
	case 386:
	case 389:
	case 392:
	case 395: return 95;
	}
	return 3;
}

int IntOf(const QJsonValue &value) {
	return value.isString() ? value.toString().toInt() : value.toInt();
}

struct Place {
	bool ok = false;
	double latitude = 0.;
	double longitude = 0.;
	QString name;
};

Place ByCity(const QString &city) {
	auto url = QUrl(u"https://geocoding-api.open-meteo.com/v1/search"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"name"_q, city);
	query.addQueryItem(u"count"_q, u"1"_q);
	query.addQueryItem(u"language"_q, u"ru"_q);
	query.addQueryItem(u"format"_q, u"json"_q);
	url.setQuery(query);
	const auto results = GetJson(url).value(u"results"_q).toArray();
	if (results.isEmpty()) {
		return {};
	}
	const auto first = results.first().toObject();
	return {
		.ok = true,
		.latitude = first.value(u"latitude"_q).toDouble(),
		.longitude = first.value(u"longitude"_q).toDouble(),
		.name = first.value(u"name"_q).toString(),
	};
}

Place ByIp() {
	const auto who = GetJson(QUrl(u"https://ipwho.is/?fields=success,city,latitude,longitude&lang=ru"_q));
	if (who.value(u"success"_q).toBool()) {
		return {
			.ok = true,
			.latitude = who.value(u"latitude"_q).toDouble(),
			.longitude = who.value(u"longitude"_q).toDouble(),
			.name = who.value(u"city"_q).toString(),
		};
	}
	const auto fallback = GetJson(QUrl(u"https://ipapi.co/json/"_q));
	if (fallback.contains(u"latitude"_q)) {
		return {
			.ok = true,
			.latitude = fallback.value(u"latitude"_q).toDouble(),
			.longitude = fallback.value(u"longitude"_q).toDouble(),
			.name = fallback.value(u"city"_q).toString(),
		};
	}
	return {};
}

int WmoFromSymbol(const QString &symbol) {
	const auto base = symbol.section(QChar(u'_'), 0, 0);
	const auto heavy = base.startsWith(u"heavy"_q);
	const auto light = base.startsWith(u"light"_q);
	if (base.contains(u"thunder"_q)) {
		return 95;
	} else if (base.contains(u"sleet"_q)) {
		return 66;
	} else if (base.contains(u"snow"_q)) {
		return heavy ? 75 : light ? 71 : 73;
	} else if (base.contains(u"rain"_q)) {
		return base.contains(u"showers"_q) ? (heavy ? 82 : 80) : (heavy ? 65 : light ? 61 : 63);
	} else if (base == u"fog"_q) {
		return 45;
	} else if (base == u"cloudy"_q) {
		return 3;
	} else if (base == u"partlycloudy"_q) {
		return 2;
	} else if (base == u"fair"_q) {
		return 1;
	}
	return 0;
}

double FeelsLike(double temperature, double windMs) {
	const auto kmh = windMs * 3.6;
	if (temperature > 10. || kmh < 4.8) {
		return temperature;
	}
	const auto v = std::pow(kmh, 0.16);
	return 13.12 + 0.6215 * temperature - 11.37 * v + 0.3965 * temperature * v;
}

Data FetchMetNo(const Place &place, const QString &city) {
	auto url = QUrl(u"https://api.met.no/weatherapi/locationforecast/2.0/compact"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"lat"_q, QString::number(place.latitude, 'f', 4));
	query.addQueryItem(u"lon"_q, QString::number(place.longitude, 'f', 4));
	url.setQuery(query);
	const auto series = GetJson(url)
		.value(u"properties"_q).toObject()
		.value(u"timeseries"_q).toArray();
	if (series.isEmpty()) {
		LOG(("Karaoke weather: met.no fallback failed"));
		return {};
	}
	const auto now = QDateTime::currentDateTimeUtc();
	auto result = Data();
	for (const auto &entry : series) {
		const auto object = entry.toObject();
		const auto time = QDateTime::fromString(object.value(u"time"_q).toString(), Qt::ISODate);
		if (time.isValid() && time.addSecs(3600) < now) {
			continue;
		}
		const auto data = object.value(u"data"_q).toObject();
		const auto details = data.value(u"instant"_q).toObject().value(u"details"_q).toObject();
		const auto symbol = data.value(u"next_1_hours"_q).toObject()
			.value(u"summary"_q).toObject()
			.value(u"symbol_code"_q).toString();
		const auto temperature = details.value(u"air_temperature"_q).toDouble();
		const auto code = WmoFromSymbol(symbol);
		if (!result.ok) {
			const auto hour = time.toLocalTime().time().hour();
			result.ok = true;
			result.fromIp = city.isEmpty();
			result.city = place.name.isEmpty() ? city : place.name;
			result.temperature = temperature;
			result.feels = FeelsLike(temperature, details.value(u"wind_speed"_q).toDouble());
			result.code = code;
			result.humidity = int(std::lround(details.value(u"relative_humidity"_q).toDouble()));
			result.day = symbol.endsWith(u"_night"_q)
				? false
				: symbol.endsWith(u"_day"_q) ? true : (hour >= 7 && hour < 19);
		} else if (int(result.hours.size()) < kHours) {
			result.hours.push_back({
				.hour = time.toLocalTime().time().hour(),
				.temperature = temperature,
				.code = code,
			});
		} else {
			break;
		}
	}
	LOG(("Karaoke weather: got %1 from met.no").arg(FormatDegrees(result.temperature)));
	return result;
}

Data FetchWttr(const Place &place, const QString &city) {
	auto url = QUrl(u"https://wttr.in/%1,%2"_q.arg(
		QString::number(place.latitude, 'f', 4),
		QString::number(place.longitude, 'f', 4)));
	url.setQuery(u"format=j1&lang=ru"_q);
	const auto json = GetJson(url);
	const auto current = json.value(u"current_condition"_q).toArray().first().toObject();
	if (current.isEmpty()) {
		LOG(("Karaoke weather: wttr.in fallback failed too"));
		return {};
	}
	auto result = Data();
	result.ok = true;
	result.fromIp = city.isEmpty();
	result.city = place.name.isEmpty() ? city : place.name;
	result.temperature = IntOf(current.value(u"temp_C"_q));
	result.feels = IntOf(current.value(u"FeelsLikeC"_q));
	result.code = WmoFromWwo(IntOf(current.value(u"weatherCode"_q)));
	result.humidity = IntOf(current.value(u"humidity"_q));
	const auto observed = current.value(u"localObsDateTime"_q).toString();
	auto hourNow = QTime::currentTime().hour();
	const auto stamp = QDateTime::fromString(observed, u"yyyy-MM-dd hh:mm AP"_q);
	if (stamp.isValid()) {
		hourNow = stamp.time().hour();
	}
	result.day = (hourNow >= 7 && hourNow < 19);
	const auto days = json.value(u"weather"_q).toArray();
	for (auto d = 0; d < days.size() && int(result.hours.size()) < kHours; ++d) {
		const auto hourly = days[d].toObject().value(u"hourly"_q).toArray();
		for (const auto &entry : hourly) {
			const auto object = entry.toObject();
			const auto hour = IntOf(object.value(u"time"_q)) / 100;
			if (d == 0 && hour <= hourNow) {
				continue;
			} else if (int(result.hours.size()) >= kHours) {
				break;
			}
			result.hours.push_back({
				.hour = hour,
				.temperature = double(IntOf(object.value(u"tempC"_q))),
				.code = WmoFromWwo(IntOf(object.value(u"weatherCode"_q))),
			});
		}
	}
	LOG(("Karaoke weather: got %1 from wttr.in").arg(FormatDegrees(result.temperature)));
	return result;
}

} // namespace

Data FetchSync(const QString &city) {
	auto result = Data();
	const auto trimmed = city.trimmed();
	auto place = trimmed.isEmpty() ? ByIp() : ByCity(trimmed);
	if (!place.ok) {
		LOG(("Karaoke weather: location not found (%1)").arg(trimmed.isEmpty() ? u"by IP"_q : trimmed));
		return result;
	}
	LOG(("Karaoke weather: location %1 (%2, %3)").arg(
		place.name,
		QString::number(place.latitude, 'f', 2),
		QString::number(place.longitude, 'f', 2)));
	auto url = QUrl(u"https://api.open-meteo.com/v1/forecast"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"latitude"_q, QString::number(place.latitude, 'f', 4));
	query.addQueryItem(u"longitude"_q, QString::number(place.longitude, 'f', 4));
	query.addQueryItem(u"current"_q, u"temperature_2m,apparent_temperature,weather_code,is_day,relative_humidity_2m"_q);
	query.addQueryItem(u"hourly"_q, u"temperature_2m,weather_code"_q);
	query.addQueryItem(u"forecast_hours"_q, QString::number(kHours));
	query.addQueryItem(u"timezone"_q, u"auto"_q);
	url.setQuery(query);
	const auto json = GetJson(url);
	const auto current = json.value(u"current"_q).toObject();
	if (current.isEmpty()) {
		if (auto met = FetchMetNo(place, trimmed); met.ok) {
			return met;
		}
		return FetchWttr(place, trimmed);
	}
	result.ok = true;
	result.fromIp = trimmed.isEmpty();
	result.city = place.name.isEmpty() ? trimmed : place.name;
	result.temperature = current.value(u"temperature_2m"_q).toDouble();
	result.feels = current.value(u"apparent_temperature"_q).toDouble();
	result.code = current.value(u"weather_code"_q).toInt();
	result.humidity = current.value(u"relative_humidity_2m"_q).toInt();
	result.day = current.value(u"is_day"_q).toInt(1) != 0;
	const auto hourly = json.value(u"hourly"_q).toObject();
	const auto times = hourly.value(u"time"_q).toArray();
	const auto temperatures = hourly.value(u"temperature_2m"_q).toArray();
	const auto codes = hourly.value(u"weather_code"_q).toArray();
	for (auto i = 0; i < times.size() && i < temperatures.size() && i < codes.size(); ++i) {
		const auto time = times[i].toString();
		const auto at = time.indexOf(QChar(u'T'));
		result.hours.push_back({
			.hour = (at >= 0) ? time.mid(at + 1, 2).toInt() : 0,
			.temperature = temperatures[i].toDouble(),
			.code = codes[i].toInt(),
		});
	}
	return result;
}

Sky SkyOf(int code) {
	if (code >= 95) {
		return Sky::Storm;
	} else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
		return Sky::Snow;
	} else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
		return Sky::Rain;
	} else if (code == 45 || code == 48) {
		return Sky::Fog;
	} else if (code >= 2) {
		return Sky::Cloudy;
	}
	return Sky::Clear;
}

QString Describe(int code) {
	switch (code) {
	case 0: return QString::fromUtf8("Ясно");
	case 1: return QString::fromUtf8("Почти ясно");
	case 2: return QString::fromUtf8("Переменная облачность");
	case 3: return QString::fromUtf8("Пасмурно");
	case 45:
	case 48: return QString::fromUtf8("Туман");
	case 51:
	case 53:
	case 55: return QString::fromUtf8("Морось");
	case 56:
	case 57: return QString::fromUtf8("Ледяная морось");
	case 61: return QString::fromUtf8("Небольшой дождь");
	case 63: return QString::fromUtf8("Дождь");
	case 65: return QString::fromUtf8("Ливень");
	case 66:
	case 67: return QString::fromUtf8("Ледяной дождь");
	case 71: return QString::fromUtf8("Небольшой снег");
	case 73: return QString::fromUtf8("Снег");
	case 75: return QString::fromUtf8("Снегопад");
	case 77: return QString::fromUtf8("Снежная крупа");
	case 80:
	case 81: return QString::fromUtf8("Ливневый дождь");
	case 82: return QString::fromUtf8("Сильный ливень");
	case 85:
	case 86: return QString::fromUtf8("Снежный заряд");
	case 95: return QString::fromUtf8("Гроза");
	case 96:
	case 99: return QString::fromUtf8("Гроза с градом");
	}
	return QString::fromUtf8("Облачно");
}

QString FormatDegrees(double value) {
	const auto rounded = int(std::lround(value));
	const auto sign = (rounded > 0) ? u"+"_q : (rounded < 0 ? QString::fromUtf8("−") : QString());
	return sign + QString::number(std::abs(rounded)) + QString::fromUtf8("°");
}

} // namespace Ayu::Ui::Weather
