// This file is part of Whitegram for Desktop.
#include "ayu/ui/utils/thread_cpu.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

#include <cstdint>

namespace Ayu::Ui {

double ThreadCpuMs() {
#ifdef Q_OS_WIN
	auto creation = FILETIME();
	auto exit = FILETIME();
	auto kernel = FILETIME();
	auto user = FILETIME();
	if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
		return 0.;
	}
	const auto value = [](const FILETIME &time) {
		return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
	};
	return (value(kernel) + value(user)) / 10000.;
#else // Q_OS_WIN
	return 0.;
#endif // Q_OS_WIN
}

} // namespace Ayu::Ui
