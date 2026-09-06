#pragma once

#include "base/basic_types.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Whitegram::SelfDestruct {

enum class PasswordVerifyStatus {
	Success,
	Wrong,
	LockedOut,
};

struct PasswordVerifyResult {
	PasswordVerifyStatus status = PasswordVerifyStatus::Wrong;
	QString message;
};

[[nodiscard]] QString FormatLockoutMessage(crl::time remainingMs);

// --- Mode 1: Local Self-Destruct (Client files only) ---
[[nodiscard]] bool HasPassword();
[[nodiscard]] rpl::producer<bool> HasPasswordValue();
[[nodiscard]] bool CheckPassword(const QString &password);
[[nodiscard]] bool IsPasswordLockedOut();
[[nodiscard]] crl::time PasswordLockoutRemaining();
void SyncPasswordLockout();
[[nodiscard]] PasswordVerifyResult VerifyPassword(const QString &password);
void SetPassword(const QString &password);
void ClearPassword();
void ShowSettings(not_null<Window::SessionController*> controller);
void ShowExecuteBox(not_null<Window::SessionController*> controller);
void ExecuteLocalSelfDestruct();

// --- Mode 2: Total Destruction (Client files + Telegram accounts) ---
[[nodiscard]] bool HasTotalDestructPassword();
[[nodiscard]] rpl::producer<bool> HasTotalDestructPasswordValue();
[[nodiscard]] bool CheckTotalDestructPassword(const QString &password);
[[nodiscard]] bool IsTotalDestructPasswordLockedOut();
[[nodiscard]] crl::time TotalDestructPasswordLockoutRemaining();
void SyncTotalDestructPasswordLockout();
[[nodiscard]] PasswordVerifyResult VerifyTotalDestructPassword(
	const QString &password);
void SetTotalDestructPassword(const QString &password);
void ClearTotalDestructPassword();

void StoreTotalDestructCloudPassword(
	uint64 userId,
	const QString &cloudPassword,
	const QString &totalDestructPassword);
void ClearTotalDestructCloudPasswords();
void ReencryptTotalDestructCloudPasswords(
	const QString &oldPassword,
	const QString &newPassword);

void ShowTotalDestructSettingsBox(
	not_null<Window::SessionController*> controller);
void ShowTotalDestructExecuteBox(
	not_null<Window::SessionController*> controller);
void ExecuteTotalDestruct(const QString &totalDestructPassword);

// --- Helpers ---
[[nodiscard]] QString EncryptCloudPassword(
	const QString &cloudPassword,
	const QString &keyPassword);
[[nodiscard]] QString DecryptCloudPassword(
	const QString &encrypted,
	const QString &keyPassword);
void ScheduleWipeAndQuit();

} // namespace Whitegram::SelfDestruct
