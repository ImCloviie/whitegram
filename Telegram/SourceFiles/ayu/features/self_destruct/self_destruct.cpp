#include "ayu/features/self_destruct/self_destruct.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/self_destruct/self_destruct_box.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/random.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/core_cloud_password.h"
#include "lang/lang_instance.h"
#include "logs.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "base/bytes.h"
#include "settings.h"
#include "storage/localstorage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Whitegram::SelfDestruct {
namespace {

[[nodiscard]] QString NativePath(const QString &path) {
	return QDir::toNativeSeparators(path);
}

void ShredFile(const QString &filePath) {
	if (filePath.isEmpty() || !QFile::exists(filePath)) {
		return;
	}
	QFile::setPermissions(
		filePath,
		QFileDevice::ReadOwner | QFileDevice::WriteOwner |
		QFileDevice::ReadUser | QFileDevice::WriteUser);

	QFile file(filePath);
	if (file.open(QIODevice::ReadWrite)) {
		const auto size = file.size();
		if (size > 0) {
			constexpr qint64 kChunkSize = 65536;
			const QByteArray zeros(kChunkSize, '\0');
			file.seek(0);
			qint64 remaining = size;
			while (remaining > 0) {
				const auto toWrite = std::min(remaining, kChunkSize);
				file.write(zeros.constData(), toWrite);
				remaining -= toWrite;
			}
			file.flush();
		}
		file.resize(0);
		file.flush();
		file.close();
	}
	QFile::remove(filePath);
}

void ShredDirectory(const QString &dirPath) {
	QDir dir(dirPath);
	if (!dir.exists()) {
		return;
	}
	const auto entries = dir.entryInfoList(
		QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
	for (const auto &entry : entries) {
		if (entry.isDir()) {
			ShredDirectory(entry.absoluteFilePath());
		} else {
			ShredFile(entry.absoluteFilePath());
		}
	}
	dir.rmdir(dirPath);
}

void DeleteIfExists(const QString &path) {
	if (path.isEmpty()) {
		return;
	}
	const auto info = QFileInfo(path);
	if (info.isDir()) {
		base::Platform::DeleteDirectory(path);
	} else if (info.isFile()) {
		QFile::remove(path);
	}
}

void DeleteLogFiles(const QString &workingDir) {
	const auto dir = QDir(workingDir);
	const auto logs = dir.entryList(
		{ u"log*.txt"_q },
		QDir::Files);
	for (const auto &name : logs) {
		ShredFile(workingDir + name);
	}
}

[[nodiscard]] bool IsSafeWipeRoot(const QString &path) {
	const auto cleaned = QDir::cleanPath(path);
	if (cleaned.size() <= 3) {
		return false;
	}
	const auto native = QDir::toNativeSeparators(QDir(cleaned).absolutePath()).toLower();

	const auto winDir = QDir::toNativeSeparators(QDir(qgetenv("WINDIR")).absolutePath()).toLower();
	if (!winDir.isEmpty() && (native == winDir || native.startsWith(winDir + u'\\'))) {
		return false;
	}
	const auto progFiles = QDir::toNativeSeparators(QDir(qgetenv("ProgramFiles")).absolutePath()).toLower();
	if (!progFiles.isEmpty() && native == progFiles) {
		return false;
	}
	const auto progFilesX86 = QDir::toNativeSeparators(QDir(qgetenv("ProgramFiles(x86)")).absolutePath()).toLower();
	if (!progFilesX86.isEmpty() && native == progFilesX86) {
		return false;
	}
	const auto userProfile = QDir::toNativeSeparators(QDir(qgetenv("USERPROFILE")).absolutePath()).toLower();
	if (!userProfile.isEmpty()) {
		if (native == userProfile) {
			return false;
		}
		if (native == userProfile + u"\\desktop"_q || native == userProfile + u"\\downloads"_q) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString BuildCleanupScript(qint64 targetPid) {
	const auto exeName = cExeName();
	const auto exePath = NativePath(cExeDir() + cExeName());
	const auto installDir = NativePath(QDir(cExeDir()).absolutePath());
	const auto installDirSlash = QDir(cExeDir()).absolutePath();
	const auto workingDir = NativePath(QDir(cWorkingDir()).absolutePath());
	const auto tdataDir = NativePath(cWorkingDir() + u"tdata"_q);
	const auto safeWipeInstall = IsSafeWipeRoot(cExeDir());
	const auto safeWipeWorking = (workingDir != installDir) && IsSafeWipeRoot(cWorkingDir());

	auto s = uR"(@echo off
setlocal enableextensions enabledelayedexpansion

cd /d "%TEMP%"

set PID={{TARGET_PID}}
if not "%PID%"=="" (
    taskkill /F /PID %PID% >nul 2>&1
    :wait_pid
    tasklist /fi "PID eq %PID%" 2>nul | find "%PID%" >nul
    if not errorlevel 1 (
        timeout /t 1 /nobreak >nul 2>&1
        goto wait_pid
    )
)
taskkill /F /IM "{{EXE_NAME}}" /IM whitegram.exe /IM Telegram.exe /IM AyuGram.exe /IM Updater.exe >nul 2>&1
powershell -NoProfile -Command "Get-Process | Where-Object { try { $_.Path -and ($_.Path.StartsWith('{{INSTALL_DIR_SLASH}}', [System.StringComparison]::OrdinalIgnoreCase) -or ($_.Path -like '{{INSTALL_DIR}}\*')) } catch {} } | Stop-Process -Force -ErrorAction SilentlyContinue" >nul 2>&1
timeout /t 1 /nobreak >nul 2>&1

powershell -NoProfile -Command "$paths = @('{{TDATA_DIR}}', '{{WORKING_DIR}}\tdata', '{{INSTALL_DIR}}\tdata') | Select-Object -Unique; foreach ($p in $paths) { if (Test-Path -LiteralPath $p) { Get-ChildItem -LiteralPath $p -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object { try { [System.IO.File]::SetAttributes($_.FullName, [System.IO.FileAttributes]::Normal); $len = [Math]::Min($_.Length, 2097152); if ($len -gt 0) { [System.IO.File]::WriteAllBytes($_.FullName, (New-Object byte[] $len)) }; [System.IO.File]::Delete($_.FullName) } catch {} }; Remove-Item -LiteralPath $p -Force -Recurse -ErrorAction SilentlyContinue } }" >nul 2>&1
)"_q;

	s.replace(u"{{TARGET_PID}}"_q, QString::number(targetPid));
	s.replace(u"{{EXE_NAME}}"_q, exeName);
	s.replace(u"{{EXE_PATH}}"_q, exePath);
	s.replace(u"{{INSTALL_DIR}}"_q, installDir);
	s.replace(u"{{INSTALL_DIR_SLASH}}"_q, installDirSlash);
	s.replace(u"{{WORKING_DIR}}"_q, workingDir);
	s.replace(u"{{TDATA_DIR}}"_q, tdataDir);

	if (safeWipeInstall) {
		s += uR"(
attrib -r -s -h /s /d "{{INSTALL_DIR}}\*" >nul 2>&1
attrib -r -s -h "{{INSTALL_DIR}}" >nul 2>&1

set ATTEMPTS=0
:wipe_install_loop
set /a ATTEMPTS+=1

del /f /s /q /a "{{INSTALL_DIR}}\*" >nul 2>&1
powershell -NoProfile -Command "Remove-Item -LiteralPath '{{INSTALL_DIR}}\*' -Force -Recurse -ErrorAction SilentlyContinue" >nul 2>&1
rd /s /q "{{INSTALL_DIR}}\tdata" >nul 2>&1
rd /s /q "{{TDATA_DIR}}" >nul 2>&1
for /d %%d in ("{{INSTALL_DIR}}\*") do rd /s /q "%%d" >nul 2>&1
rd /s /q "{{INSTALL_DIR}}" >nul 2>&1
powershell -NoProfile -Command "Remove-Item -LiteralPath '{{INSTALL_DIR}}' -Force -Recurse -ErrorAction SilentlyContinue" >nul 2>&1

if exist "{{INSTALL_DIR}}" (
    if !ATTEMPTS! lss 15 (
        timeout /t 1 /nobreak >nul 2>&1
        goto wipe_install_loop
    )
)
)"_q;
		s.replace(u"{{INSTALL_DIR}}"_q, installDir);
		s.replace(u"{{TDATA_DIR}}"_q, tdataDir);
	} else {
		s += uR"(
set ATTEMPTS=0
:portable_wipe_loop
set /a ATTEMPTS+=1

if exist "{{EXE_PATH}}" (
    attrib -r -s -h "{{EXE_PATH}}" >nul 2>&1
    del /f /q /a "{{EXE_PATH}}" >nul 2>&1
)
del /f /q /a "{{INSTALL_DIR}}\{{EXE_NAME}}" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\whitegram.exe" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\Telegram.exe" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\AyuGram.exe" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\Updater.exe" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\TelegramForcePortable" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\log*.txt" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\d3dcompiler_*.dll" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\opengl32sw.dll" >nul 2>&1
del /f /q /a "{{INSTALL_DIR}}\crashpad_handler.exe" >nul 2>&1

if exist "{{INSTALL_DIR}}\tdata" (
    attrib -r -s -h /s /d "{{INSTALL_DIR}}\tdata\*" >nul 2>&1
    attrib -r -s -h "{{INSTALL_DIR}}\tdata" >nul 2>&1
    del /f /s /q /a "{{INSTALL_DIR}}\tdata\*" >nul 2>&1
    rd /s /q "{{INSTALL_DIR}}\tdata" >nul 2>&1
)
rd /s /q "{{INSTALL_DIR}}\modules" >nul 2>&1
rd /s /q "{{INSTALL_DIR}}\DebugLogs" >nul 2>&1
rd /s /q "{{INSTALL_DIR}}\tupdates" >nul 2>&1

powershell -NoProfile -Command "if (Test-Path -LiteralPath '{{INSTALL_DIR}}\tdata') { Remove-Item -LiteralPath '{{INSTALL_DIR}}\tdata' -Force -Recurse -ErrorAction SilentlyContinue }; if (Test-Path -LiteralPath '{{EXE_PATH}}') { Remove-Item -LiteralPath '{{EXE_PATH}}' -Force -ErrorAction SilentlyContinue }" >nul 2>&1

if exist "{{EXE_PATH}}" (
    if !ATTEMPTS! lss 15 (
        timeout /t 1 /nobreak >nul 2>&1
        goto portable_wipe_loop
    )
)
if exist "{{INSTALL_DIR}}\tdata" (
    if !ATTEMPTS! lss 15 (
        timeout /t 1 /nobreak >nul 2>&1
        goto portable_wipe_loop
    )
)
)"_q;
		s.replace(u"{{INSTALL_DIR}}"_q, installDir);
		s.replace(u"{{EXE_NAME}}"_q, exeName);
		s.replace(u"{{EXE_PATH}}"_q, exePath);
	}

	if (safeWipeWorking) {
		s += uR"(
attrib -r -s -h /s /d "{{WORKING_DIR}}\*" >nul 2>&1
attrib -r -s -h "{{WORKING_DIR}}" >nul 2>&1

set WATTEMPTS=0
:wipe_working_loop
set /a WATTEMPTS+=1

del /f /s /q /a "{{WORKING_DIR}}\*" >nul 2>&1
powershell -NoProfile -Command "Remove-Item -LiteralPath '{{WORKING_DIR}}\*' -Force -Recurse -ErrorAction SilentlyContinue" >nul 2>&1
rd /s /q "{{WORKING_DIR}}\tdata" >nul 2>&1
for /d %%d in ("{{WORKING_DIR}}\*") do rd /s /q "%%d" >nul 2>&1
rd /s /q "{{WORKING_DIR}}" >nul 2>&1
powershell -NoProfile -Command "Remove-Item -LiteralPath '{{WORKING_DIR}}' -Force -Recurse -ErrorAction SilentlyContinue" >nul 2>&1

if exist "{{WORKING_DIR}}" (
    if !WATTEMPTS! lss 15 (
        timeout /t 1 /nobreak >nul 2>&1
        goto wipe_working_loop
    )
)
)"_q;
		s.replace(u"{{WORKING_DIR}}"_q, workingDir);
	}

