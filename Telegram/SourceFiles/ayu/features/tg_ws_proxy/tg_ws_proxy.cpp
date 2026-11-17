// Whitegram bypass 2: bundled Flowseal/tg-ws-proxy (MIT) as a local MTProto proxy.
#include "ayu/features/tg_ws_proxy/tg_ws_proxy.h"

#include "ayu/ayu_settings.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "logs.h"
#include "main/main_domain.h"
#include "mtproto/mtproto_proxy_data.h"
#include "settings.h"
#include "storage/localstorage.h"

#include "rpl/variable.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QRandomGenerator>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif // Q_OS_WIN

namespace AyuFeatures::TgWsProxy {
namespace {

constexpr auto kHost = "127.0.0.1";
constexpr auto kReadyPollDelay = crl::time(300);
constexpr auto kReadyTimeout = crl::time(20 * 1000);
constexpr auto kRestartDelay = crl::time(3000);
constexpr auto kMaxQuickRestarts = 5;
constexpr auto kStableRunTime = crl::time(60 * 1000);

[[nodiscard]] QString GenerateSecret() {
	auto result = QString();
	auto *rng = QRandomGenerator::system();
	for (auto i = 0; i != 16; ++i) {
		result += u"%1"_q.arg(rng->bounded(256), 2, 16, QChar('0'));
	}
	return result;
}

#ifdef Q_OS_WIN
// WHY: the PyInstaller exe is a launcher plus a child that runs the proxy;
// killing the launcher alone leaves the child alive, so both go in a job.
[[nodiscard]] HANDLE CreateKillOnCloseJob(qint64 pid) {
	const auto job = CreateJobObjectW(nullptr, nullptr);
	if (!job) {
		return nullptr;
	}
	auto info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
	info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	SetInformationJobObject(
		job,
		JobObjectExtendedLimitInformation,
		&info,
		sizeof(info));
	const auto process = OpenProcess(
		PROCESS_SET_QUOTA | PROCESS_TERMINATE,
		FALSE,
		DWORD(pid));
	const auto assigned = process && AssignProcessToJobObject(job, process);
	if (process) {
		CloseHandle(process);
	}
	if (!assigned) {
		LOG(("TgWsProxy: could not assign process to job, error %1"
			).arg(GetLastError()));
		CloseHandle(job);
		return nullptr;
	}
	return job;
}
#endif // Q_OS_WIN

[[nodiscard]] int FindFreePort() {
	auto server = QTcpServer();
	if (!server.listen(QHostAddress::LocalHost, 0)) {
		return 0;
	}
	const auto port = int(server.serverPort());
	server.close();
	return port;
}

class Manager final : public QObject {
public:
	Manager()
	: _readyTimer([=] { probeReady(); })
	, _restartTimer([=] { startProcess(); }) {
	}

	void start() {
		if (_started) {
			return;
		}
		_started = true;
		QObject::connect(
			qApp,
			&QCoreApplication::aboutToQuit,
			this,
			[=] { killProcess(); });
		reevaluate();
	}

	void reevaluate() {
		if (!_started || Core::Quitting() || !Core::IsAppLaunched()) {
			return;
		}
		const auto enabled = AyuSettings::getInstance().tgWsProxyEnabled();
		if (!enabled && !_wanted && !_staleChecked) {
			_staleChecked = true;
			if (hasStaleEntries()) {
				// The app closed while bypass 2 was on and it is off now.
				stop();
			}
		}
		if (enabled && !_wanted) {
			_wanted = true;
			_quickRestarts = 0;
			startProcess();
		} else if (!enabled && _wanted) {
			_wanted = false;
			stop();
		}
	}

	void shutdown() {
		killProcess();
	}

	[[nodiscard]] Status status() const {
		return _status.current();
	}

	[[nodiscard]] rpl::producer<Status> statusValue() const {
		return _status.value();
	}

private:
	[[nodiscard]] QString secret() {
		auto &settings = AyuSettings::getInstance();
		auto result = settings.tgWsProxySecret();
		if (result.size() != 32) {
			result = GenerateSecret();
			settings.setTgWsProxySecret(result);
		}
		return result;
	}

	[[nodiscard]] bool hasStaleEntries() const {
		if (AyuSettings::getInstance().tgWsProxySecret().size() != 32) {
			return false;
		}
		const auto &proxySettings = Core::App().settings().proxy();
		return isOurs(proxySettings.selected())
			|| ranges::any_of(proxySettings.list(), [&](const auto &p) {
				return isOurs(p);
			});
	}

