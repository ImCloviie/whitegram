// This file is part of Whitegram for Desktop.
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <utility>
#include <vector>

namespace Ayu::Ui::Net {

struct Request {
	QUrl url;
	QByteArray body;
	QByteArray contentType;
	QString userAgent;
	std::vector<std::pair<QByteArray, QByteArray>> headers;
	int timeoutMs = 7000;
};

// Blocking: call from a worker thread; falls back past the app proxy on network failures.
[[nodiscard]] QByteArray FetchSync(const Request &request);
[[nodiscard]] QByteArray GetSync(const QUrl &url, int timeoutMs = 7000);

} // namespace Ayu::Ui::Net