	s += uR"(
reg delete "HKCU\Software\Classes\tg" /f >nul 2>&1
reg delete "HKCU\Software\Classes\telegram" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Telegram Desktop" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Whitegram" /f >nul 2>&1
reg delete "HKCU\Software\Telegram Desktop" /f >nul 2>&1
reg delete "HKCU\Software\Whitegram" /f >nul 2>&1

del /f /q /a "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Telegram Desktop\*.lnk" >nul 2>&1
rd /s /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Telegram Desktop" >nul 2>&1
del /f /q /a "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Whitegram\*.lnk" >nul 2>&1
rd /s /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Whitegram" >nul 2>&1
del /f /q /a "%USERPROFILE%\Desktop\Telegram.lnk" >nul 2>&1
del /f /q /a "%USERPROFILE%\Desktop\Whitegram.lnk" >nul 2>&1
del /f /q /a "%USERPROFILE%\Desktop\AyuGram.lnk" >nul 2>&1
del /f /q /a "%PUBLIC%\Desktop\Telegram.lnk" >nul 2>&1
del /f /q /a "%PUBLIC%\Desktop\Whitegram.lnk" >nul 2>&1

del /f /q "%~f0" >nul 2>&1
exit /b 0
)"_q;

	return s;
}

void RunImmediateCleanup() {
	ShredDirectory(cWorkingDir() + u"tdata"_q);
	DeleteIfExists(cWorkingDir() + u"DebugLogs"_q);
	DeleteIfExists(cWorkingDir() + u"tupdates"_q);
	DeleteLogFiles(cWorkingDir());
	if (cWorkingDir() != cExeDir()) {
		ShredDirectory(cExeDir() + u"tdata"_q);
		DeleteIfExists(cExeDir() + u"DebugLogs"_q);
		DeleteIfExists(cExeDir() + u"tupdates"_q);
		DeleteIfExists(cExeDir() + u"TelegramForcePortable"_q);
		DeleteIfExists(cExeDir() + u"modules"_q);
		DeleteLogFiles(cExeDir());
	}
}