	[[nodiscard]] bool isOurs(const MTP::ProxyData &proxy) const {
		const auto secret = AyuSettings::getInstance().tgWsProxySecret();
		return (secret.size() == 32)
			&& proxy.type == MTP::ProxyData::Type::Mtproto
			&& proxy.host == QString::fromLatin1(kHost)
			&& proxy.password.endsWith(secret, Qt::CaseInsensitive);
	}

	void setFailed(const QString &error) {
		LOG(("TgWsProxy: %1").arg(error));
		_status = Status{ .state = State::Failed, .error = error };
	}

	void startProcess() {
		if (!_wanted || Core::Quitting()) {
			return;
		}
		killProcess();
		const auto path = executablePath();
		if (!QFileInfo::exists(path)) {
			setFailed(u"tg-ws-proxy.exe not found"_q);
			return;
		}
		_port = FindFreePort();
		if (!_port) {
			setFailed(u"no free local port"_q);
			return;
		}
		_status = Status{ .state = State::Starting, .port = _port };
		_process = new QProcess(this);
		_process->setProgram(path);
		_process->setArguments({
			u"--host"_q, QString::fromLatin1(kHost),
			u"--port"_q, QString::number(_port),
			u"--secret"_q, secret(),
			u"--log-file"_q, cWorkingDir() + u"tg-ws-proxy.log"_q,
		});
		// WHY: with inherited handles the proxy kept tdata/working open, so a
		// leftover proxy stopped Whitegram from starting; it gets none now.
		_process->setProcessChannelMode(QProcess::ForwardedChannels);
		_process->setInputChannelMode(QProcess::ForwardedInputChannel);
#ifdef Q_OS_WIN
		_process->setCreateProcessArgumentsModifier([](
				QProcess::CreateProcessArguments *args) {
			args->inheritHandles = FALSE;
			args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
		});
#endif // Q_OS_WIN
		const auto process = _process;
		QObject::connect(
			process,
			qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this,
			[=](int code, QProcess::ExitStatus) { processFinished(process, code); });
		QObject::connect(
			process,
			&QProcess::errorOccurred,
			this,
			[=](QProcess::ProcessError error) {
				if (error == QProcess::FailedToStart && _process == process) {
					setFailed(u"failed to start: %1"_q.arg(process->errorString()));
				}
			});
		QObject::connect(process, &QProcess::started, this, [=] {
#ifdef Q_OS_WIN
			if (_process == process) {
				closeJob(false);
				_job = CreateKillOnCloseJob(process->processId());
			}
#endif // Q_OS_WIN
		});
		LOG(("TgWsProxy: starting on %1:%2").arg(kHost).arg(_port));
		_startedAt = crl::now();
		_process->start();
		_readyDeadline = crl::now() + kReadyTimeout;
		_readyTimer.callOnce(kReadyPollDelay);
	}

	void probeReady() {
		if (!_wanted || !_process) {
			return;
		}
		if (crl::now() > _readyDeadline) {
			setFailed(u"proxy did not start listening"_q);
			killProcess();
			scheduleRestart();
			return;
		}
		auto socket = new QTcpSocket(this);
		const auto port = _port;
		const auto done = [=](bool ok) {
			socket->deleteLater();
			if (!_wanted || port != _port) {
				return;
			}
			if (ok) {
				becameReady();
			} else {
				_readyTimer.callOnce(kReadyPollDelay);
			}
		};
		QObject::connect(socket, &QTcpSocket::connected, this, [=] {
			socket->disconnect(this);
			socket->abort();
			done(true);
		});
		QObject::connect(socket, &QTcpSocket::errorOccurred, this, [=] {
			socket->disconnect(this);
			done(false);
		});
		socket->connectToHost(QString::fromLatin1(kHost), quint16(port));
	}

	void becameReady() {
		LOG(("TgWsProxy: listening on %1:%2").arg(kHost).arg(_port));
		auto &proxySettings = Core::App().settings().proxy();
		const auto current = proxySettings.selected();
		if (!_hasPrevious && !isOurs(current)) {
			_previous = current;
			_previousSettings = proxySettings.settings();
			_hasPrevious = true;
		}
		auto proxy = MTP::ProxyData();
		proxy.type = MTP::ProxyData::Type::Mtproto;
		proxy.host = QString::fromLatin1(kHost);
		proxy.port = uint32(_port);
		proxy.password = u"dd"_q + secret();
		Core::App().setCurrentProxy(proxy, MTP::ProxyData::Settings::Enabled);
		Local::writeSettings();
		_status = Status{ .state = State::Running, .port = _port };
	}

