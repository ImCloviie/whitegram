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
	std::vector<QString> serverMirrorHosts;
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
	state.data.serverMirrorHosts = settings.bypassServerMirrorHosts();
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
		auto trimmed = part.trimmed();
		if (trimmed.isEmpty()) {
			continue;
		}
		// Accept whatever a user may paste: a bare domain, a full URL, a URL
		// with a path, or a value with a scheme. Only the host part is of any
		// use here, because the rest is combined into a kws host below.
		const auto scheme = trimmed.indexOf(u"://"_q);
		if (scheme >= 0) {
			trimmed = trimmed.mid(scheme + 3);
		}
		const auto slash = trimmed.indexOf(u'/');
		if (slash >= 0) {
			trimmed = trimmed.left(slash);
		}
		const auto query = trimmed.indexOf(u'?');
		if (query >= 0) {
			trimmed = trimmed.left(query);
		}
		const auto hash = trimmed.indexOf(u'#');
		if (hash >= 0) {
			trimmed = trimmed.left(hash);
		}
		// Drop a port and any userinfo, both of which Qt resolves poorly
		// inside the generated host name.
		const auto at = trimmed.lastIndexOf(u'@');
		if (at >= 0) {
			trimmed = trimmed.mid(at + 1);
		}
		const auto colon = trimmed.lastIndexOf(u':');
		if (colon >= 0) {
			trimmed = trimmed.left(colon);
		}
		trimmed = trimmed.trimmed();
		if (trimmed.isEmpty()) {
			continue;
		}
		// A host has to look like a hostname, otherwise it would only waste
		// the connection budget.
		auto valid = true;
		for (const auto ch : trimmed) {
			const auto isAllowed = (ch >= u'a' && ch <= u'z')
				|| (ch >= u'A' && ch <= u'Z')
				|| (ch >= u'0' && ch <= u'9')
				|| ch == u'.'
				|| ch == u'-'
				|| ch == u'_';
			if (!isAllowed) {
				valid = false;
				break;
			}
		}
		if (!valid || !trimmed.contains(u'.')) {
			LOG(("Bypass WS: ignoring malformed custom mirror \"%1\""
				).arg(trimmed));
			continue;
		}
		result.push_back(trimmed);
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
		// These are full hosts already prepared by the server, so they are
		// used as is instead of being turned into a kws name.
		for (const auto &host : snapshot.serverMirrorHosts) {
			AppendUnique(candidates, host.trimmed());
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

	// The official hosts are the only ones guaranteed to be reachable, so they
	// must never be pushed out of the budget by a mirror that happens to be
	// down. They are moved to the front and kept in their original order.
	auto official = std::vector<QString>();
	auto rest = std::vector<QString>();
	official.reserve(candidates.size());
	rest.reserve(candidates.size());
	for (auto &host : candidates) {
		if (IsOfficialTelegramHost(host)) {
			official.push_back(std::move(host));
		} else {
			rest.push_back(std::move(host));
		}
	}
	candidates = std::vector<QString>();
	candidates.reserve(official.size() + rest.size());
	for (auto &host : official) {
		candidates.push_back(std::move(host));
	}
	for (auto &host : rest) {
		candidates.push_back(std::move(host));
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
	auto pinnedCount = 0;
	// A host that worked before is by far the best first guess.
	if (pinLastGood) {
		for (const auto &host : filtered) {
			if (host.compare(lastGood, Qt::CaseInsensitive) == 0) {
				ordered.push_back(host);
				++pinnedCount;
				break;
			}
		}
	}
	// Then the official hosts, still in order, then everything else shuffled.
	auto officialTail = std::vector<QString>();
	auto restTail = std::vector<QString>();
	for (const auto &host : filtered) {
		if (pinLastGood
			&& host.compare(lastGood, Qt::CaseInsensitive) == 0) {
			continue;
		}
		if (IsOfficialTelegramHost(host)) {
			officialTail.push_back(host);
		} else {
			restTail.push_back(host);
		}
	}
	for (auto &host : officialTail) {
		ordered.push_back(std::move(host));
	}
	const auto shuffledFrom = int(ordered.size());
	for (auto &host : restTail) {
		ordered.push_back(std::move(host));
	}
	ShuffleTail(ordered, shuffledFrom);
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
	crl::on_main([hosts] {
		AyuSettings::getInstance().setBypassServerMirrorHosts(hosts);
		SyncRuntimeFromSettings();
	});
}

} // namespace AyuFeatures::BypassHosts