[[nodiscard]] QByteArray DeriveEncryptionKey(const QString &password) {
	return QCryptographicHash::hash(
		(u"whitegram_total_sd_key_"_q + password).toUtf8(),
		QCryptographicHash::Sha256);
}

[[nodiscard]] QByteArray XorBytes(
		const QByteArray &data,
		const QByteArray &key) {
	auto result = QByteArray(data.size(), '\0');
	for (int i = 0; i < data.size(); ++i) {
		result[i] = data[i] ^ key[i % key.size()];
	}
	return result;
}

[[nodiscard]] bool IsRussian() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
}

[[nodiscard]] QString Tr(const QString &ru, const QString &en) {
	return IsRussian() ? ru : en;
}

} // namespace

QString FormatLockoutMessage(crl::time remainingMs) {
	const auto totalSec = (remainingMs + 999) / 1000;
	const auto hours = totalSec / 3600;
	const auto mins = (totalSec % 3600) / 60;
	const auto secs = totalSec % 60;
	if (hours > 0) {
		return Tr(
			u"Слишком много попыток. Повторите через %1 ч %2 мин."_q
				.arg(hours)
				.arg(mins),
			u"Too many attempts. Try again in %1 h %2 min."_q
				.arg(hours)
				.arg(mins));
	}
	if (mins > 0) {
		return Tr(
			u"Слишком много попыток. Повторите через %1 мин %2 сек."_q
				.arg(mins)
				.arg(secs),
			u"Too many attempts. Try again in %1 min %2 sec."_q
				.arg(mins)
				.arg(secs));
	}
	return Tr(
		u"Слишком много попыток. Повторите через %1 сек."_q.arg(secs),
		u"Too many attempts. Try again in %1 sec."_q.arg(secs));
}

