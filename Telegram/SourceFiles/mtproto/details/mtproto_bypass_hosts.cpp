/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_bypass_hosts.h"

#include <QtCore/QRandomGenerator>
#include <QtCore/QString>

namespace MTP::details {
namespace {

BypassHostsResolver Resolver;
BypassHostResultHandler ResultHandler;
BypassHostsExhaustedHandler ExhaustedHandler;

constexpr const char *kCfBases[] = {
	"pclead.co.uk",
	"offshor.co.uk",
	"cakeisalie.co.uk",
	"noskomnadzor.co.uk",
	"lovetrue.co.uk",
	"sorokdva.co.uk",
	"pyatdesyatdva.co.uk",
	"kartoshka.co.uk",
	"sorokodin.co.uk",
	"pyatdesyatodin.co.uk",
	"notelega.co.uk",
	"ebally.co.uk",
	"nebally.co.uk",
	"havegreatday.co.uk",
	"pomogite.co.uk",
	"fixtelega.co.uk",
	"sadnews.co.uk",
	"onedaychamp.co.uk",
	"stopblocking.co.uk",
	"nothingthere.co.uk",
};

void AppendUnique(std::vector<QString> &hosts, const QString &host) {
	if (host.isEmpty()) {
		return;
	}
	for (const auto &existing : hosts) {
		if (existing.compare(host, Qt::CaseInsensitive) == 0) {
			return;
		}
	}
	hosts.push_back(host);
}

void AppendMirrorBase(
		std::vector<QString> &hosts,
		int dc,
		const QString &base,
		bool isMedia) {
	const auto trimmed = base.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	if (trimmed.startsWith(u"kws"_q, Qt::CaseInsensitive)) {
		AppendUnique(hosts, trimmed);
		return;
	}
	const auto primary = isMedia
		? u"kws%1-1.%2"_q.arg(dc).arg(trimmed)
		: u"kws%1.%2"_q.arg(dc).arg(trimmed);
	const auto secondary = isMedia
		? u"kws%1.%2"_q.arg(dc).arg(trimmed)
		: u"kws%1-1.%2"_q.arg(dc).arg(trimmed);
	AppendUnique(hosts, primary);
	AppendUnique(hosts, secondary);
}

void AppendOfficialHosts(std::vector<QString> &hosts, int dc, bool isMedia) {
	if (dc == 203) {
		dc = 2;
	}
	const auto primary = u"kws%1.web.telegram.org"_q.arg(dc);
	const auto secondary = u"kws%1-1.web.telegram.org"_q.arg(dc);
	if (isMedia) {
		AppendUnique(hosts, secondary);
		AppendUnique(hosts, primary);
	} else {
		AppendUnique(hosts, primary);
		AppendUnique(hosts, secondary);
	}
}

void ShuffleTail(std::vector<QString> &hosts, int keepOrderedPrefix) {
	if (int(hosts.size()) <= keepOrderedPrefix + 1) {
		return;
	}
	auto *rng = QRandomGenerator::global();
	for (auto i = int(hosts.size()) - 1; i > keepOrderedPrefix; --i) {
		const auto j = keepOrderedPrefix
			+ int(rng->bounded(i - keepOrderedPrefix + 1));
		std::swap(hosts[i], hosts[j]);
	}
}

} // namespace

void SetBypassHostsResolver(BypassHostsResolver resolver) {
	Resolver = std::move(resolver);
}

void SetBypassHostResultHandler(BypassHostResultHandler handler) {
	ResultHandler = std::move(handler);
}

void SetBypassHostsExhaustedHandler(BypassHostsExhaustedHandler handler) {
	ExhaustedHandler = std::move(handler);
}

std::vector<QString> DefaultBypassHosts(BypassHostRequest request) {
	auto dc = request.dc;
	if (dc == 203) {
		dc = 2;
	}
	if (dc < 1 || dc > 5) {
		dc = 2;
	}

	auto hosts = std::vector<QString>();
	if (!request.isTestDc) {
		for (const auto *base : kCfBases) {
			AppendMirrorBase(
				hosts,
				dc,
				QString::fromLatin1(base),
				request.isMedia);
		}
		ShuffleTail(hosts, 0);
		AppendOfficialHosts(hosts, dc, request.isMedia);
	}
	return hosts;
}

std::vector<QString> ResolveBypassHosts(BypassHostRequest request) {
	if (Resolver) {
		return Resolver(request);
	}
	return DefaultBypassHosts(request);
}

void NotifyBypassHostResult(const QString &host, bool success) {
	if (ResultHandler) {
		ResultHandler(host, success);
	}
}

void NotifyBypassHostsExhausted() {
	if (ExhaustedHandler) {
		ExhaustedHandler();
	}
}

} // namespace MTP::details
