// This file is part of Whitegram for Desktop.
#pragma once

#include <QtCore/QString>

#include <vector>

namespace Ayu::Ui::Weather {

enum class Sky {
	Clear,
	Cloudy,
	Fog,
	Rain,
	Snow,
	Storm,
};

struct Hour {
	int hour = 0;
	double temperature = 0.;
	int code = 0;
};

struct Data {
	bool ok = false;
	bool fromIp = false;
	QString city;
	double temperature = 0.;
	double feels = 0.;
	int code = 0;
	int humidity = 0;
	bool day = true;
	std::vector<Hour> hours;
};

[[nodiscard]] Data FetchSync(const QString &city);
[[nodiscard]] Sky SkyOf(int code);
[[nodiscard]] QString Describe(int code);
[[nodiscard]] QString FormatDegrees(double value);

} // namespace Ayu::Ui::Weather