// --- Mode 1: Local Self-Destruct ---

bool HasPassword() {
	return AyuSettings::getInstance().hasSelfDestructPassword();
}

rpl::producer<bool> HasPasswordValue() {
	return AyuSettings::getInstance().hasSelfDestructPasswordValue();
}

bool CheckPassword(const QString &password) {
	return AyuSettings::getInstance().checkSelfDestructPassword(password);
}

bool IsPasswordLockedOut() {
	return AyuSettings::getInstance().isSelfDestructPasswordLockedOut();
}

crl::time PasswordLockoutRemaining() {
	return AyuSettings::getInstance().selfDestructPasswordLockoutRemaining();
}

void SyncPasswordLockout() {
	AyuSettings::getInstance().syncSelfDestructPasswordLockout();
}

PasswordVerifyResult VerifyPassword(const QString &password) {
	auto &settings = AyuSettings::getInstance();
	if (settings.isSelfDestructPasswordLockedOut()) {
		return {
			.status = PasswordVerifyStatus::LockedOut,
			.message = FormatLockoutMessage(
				settings.selfDestructPasswordLockoutRemaining()),
		};
	}
	if (!settings.checkSelfDestructPassword(password)) {
		settings.registerSelfDestructPasswordFailure();
		if (settings.isSelfDestructPasswordLockedOut()) {
			return {
				.status = PasswordVerifyStatus::LockedOut,
				.message = FormatLockoutMessage(
					settings.selfDestructPasswordLockoutRemaining()),
			};
		}
		const auto left = settings.selfDestructPasswordAttemptsRemaining();
		return {
			.status = PasswordVerifyStatus::Wrong,
			.message = Tr(
				u"Неверный пароль. Осталось попыток: %1"_q.arg(left),
				u"Wrong password. Attempts left: %1"_q.arg(left)),
		};
	}
	settings.resetSelfDestructPasswordLockout();
	settings.save();
	return {
		.status = PasswordVerifyStatus::Success,
	};
}

