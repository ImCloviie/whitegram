/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <vector>

class QString;

namespace MTP::details {

struct BypassHostRequest {
	int dc = 2;
	bool isMedia = false;
	bool isTestDc = false;
};

using BypassHostsResolver = Fn<std::vector<QString>(BypassHostRequest)>;
using BypassHostResultHandler = Fn<void(const QString &host, bool success)>;
using BypassHostsExhaustedHandler = Fn<void()>;

void SetBypassHostsResolver(BypassHostsResolver resolver);
void SetBypassHostResultHandler(BypassHostResultHandler handler);
void SetBypassHostsExhaustedHandler(BypassHostsExhaustedHandler handler);

[[nodiscard]] std::vector<QString> ResolveBypassHosts(
	BypassHostRequest request);
void NotifyBypassHostResult(const QString &host, bool success);
void NotifyBypassHostsExhausted();

[[nodiscard]] std::vector<QString> DefaultBypassHosts(
	BypassHostRequest request);

} // namespace MTP::details
