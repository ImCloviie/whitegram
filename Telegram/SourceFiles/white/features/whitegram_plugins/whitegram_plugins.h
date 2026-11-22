// This is the source code of Whitegram Desktop.
//
// Whitegram Python plugin engine: embedded CPython runtime with a
// "whitegram_host.py" dispatcher, ported from the exDesktop plugin idea.
//
// Copyright (c) 2026 Whitegram Team.

#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "ui/text/text_entity.h"

#include <QtCore/QJsonValue>

class History;
class HistoryItem;
class PeerData;

namespace Whitegram::Plugins {

inline constexpr auto kSdkVersion = "1.0.0";

// Is the engine built into this binary (CMake found Python development).
[[nodiscard]] bool Compiled();

// The Python runtime and the host script are present next to the exe.
[[nodiscard]] bool Available();

// The engine finished starting and dispatch works.
[[nodiscard]] bool Started();

[[nodiscard]] QString RuntimeError();

// Python version detected at build time, e.g. "3.14".
[[nodiscard]] QString PythonVersion();

void Start();
void Shutdown();

QJsonValue Call(
	const QString &method,
	const QJsonObject &args = QJsonObject());

// Slash commands registry is kept inside the host, message hooks are
// checked in ApiWrap::sendMessage.
enum class SendStrategy {
	Default,
	Cancel,
	Modify,
};

struct SendResult {
	SendStrategy strategy = SendStrategy::Default;
	QString text;
	EntitiesInText entities;
};

[[nodiscard]] SendResult OnSendMessage(
	not_null<History*> history,
	const QString &text,
	MsgId replyTo);

// True while the engine itself sends a message (prevents hook recursion).
[[nodiscard]] bool Bypassing();

void NotifyNewMessage(not_null<HistoryItem*> item);
void NotifyAppEvent(const QString &name);

struct MenuItem {
	QString id;
	QString text;
};

[[nodiscard]] std::vector<MenuItem> MenuItems(const QString &type);
void ClickMenuItem(const QString &id, QJsonObject context);

// Ctrl+Alt+P draft transformation, returns empty string when no plugin
// transformed the text. When transformed, shows a toast with the plugin name.
QString TransformDraft(const QString &text);

[[nodiscard]] int64 DialogId(not_null<PeerData*> peer);

[[nodiscard]] rpl::producer<> listChanges();
void notifyListChanged();

// Engine settings (tdata/whitegram/plugins/engine.ini).
[[nodiscard]] bool EngineEnabled();
void SetEngineEnabled(bool enabled);
[[nodiscard]] bool SafeMode();
void SetSafeMode(bool enabled);
[[nodiscard]] bool AllowUnknownSources();
void SetAllowUnknownSources(bool enabled);

// tdata/whitegram/plugins/
[[nodiscard]] QString DataPath();
[[nodiscard]] QString PluginsDir();
[[nodiscard]] QString SettingsDir();

} // namespace Whitegram::Plugins