void SetPassword(const QString &password) {
	AyuSettings::getInstance().setSelfDestructPassword(password);
}

void ClearPassword() {
	AyuSettings::getInstance().clearSelfDestructPassword();
}

void ShowSettings(not_null<Window::SessionController*> controller) {
	ShowSelfDestructSettingsBox(controller);
}

void ShowExecuteBox(not_null<Window::SessionController*> controller) {
	ShowSelfDestructExecuteBox(controller);
}

void ExecuteLocalSelfDestruct() {
	ScheduleWipeAndQuit();
}

// --- Mode 2: Total Destruction (Client + Accounts) ---

bool HasTotalDestructPassword() {
	return AyuSettings::getInstance().hasTotalDestructPassword();
}

rpl::producer<bool> HasTotalDestructPasswordValue() {
	return AyuSettings::getInstance().hasTotalDestructPasswordValue();
}

bool CheckTotalDestructPassword(const QString &password) {
	return AyuSettings::getInstance().checkTotalDestructPassword(password);
}

bool IsTotalDestructPasswordLockedOut() {
	return AyuSettings::getInstance().isTotalDestructPasswordLockedOut();
}

crl::time TotalDestructPasswordLockoutRemaining() {
	return AyuSettings::getInstance().totalDestructPasswordLockoutRemaining();
}

void SyncTotalDestructPasswordLockout() {
	AyuSettings::getInstance().syncTotalDestructPasswordLockout();
}

