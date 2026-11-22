// Whitegram bypass 2: bundled Flowseal/tg-ws-proxy (MIT) as a local MTProto proxy.
#pragma once

#include "rpl/producer.h"

namespace WhiteFeatures::TgWsProxy {

enum class State {
	Off,
	Starting,
	Running,
	Failed,
};

struct Status {
	State state = State::Off;
	int port = 0;
	QString error;

	friend inline bool operator==(const Status &, const Status &) = default;
};

void init();
void reevaluate();
void shutdown();

[[nodiscard]] Status status();
[[nodiscard]] rpl::producer<Status> statusValue();
[[nodiscard]] QString executablePath();

// Stops proxies of this install left by an older version; true if any.
bool KillOrphans();

} // namespace AyuFeatures::TgWsProxy
