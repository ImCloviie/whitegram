#include "ayu/features/connection_bypass/bypass_hosts.h"

#include "ayu/ayu_settings.h"
#include "base/random.h"
#include "crl/crl.h"
#include "logs.h"
#include "mtproto/details/mtproto_bypass_hosts.h"

#include <QtCore/QDateTime>

#include <mutex>

namespace AyuFeatures::BypassHosts {
namespace {

constexpr auto kBadHostCooldownMs = crl::time(10 * 60 * 1000);

struct RuntimeSnapshot {
	QString lastGood;
	std::map<QString, crl::time> badUntil;
	std::vector<QString> serverMirrors;
	QString customSecret;
};

struct RuntimeState {
	std::mutex mutex;
	RuntimeSnapshot data;
};

RuntimeState &State() {
	static RuntimeState state;
	return state;
}

void SyncRuntimeFromSettings() {
	const auto &settings = AyuSettings::getInstance();
	auto &state = State();
	const auto guard = std::lock_guard(state.mutex);
	state.data.lastGood = settings.bypassLastGoodHost();
	state.data.serverMirrors = settings.bypassServerMirrors();
	state.data.customSecret = settings.connectionBypassSecret();
	state.data.badUntil.clear();
	const auto now = QDateTime::currentMSecsSinceEpoch();
	for (const auto &[host, until] : settings.bypassBadHostsSnapshot()) {
		if (until > now) {
			state.data.badUntil.emplace(host, until);
		}
	}
}

[[nodiscard]] RuntimeSnapshot CopyRuntimeSnapshot() {
	auto &state = State();
	const auto guard = std::lock_guard(state.mutex);
	return state.data;
}

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

[[nodiscard]] bool IsOfficialTelegramHost(const QString &host) {
	return host.endsWith(u".web.telegram.org"_q, Qt::CaseInsensitive);
}

[[nodiscard]] bool IsOfficialTelegramBase(const QString &base) {
	return base.compare(u"web.telegram.org"_q, Qt::CaseInsensitive) == 0;
}

void AppendMirrorBase(
		std::vector<QString> &hosts,
		int dc,
		const QString &entry,
		bool isMedia) {
	const auto trimmed = entry.trimmed();
	if (trimmed.isEmpty() || IsOfficialTelegramBase(trimmed)) {
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

void ShuffleTail(std::vector<QString> &hosts, int keepOrderedPrefix) {
	if (int(hosts.size()) <= keepOrderedPrefix + 1) {
		return;
	}
	for (auto i = int(hosts.size()) - 1; i > keepOrderedPrefix; --i) {
		const auto j = keepOrderedPrefix
			+ int(base::RandomIndex(i - keepOrderedPrefix + 1));
		std::swap(hosts[i], hosts[j]);
	}
}

[[nodiscard]] std::vector<QString> ParseCustomDomains(
		const QString &raw) {
	auto result = std::vector<QString>();
	for (const auto &part : raw.split(u',')) {
		const auto trimmed = part.trimmed();
		if (!trimmed.isEmpty()) {
			result.push_back(trimmed);
		}
	}
	return result;
}

[[nodiscard]] bool IsBadHost(
		const RuntimeSnapshot &snapshot,
		const QString &host) {
	const auto it = snapshot.badUntil.find(host);
	if (it == end(snapshot.badUntil)) {
		return false;
	}
	return it->second > QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] std::vector<QString> Resolve(
		const MTP::details::BypassHostRequest &request) {
	const auto snapshot = CopyRuntimeSnapshot();
	auto dc = request.dc;
	if (dc == 203) {
		dc = 2;
	}
	if (dc < 1 || dc > 5) {
		dc = 2;
	}

	auto candidates = std::vector<QString>();
	if (!request.isTestDc) {
		for (const auto &base : snapshot.serverMirrors) {
			AppendMirrorBase(candidates, dc, base, request.isMedia);
		}
		for (const auto &custom : ParseCustomDomains(snapshot.customSecret)) {
			AppendMirrorBase(candidates, dc, custom, request.isMedia);
		}
		for (const auto &host : MTP::details::DefaultBypassHosts(request)) {
			AppendUnique(candidates, host);
		}
	} else {
		candidates = MTP::details::DefaultBypassHosts(request);
	}

	auto filtered = std::vector<QString>();
	filtered.reserve(candidates.size());
	for (const auto &host : candidates) {
		if (!IsBadHost(snapshot, host)) {
			filtered.push_back(host);
		}
	}
	if (filtered.empty()) {
		filtered = candidates;
	}

	const auto lastGood = snapshot.lastGood;
	const auto pinLastGood = !lastGood.isEmpty()
		&& !IsOfficialTelegramHost(lastGood);
	auto ordered = std::vector<QString>();
	if (pinLastGood) {
		for (const auto &host : filtered) {
			if (host.compare(lastGood, Qt::CaseInsensitive) == 0) {
				ordered.push_back(host);
				break;
			}
		}
	}
	for (const auto &host : filtered) {
		if (!pinLastGood
			|| host.compare(lastGood, Qt::CaseInsensitive) != 0) {
			ordered.push_back(host);
		}
	}

	const auto pinned = int(pinLastGood && !ordered.empty());
	ShuffleTail(ordered, pinned);
	DEBUG_LOG(("Bypass WS: prepared %1 hosts for dc %2").arg(
		int(ordered.size())).arg(dc));
	return ordered;
}

void OnHostResult(const QString &host, bool success) {
	if (host.isEmpty()) {
		return;
	}
	const auto until = QDateTime::currentMSecsSinceEpoch() + kBadHostCooldownMs;
	{
		auto &state = State();
		const auto guard = std::lock_guard(state.mutex);
		if (success) {
			if (!IsOfficialTelegramHost(host)) {
				state.data.lastGood = host;
			}
			state.data.badUntil.erase(host);
		} else {
			state.data.badUntil[host] = until;
		}
	}
	auto &settings = AyuSettings::getInstance();
	if (success) {
		if (!IsOfficialTelegramHost(host)) {
			settings.setBypassLastGoodHost(host);
		}
		settings.clearBypassBadHost(host);
		DEBUG_LOG(("Bypass WS: marked good host %1").arg(host));
	} else {
		settings.markBypassBadHost(host, until);
		DEBUG_LOG(("Bypass WS: marked bad host %1").arg(host));
	}
}

} // namespace

void init() {
	SyncRuntimeFromSettings();
	MTP::details::SetBypassHostsResolver([](
			const MTP::details::BypassHostRequest &request) {
		return Resolve(request);
	});
	MTP::details::SetBypassHostResultHandler([](
			const QString &host,
			bool success) {
		crl::on_main([=] {
			OnHostResult(host, success);
		});
	});
}

void syncFromSettings() {
	SyncRuntimeFromSettings();
}

void setServerMirrors(const std::vector<QString> &mirrors) {
	crl::on_main([mirrors] {
		AyuSettings::getInstance().setBypassServerMirrors(mirrors);
		SyncRuntimeFromSettings();
	});
}

void setServerMirrorHosts(const std::vector<QString> &hosts) {
	(void)hosts;
}

} // namespace AyuFeatures::BypassHosts