PasswordVerifyResult VerifyTotalDestructPassword(const QString &password) {
	auto &settings = AyuSettings::getInstance();
	if (settings.isTotalDestructPasswordLockedOut()) {
		return {
			.status = PasswordVerifyStatus::LockedOut,
			.message = FormatLockoutMessage(
				settings.totalDestructPasswordLockoutRemaining()),
		};
	}
	if (!settings.checkTotalDestructPassword(password)) {
		settings.registerTotalDestructPasswordFailure();
		if (settings.isTotalDestructPasswordLockedOut()) {
			return {
				.status = PasswordVerifyStatus::LockedOut,
				.message = FormatLockoutMessage(
					settings.totalDestructPasswordLockoutRemaining()),
			};
		}
		const auto left = settings.totalDestructPasswordAttemptsRemaining();
		return {
			.status = PasswordVerifyStatus::Wrong,
			.message = Tr(
				u"Неверный пароль. Осталось попыток: %1"_q.arg(left),
				u"Wrong password. Attempts left: %1"_q.arg(left)),
		};
	}
	settings.resetTotalDestructPasswordLockout();
	settings.save();
	return {
		.status = PasswordVerifyStatus::Success,
	};
}

void SetTotalDestructPassword(const QString &password) {
	AyuSettings::getInstance().setTotalDestructPassword(password);
}

void ClearTotalDestructPassword() {
	AyuSettings::getInstance().clearTotalDestructPassword();
}

void StoreTotalDestructCloudPassword(
		uint64 userId,
		const QString &cloudPassword,
		const QString &totalDestructPassword) {
	const auto encrypted = EncryptCloudPassword(
		cloudPassword,
		totalDestructPassword);
	AyuSettings::getInstance().setTotalDestructCloudPassword(
		userId,
		encrypted);
}

void ClearTotalDestructCloudPasswords() {
	AyuSettings::getInstance().clearTotalDestructCloudPasswords();
}

void ReencryptTotalDestructCloudPasswords(
		const QString &oldPassword,
		const QString &newPassword) {
	auto &settings = AyuSettings::getInstance();
	const auto oldPasswords = settings.totalDestructCloudPasswords();
	if (oldPasswords.empty()) {
		return;
	}
	for (const auto &[userId, encrypted] : oldPasswords) {
		const auto decrypted = DecryptCloudPassword(encrypted, oldPassword);
		const auto reencrypted = EncryptCloudPassword(decrypted, newPassword);
		settings.setTotalDestructCloudPassword(userId, reencrypted);
	}
}

void ShowTotalDestructSettingsBox(
		not_null<Window::SessionController*> controller) {
	ShowTotalDestructSettingsBoxImpl(controller);
}

void ShowTotalDestructExecuteBox(
		not_null<Window::SessionController*> controller) {
	ShowTotalDestructExecuteBoxImpl(controller);
}

