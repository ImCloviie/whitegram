// This file is part of Whitegram for Desktop.
#include "ayu/ui/utils/net_fetch.h"

#include "logs.h"

#include <QtCore/QEventLoop>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkProxyFactory>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <memory>

namespace Ayu::Ui::Net {
namespace {

QMutex &RoutesMutex() {
	static auto result = QMutex();
	return result;
}

QHash<QString, int> &Routes() {
	static auto result = QHash<QString, int>();
	return result;
}

bool SameProxy(const QNetworkProxy &a, const QNetworkProxy &b) {
	if (a.type() == QNetworkProxy::NoProxy || b.type() == QNetworkProxy::NoProxy) {
		return a.type() == b.type();
	}
	return a.type() == b.type() && a.hostName() == b.hostName() && a.port() == b.port();
}

std::vector<QNetworkProxy> RouteProxies(const QUrl &url) {
	const auto query = QNetworkProxyQuery(url);
	const auto app = QNetworkProxyFactory::proxyForQuery(query);
	const auto system = QNetworkProxyFactory::systemProxyForQuery(query);
	auto result = std::vector<QNetworkProxy>();
	const auto add = [&](const QNetworkProxy &proxy) {
		for (const auto &existing : result) {
			if (SameProxy(existing, proxy)) {
				return;
			}
		}
		result.push_back(proxy);
	};
	add(app.isEmpty() ? QNetworkProxy(QNetworkProxy::NoProxy) : app.front());
	add(QNetworkProxy(QNetworkProxy::NoProxy));
	if (!system.isEmpty()) {
		add(system.front());
	}
	return result;
}

struct Attempt {
	QByteArray data;
	bool ok = false;
	bool serverAnswered = false;
	QString error;
};

Attempt Run(const Request &request, const QNetworkProxy &proxy) {
	auto manager = QNetworkAccessManager();
	manager.setProxy(proxy);
	auto r = QNetworkRequest(request.url);
	r.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	r.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	r.setHeader(
		QNetworkRequest::UserAgentHeader,
		request.userAgent.isEmpty()
			? u"WhitegramDesktop/1.0 (https://github.com/ImCloviie)"_q
			: request.userAgent);
	if (!request.contentType.isEmpty()) {
		r.setHeader(QNetworkRequest::ContentTypeHeader, request.contentType);
	}
	for (const auto &[name, value] : request.headers) {
		r.setRawHeader(name, value);
	}
	const auto reply = request.body.isEmpty()
		? manager.get(r)
		: manager.post(r, request.body);
	auto loop = QEventLoop();
	auto timer = QTimer();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	timer.start(request.timeoutMs);
	loop.exec();
	const auto timedOut = !timer.isActive();
	timer.stop();
	const auto guard = std::unique_ptr<QNetworkReply, void(*)(QNetworkReply*)>(
		reply,
		[](QNetworkReply *r) { if (r->isRunning()) { r->abort(); } r->deleteLater(); });
	auto result = Attempt();
	const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	result.serverAnswered = !timedOut && status.isValid();
	if (timedOut) {
		result.error = u"timeout"_q;
	} else if (reply->error() != QNetworkReply::NoError) {
		result.error = reply->errorString();
	} else {
		result.ok = true;
		result.data = reply->readAll();
	}
	return result;
}

} // namespace

QByteArray FetchSync(const Request &request) {
	const auto host = request.url.host();
	auto routes = RouteProxies(request.url);
	auto preferred = 0;
	{
		const auto lock = QMutexLocker(&RoutesMutex());
		preferred = Routes().value(host, 0);
	}
	if (preferred > 0 && preferred < int(routes.size())) {
		std::swap(routes[0], routes[preferred]);
	}
	const auto all = RouteProxies(request.url);
	for (auto i = 0; i != int(routes.size()); ++i) {
		const auto attempt = Run(request, routes[i]);
		if (attempt.ok) {
			for (auto k = 0; k != int(all.size()); ++k) {
				if (SameProxy(all[k], routes[i])) {
					const auto lock = QMutexLocker(&RoutesMutex());
					Routes().insert(host, k);
					break;
				}
			}
			return attempt.data;
		}
		LOG(("Whitegram net: %1 failed via route %2 (%3)").arg(
			host,
			QString::number(i),
			attempt.error));
		if (attempt.serverAnswered) {
			break;
		}
	}
	return {};
}

QByteArray GetSync(const QUrl &url, int timeoutMs) {
	return FetchSync({ .url = url, .timeoutMs = timeoutMs });
}

} // namespace Ayu::Ui::Net
