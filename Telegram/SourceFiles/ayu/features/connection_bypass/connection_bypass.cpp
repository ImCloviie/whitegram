// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/connection_bypass/connection_bypass.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/connection_bypass/bypass_hosts.h"
#include "base/random.h"
#include "base/timer.h"
#include "crl/crl.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "logs.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/details/mtproto_abstract_socket.h"
#include "mtproto/details/mtproto_bypass_hosts.h"
#include "mtproto/facade.h"
#include "mtproto/mtproto_proxy_data.h"
#include "storage/localstorage.h"

#include "rpl/variable.h"

#include <QtCore/QCoreApplication>

namespace AyuFeatures::ConnectionBypass {
namespace {

constexpr auto kAutoWait = crl::time(12 * 1000);
constexpr auto kRestartJitterMin = crl::time(1000);
constexpr auto kRestartJitterMax = crl::time(5000);
constexpr auto kWsRetryAfterFallback = crl::time(3 * 60 * 1000);
constexpr auto kRestartDebounce = crl::time(500);
constexpr auto kMinRestartInterval = crl::time(2000);
constexpr auto kSwitchWindow = crl::time(10 * 1000);
constexpr auto kSwitchLimit = 3;
constexpr auto kSwitchCooldown = crl::time(8 * 1000);
constexpr auto kStartupRestartDelay = crl::time(5000);

enum class RestartKind {
	Startup,
	User,
	Internal,
};

[[nodiscard]] bool domainReady() {
	return Core::IsAppLaunched() && Core::App().domain().started();
}

[[nodiscard]] bool anyAccountConnected() {
	if (!domainReady()) {
		return false;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (!account || account->mtp().isTestMode()) {
			continue;
		}
		if (account->mtp().dcstate() == MTP::ConnectedState) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool hasWatchableAccount() {
	if (!domainReady()) {
		return false;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (account && !account->mtp().isTestMode()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool isLegacyLocalProxy(const MTP::ProxyData &proxy) {
	return proxy.type == MTP::ProxyData::Type::Mtproto
		&& (proxy.host == u"127.0.0.1"_q || proxy.host == u"localhost"_q)
		&& (proxy.port == 1455 || proxy.port == 1443);
}

[[nodiscard]] bool clearLegacyLocalProxy() {
	if (!Core::IsAppLaunched()) {
		return false;
	}
	auto &settings = Core::App().settings().proxy();
	auto changed = false;
	if (isLegacyLocalProxy(settings.selected())) {
		Core::App().setCurrentProxy(
			MTP::ProxyData(),
			MTP::ProxyData::Settings::Disabled);
		changed = true;
	}
	auto list = settings.list();
	auto filtered = std::vector<MTP::ProxyData>();
	filtered.reserve(list.size());
	for (const auto &entry : list) {
		if (!isLegacyLocalProxy(entry)) {
			filtered.push_back(entry);
		}
	}
	if (filtered.size() != list.size()) {
		settings.setList(std::move(filtered));
		changed = true;
	}
	if (changed) {
		Local::writeSettings();
	}
	return changed;
}

class Manager final : public QObject {
public:
	Manager()
	: _autoTimer([=] { onAutoTick(); })
	, _restartTimer([=] { onRestartTimer(); })
	, _startupTimer([=] { tryApplyWhenReady(); })
	, _cooldownTimer([=] { finishSwitchCooldown(); })
	, _startupRestartTimer([=] { onStartupRestart(); }) {
	}

	[[nodiscard]] bool modeSwitchLocked() const {
		return _modeSwitchLocked.current();
	}

	[[nodiscard]] rpl::producer<bool> modeSwitchLockedValue() const {
		return _modeSwitchLocked.value();
	}

	void registerUserSwitch() {
		const auto now = crl::now();
		if (!_switchWindowStart
			|| (now - _switchWindowStart) > kSwitchWindow) {
			_switchWindowStart = now;
			_switchCount = 1;
			return;
		}
		if (++_switchCount < kSwitchLimit) {
			return;
		}
		_switchWindowStart = 0;
		_switchCount = 0;
		_cooldownUntil = now + kSwitchCooldown;
		if (!_modeSwitchLocked.current()) {
			_modeSwitchLocked = true;
		}
		_cooldownTimer.callOnce(kSwitchCooldown);
	}

	void start() {
		if (_started) {
			return;
		}
		_started = true;

		BypassHosts::init();
		MTP::details::SetBypassHostsExhaustedHandler([=] {
			crl::on_main([=] {
				handleBypassExhausted();
			});
		});

		QObject::connect(
			qApp,
			&QCoreApplication::aboutToQuit,
			this,
			[=] { shutdown(); });

		if (Core::IsAppLaunched()) {
			Core::App().settings().proxy().connectionTypeValue(
			) | rpl::on_next([=] {
				if (AyuSettings::getInstance().connectionBypassMode()
					!= ConnectionBypassMode::Off) {
					if (clearLegacyLocalProxy()) {
						scheduleRestart(RestartKind::Internal);
					}
				}
				watchAuto();
			}, _lifetime);

			Core::App().domain().accountsChanges(
			) | rpl::on_next([=] {
				syncBypassState();
			}, _lifetime);
		}

		syncBypassState();
		tryApplyWhenReady();
	}

	void tryApplyWhenReady() {
		if (!_started || Core::Quitting() || !Core::IsAppLaunched()) {
			return;
		}
		if (!Core::App().domain().started()) {
			_startupTimer.callOnce(200);
			return;
		}
		syncBypassState();
		scheduleStartupRestartIfNeeded();
	}

	void reevaluate(bool forceRestart, bool userInitiated) {
		if (!_started || Core::Quitting() || !Core::IsAppLaunched()) {
			return;
		}
		if (userInitiated) {
			registerUserSwitch();
			const auto mode = AyuSettings::getInstance().connectionBypassMode();
			if (mode == ConnectionBypassMode::Auto) {
				_wsFallbackActive = false;
				_autoTimer.cancel();
				if (anyAccountConnected()) {
					_notConnectedSince = 0;
					_autoTriggered = false;
				} else {
					_autoTriggered = true;
					_notConnectedSince = crl::now();
				}
			}
		}
		syncBypassState();
		if (!forceRestart) {
			return;
		}
		const auto immediate = userInitiated;
		scheduleRestart(
			userInitiated ? RestartKind::User : RestartKind::Internal,
			immediate);
	}

	void shutdown() {
		_autoTimer.cancel();
		_restartTimer.cancel();
		_startupTimer.cancel();
		_cooldownTimer.cancel();
		_startupRestartTimer.cancel();
		MTP::details::SetPreferWebSocket(false);
	}

private:
	void onStartupRestart() {
		if (!_started || Core::Quitting() || !Core::IsAppLaunched()) {
			return;
		}
		if (!hasReadySession()) {
			if (++_startupRestartAttempts < 5) {
				_startupRestartTimer.callOnce(2000);
			} else {
				_startupRestartDone = true;
			}
			return;
		}
		_startupRestartDone = true;
		requestRestartNow();
	}

	[[nodiscard]] bool hasReadySession() const {
		if (!domainReady()) {
			return false;
		}
		for (const auto &[index, account] : Core::App().domain().accounts()) {
			if (account && account->sessionExists()) {
				return true;
			}
		}
		return false;
	}

	void scheduleStartupRestartIfNeeded() {
		if (_startupRestartDone || _startupRestartTimer.isActive()) {
			return;
		}
		const auto mode = AyuSettings::getInstance().connectionBypassMode();
		if (mode == ConnectionBypassMode::Off) {
			return;
		}
		if (mode == ConnectionBypassMode::Auto && anyAccountConnected()) {
			return;
		}
		_startupRestartTimer.callOnce(kStartupRestartDelay);
	}
	[[nodiscard]] bool inSwitchCooldown() const {
		return crl::now() < _cooldownUntil;
	}

	void finishSwitchCooldown() {
		_cooldownUntil = 0;
		if (_modeSwitchLocked.current()) {
			_modeSwitchLocked = false;
		}
		onRestartTimer();
	}

	void onRestartTimer() {
		if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
			_startupTimer.callOnce(200);
			return;
		}
		const auto now = crl::now();
		if (inSwitchCooldown()) {
			_restartTimer.callOnce(_cooldownUntil - now);
			return;
		}
		const auto minWait = (_lastRestartAt + kMinRestartInterval) - now;
		if (minWait > 0) {
			_restartTimer.callOnce(minWait);
			return;
		}
		requestRestartNow();
	}

	void requestRestartNow() {
		_wantImmediateRestart = false;
		_lastRestartAt = crl::now();
		restartConnectionsNow();
	}

	void syncBypassState() {
		BypassHosts::syncFromSettings();
		const auto mode = AyuSettings::getInstance().connectionBypassMode();
		[[maybe_unused]] const auto cleared = clearLegacyLocalProxy();
		if (mode == ConnectionBypassMode::Off) {
			_autoTriggered = false;
			_wsFallbackActive = false;
			_autoTimer.cancel();
			_notConnectedSince = 0;
			setPreferWebSocket(false);
			return;
		}
		if (mode == ConnectionBypassMode::Always) {
			_autoTriggered = false;
			_wsFallbackActive = false;
			_autoTimer.cancel();
			_notConnectedSince = 0;
			setPreferWebSocket(true);
			return;
		}
		if (_autoTriggered && !_wsFallbackActive) {
			setPreferWebSocket(true);
			return;
		}
		if (_wsFallbackActive) {
			setPreferWebSocket(false);
			watchAuto();
			return;
		}
		_notConnectedSince = 0;
		setPreferWebSocket(false);
		watchAuto();
	}

	void setPreferWebSocket(bool enabled) {
		if (MTP::details::PreferWebSocket() == enabled) {
			return;
		}
		MTP::details::SetPreferWebSocket(enabled);
		DEBUG_LOG(("Bypass: WebSocket %1").arg(
			enabled ? "enabled" : "disabled"));
	}

	void scheduleRestart(RestartKind kind, bool immediate = false) {
		if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
			_startupTimer.callOnce(200);
			return;
		}
		if (kind == RestartKind::Startup) {
			scheduleStartupRestartIfNeeded();
			return;
		}
		if (immediate) {
			_wantImmediateRestart = true;
		}
		const auto now = crl::now();
		auto delay = crl::time(0);
		if (inSwitchCooldown()) {
			delay = std::max(delay, _cooldownUntil - now);
		}
		const auto minWait = (_lastRestartAt + kMinRestartInterval) - now;
		if (minWait > 0) {
			delay = std::max(delay, minWait);
		}
		if (!_wantImmediateRestart || kind == RestartKind::Internal) {
			delay = std::max(delay, kRestartDebounce);
			const auto span = int(kRestartJitterMax - kRestartJitterMin);
			delay = std::max(
				delay,
				kRestartJitterMin + crl::time(base::RandomIndex(span)));
		} else if (delay == 0) {
			_restartTimer.cancel();
			onRestartTimer();
			return;
		}
		_restartTimer.callOnce(delay);
	}

	void restartConnectionsNow() {
		if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
			return;
		}
		for (const auto &[index, account] : Core::App().domain().accounts()) {
			if (!account || !account->sessionExists()) {
				continue;
			}
			account->mtp().restart();
		}
	}

	void handleBypassExhausted() {
		const auto mode = AyuSettings::getInstance().connectionBypassMode();
		if (mode != ConnectionBypassMode::Auto
			|| !MTP::details::PreferWebSocket()
			|| _wsFallbackActive) {
			return;
		}
		DEBUG_LOG(("Bypass: all WS hosts failed, falling back to TCP"));
		_wsFallbackActive = true;
		_wsRetryAfter = crl::now() + kWsRetryAfterFallback;
		setPreferWebSocket(false);
		scheduleRestart(RestartKind::Internal);
	}

	void watchAuto() {
		if (!_started || Core::Quitting()) {
			return;
		}
		const auto mode = AyuSettings::getInstance().connectionBypassMode();
		if (mode != ConnectionBypassMode::Auto) {
			return;
		}
		if (_wsFallbackActive) {
			if (crl::now() >= _wsRetryAfter && !anyAccountConnected()) {
				DEBUG_LOG(("Bypass: retrying WebSocket after TCP fallback"));
				_wsFallbackActive = false;
				_autoTriggered = true;
				setPreferWebSocket(true);
				scheduleRestart(RestartKind::Internal);
			}
			if (!_autoTimer.isActive()) {
				_autoTimer.callEach(1000);
			}
			return;
		}
		if (_autoTriggered || MTP::details::PreferWebSocket()) {
			return;
		}
		if (!hasWatchableAccount()) {
			_notConnectedSince = 0;
			if (!_autoTimer.isActive()) {
				_autoTimer.callEach(1000);
			}
			return;
		}
		if (anyAccountConnected()) {
			_notConnectedSince = 0;
			_autoTimer.cancel();
			return;
		}
		if (!_notConnectedSince) {
			_notConnectedSince = crl::now();
		}
		if (!_autoTimer.isActive()) {
			_autoTimer.callEach(1000);
		}
	}

	void onAutoTick() {
		const auto mode = AyuSettings::getInstance().connectionBypassMode();
		if (mode != ConnectionBypassMode::Auto) {
			_autoTimer.cancel();
			return;
		}
		if (_wsFallbackActive) {
			watchAuto();
			return;
		}
		if (_autoTriggered || MTP::details::PreferWebSocket()) {
			_autoTimer.cancel();
			return;
		}
		if (!hasWatchableAccount()) {
			return;
		}
		if (anyAccountConnected()) {
			_notConnectedSince = 0;
			_autoTimer.cancel();
			return;
		}
		if (!_notConnectedSince) {
			_notConnectedSince = crl::now();
			return;
		}
		if (crl::now() - _notConnectedSince < kAutoWait) {
			return;
		}
		_autoTimer.cancel();
		_autoTriggered = true;
		[[maybe_unused]] const auto cleared = clearLegacyLocalProxy();
		setPreferWebSocket(true);
		scheduleRestart(RestartKind::Internal);
	}

	bool _started = false;
	bool _autoTriggered = false;
	bool _wsFallbackActive = false;
	bool _wantImmediateRestart = false;
	bool _startupRestartDone = false;
	int _startupRestartAttempts = 0;
	int _switchCount = 0;
	crl::time _notConnectedSince = 0;
	crl::time _wsRetryAfter = 0;
	crl::time _lastRestartAt = 0;
	crl::time _cooldownUntil = 0;
	crl::time _switchWindowStart = 0;
	base::Timer _autoTimer;
	base::Timer _restartTimer;
	base::Timer _startupTimer;
	base::Timer _cooldownTimer;
	base::Timer _startupRestartTimer;
	rpl::variable<bool> _modeSwitchLocked = false;
	rpl::lifetime _lifetime;
};

Manager &Instance() {
	static Manager instance;
	return instance;
}

} // namespace

void init() {
	Instance().start();
}

void reevaluate(bool forceRestart, bool userInitiated) {
	Instance().reevaluate(forceRestart, userInitiated);
}

void shutdown() {
	Instance().shutdown();
}

bool modeSwitchLocked() {
	return Instance().modeSwitchLocked();
}

rpl::producer<bool> modeSwitchLockedValue() {
	return Instance().modeSwitchLockedValue();
}

} // namespace AyuFeatures::ConnectionBypass