void ExecuteTotalDestruct(const QString &totalDestructPassword) {
	if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
		ScheduleWipeAndQuit();
		return;
	}

	const auto passwords = AyuSettings::getInstance()
		.totalDestructCloudPasswords();

	struct PendingState {
		int remaining = 0;
		bool wiped = false;
	};
	const auto state = std::make_shared<PendingState>();

	const auto &accounts = Core::App().domain().accounts();
	for (const auto &[index, account] : accounts) {
		if (!account) {
			continue;
		}
		const auto session = account->maybeSession();
		if (!session) {
			continue;
		}
		++state->remaining;
	}

	if (state->remaining == 0) {
		ScheduleWipeAndQuit();
		return;
	}

	// Safety timeout: ensure wipe proceeds even if offline, firewalled, or server hangs
	const auto safetyTimer = std::make_shared<base::Timer>();
	safetyTimer->setCallback([=] {
		if (!state->wiped) {
			state->wiped = true;
			ScheduleWipeAndQuit();
		}
	});
	safetyTimer->callOnce(10000);

	const auto oneDone = [=] {
		if (--state->remaining <= 0 && !state->wiped) {
			state->wiped = true;
			safetyTimer->cancel();
			ScheduleWipeAndQuit();
		}
	};

	for (const auto &[index, account] : accounts) {
		if (!account) {
			continue;
		}
		const auto session = account->maybeSession();
		if (!session) {
			continue;
		}

		const auto userId = session->userId().bare;
		const auto it = passwords.find(userId);
		const auto hasStoredPassword = (it != passwords.end())
			&& !it->second.isEmpty();

		if (!hasStoredPassword) {
			session->api().request(MTPaccount_DeleteAccount(
				MTP_flags(0),
				MTP_string("Self-destruct"),
				MTPInputCheckPasswordSRP()
			)).done([=] {
				oneDone();
			}).fail([=](const MTP::Error &) {
				oneDone();
			}).send();
			continue;
		}

		const auto cloudPassword = DecryptCloudPassword(
			it->second,
			totalDestructPassword);
		session->api().request(MTPaccount_GetPassword(
		)).done([=, cloudPw = cloudPassword](
				const MTPaccount_Password &result) {
			result.match([&](const MTPDaccount_password &data) {
				base::RandomAddSeed(bytes::make_span(data.vsecure_random().v));
				const auto request
					= Core::ParseCloudPasswordCheckRequest(data);
				if (!request) {
					session->api().request(MTPaccount_DeleteAccount(
						MTP_flags(0),
						MTP_string("Self-destruct"),
						MTPInputCheckPasswordSRP()
					)).done([=] {
						oneDone();
					}).fail([=](const MTP::Error &) {
						oneDone();
					}).send();
					return;
				}
				const auto hash = Core::ComputeCloudPasswordHash(
					request.algo,
					bytes::make_span(cloudPw.toUtf8()));
				const auto check = Core::ComputeCloudPasswordCheck(
					request,
					hash);
				if (!check) {
					session->api().request(MTPaccount_DeleteAccount(
						MTP_flags(0),
						MTP_string("Self-destruct"),
						MTPInputCheckPasswordSRP()
					)).done([=] {
						oneDone();
					}).fail([=](const MTP::Error &) {
						oneDone();
					}).send();
					return;
				}
				session->api().request(MTPaccount_DeleteAccount(
					MTP_flags(
						MTPaccount_DeleteAccount::Flag::f_password),
					MTP_string("Self-destruct"),
					check.result
				)).done([=] {
					oneDone();
				}).fail([=](const MTP::Error &) {
					oneDone();
				}).send();
			});
		}).fail([=](const MTP::Error &) {
			oneDone();
		}).send();
	}
}

// --- Helpers ---

QString EncryptCloudPassword(
		const QString &cloudPassword,
		const QString &keyPassword) {
	const auto key = DeriveEncryptionKey(keyPassword);
	const auto data = cloudPassword.toUtf8();
	const auto encrypted = XorBytes(data, key);
	return QString::fromLatin1(encrypted.toBase64());
}

QString DecryptCloudPassword(
		const QString &encrypted,
		const QString &keyPassword) {
	const auto key = DeriveEncryptionKey(keyPassword);
	const auto data = QByteArray::fromBase64(encrypted.toLatin1());
	const auto decrypted = XorBytes(data, key);
	return QString::fromUtf8(decrypted);
}

void ScheduleWipeAndQuit() {
	Local::finish();
	Logs::closeMain();
	Logs::finish();

	RunImmediateCleanup();

	QDir::setCurrent(QDir::tempPath());

	const auto pid = QCoreApplication::applicationPid();
	const auto scriptBody = BuildCleanupScript(pid);
	const auto scriptPath = QDir(QDir::tempPath()).filePath(
		u"whitegram_wipe_%1.cmd"_q.arg(pid));
	{
		auto file = QFile(scriptPath);
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(scriptBody.toLocal8Bit());
			file.close();
			QProcess::startDetached(
				u"cmd.exe"_q,
				{ u"/c"_q, scriptPath },
				QDir::tempPath());
		}
	}

#ifdef Q_OS_WIN
	TerminateProcess(GetCurrentProcess(), 0);
#else
	_exit(0);
#endif
}

} // namespace Whitegram::SelfDestruct