	void processFinished(QProcess *process, int code) {
		if (_process != process) {
			return;
		}
		_process = nullptr;
		process->deleteLater();
		closeJob(true);
		_readyTimer.cancel();
		if (!_wanted || Core::Quitting()) {
			return;
		}
		LOG(("TgWsProxy: process exited with code %1").arg(code));
		if (crl::now() - _startedAt > kStableRunTime) {
			_quickRestarts = 0;
		}
		scheduleRestart();
	}

	void scheduleRestart() {
		if (++_quickRestarts > kMaxQuickRestarts) {
			setFailed(u"proxy keeps stopping, see tg-ws-proxy.log"_q);
			return;
		}
		_status = Status{ .state = State::Starting, .port = _port };
		_restartTimer.callOnce(kRestartDelay);
	}

	void killProcess() {
		_readyTimer.cancel();
		_restartTimer.cancel();
		if (const auto process = base::take(_process)) {
			process->disconnect(this);
			process->kill();
			process->waitForFinished(1000);
			process->deleteLater();
		}
		closeJob(true);
	}

	void closeJob(bool terminate) {
#ifdef Q_OS_WIN
		if (const auto job = base::take(_job)) {
			if (terminate) {
				TerminateJobObject(job, 0);
			}
			CloseHandle(job);
		}
#endif // Q_OS_WIN
	}

	void stop() {
		killProcess();
		if (Core::IsAppLaunched()) {
			auto &proxySettings = Core::App().settings().proxy();
			if (isOurs(proxySettings.selected())) {
				if (_hasPrevious && !isOurs(_previous)) {
					Core::App().setCurrentProxy(_previous, _previousSettings);
				} else {
					Core::App().setCurrentProxy(
						MTP::ProxyData(),
						MTP::ProxyData::Settings::Disabled);
				}
			}
			auto list = proxySettings.list();
			const auto was = list.size();
			list.erase(ranges::remove_if(list, [&](const MTP::ProxyData &p) {
				return isOurs(p);
			}), end(list));
			if (list.size() != was) {
				proxySettings.setList(std::move(list));
			}
			Local::writeSettings();
		}
		_hasPrevious = false;
		_status = Status();
		LOG(("TgWsProxy: stopped"));
	}

	bool _started = false;
	bool _wanted = false;
	bool _staleChecked = false;
	QProcess *_process = nullptr;
#ifdef Q_OS_WIN
	HANDLE _job = nullptr;
#endif // Q_OS_WIN
	int _port = 0;
	int _quickRestarts = 0;
	crl::time _startedAt = 0;
	crl::time _readyDeadline = 0;
	MTP::ProxyData _previous;
	MTP::ProxyData::Settings _previousSettings
		= MTP::ProxyData::Settings::System;
	bool _hasPrevious = false;
	base::Timer _readyTimer;
	base::Timer _restartTimer;
	rpl::variable<Status> _status;
};

Manager &Instance() {
	static Manager instance;
	return instance;
}

} // namespace

void init() {
	Instance().start();
}

void reevaluate() {
	Instance().reevaluate();
}

void shutdown() {
	Instance().shutdown();
}

Status status() {
	return Instance().status();
}

rpl::producer<Status> statusValue() {
	return Instance().statusValue();
}

QString executablePath() {
	return cExeDir() + u"modules/x64/tgwsproxy/tg-ws-proxy.exe"_q;
}

bool KillOrphans() {
#ifdef Q_OS_WIN
	const auto target = QDir::toNativeSeparators(
		QFileInfo(executablePath()).absoluteFilePath()).toLower();
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}
	auto killed = 0;
	auto entry = PROCESSENTRY32W();
	entry.dwSize = sizeof(entry);
	for (auto ok = Process32FirstW(snapshot, &entry); ok;
			ok = Process32NextW(snapshot, &entry)) {
		if (_wcsicmp(entry.szExeFile, L"tg-ws-proxy.exe") != 0) {
			continue;
		}
		const auto process = OpenProcess(
			PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
			FALSE,
			entry.th32ProcessID);
		if (!process) {
			continue;
		}
		WCHAR path[MAX_PATH * 2] = { 0 };
		auto size = DWORD(std::size(path));
		const auto ours = QueryFullProcessImageNameW(process, 0, path, &size)
			&& (QString::fromWCharArray(path, int(size)).toLower() == target);
		if (ours && TerminateProcess(process, 0)) {
			WaitForSingleObject(process, 3000);
			++killed;
		}
		CloseHandle(process);
	}
	CloseHandle(snapshot);
	if (killed) {
		LOG(("TgWsProxy: stopped %1 leftover proxy processes").arg(killed));
	}
	return killed > 0;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

} // namespace AyuFeatures::TgWsProxy
