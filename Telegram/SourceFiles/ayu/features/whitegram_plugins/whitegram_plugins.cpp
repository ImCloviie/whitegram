// This is the source code of Whitegram Desktop.
//
// Whitegram Python plugin engine.
// Copyright (c) 2026 Whitegram Team.

#include "ayu/features/whitegram_plugins/whitegram_plugins.h"

#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "crl/crl.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "rpl/never.h"
#include "settings.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <algorithm>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

#ifdef WG_WITH_PYTHON

#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")

#include <atomic>
#include <chrono>
#include <deque>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

namespace Whitegram::Plugins {
namespace {

struct State {
	bool started = false;
	bool interpInit = false;
	QString error;
	PyThreadState *mainThread = nullptr;
	PyObject *dispatch = nullptr;
	rpl::event_stream<> listChanges;
	bool eventInFlight = false;
	std::deque<QJsonObject> recent;
};

constexpr auto kHangTimeout = crl::time(10000);
std::atomic<crl::time> CallStarted = 0;
std::atomic<bool> HangReported = false;
std::atomic<bool> WatchdogRunning = false;

State &Instance() {
	static auto result = State();
	return result;
}

bool BypassFlag = false;
std::atomic<int64> CurrentDialog = 0;

class Gil final {
public:
	Gil() : _state(PyGILState_Ensure()) {
	}
	~Gil() {
		PyGILState_Release(_state);
	}

private:
	PyGILState_STATE _state;

};

[[nodiscard]] QString FromPy(PyObject *object) {
	if (!object || !PyUnicode_Check(object)) {
		return QString();
	}
	Py_ssize_t size = 0;
	const auto data = PyUnicode_AsUTF8AndSize(object, &size);
	return data ? QString::fromUtf8(data, int(size)) : QString();
}

[[nodiscard]] Window::SessionController *ActiveController() {
	if (const auto window = Core::App().activePrimaryWindow()) {
		return window->sessionController();
	}
	return nullptr;
}

void ShowToast(const QString &text) {
	if (const auto controller = ActiveController()) {
		controller->showToast(text);
	}
}

// Settings and directories.

[[nodiscard]] QString EngineIniPath() {
	return DataPath() + u"engine.ini"_q;
}

[[nodiscard]] QString RuntimePath() {
	return cExeDir() + u"python/"_q;
}

[[nodiscard]] QString HostScriptPath() {
	return RuntimePath() + u"whitegram/whitegram_host.py"_q;
}

[[nodiscard]] QString StartMarkerPath() {
	return DataPath() + u"starting.lock"_q;
}

[[nodiscard]] QString HangMarkerPath() {
	return DataPath() + u"plugin_hang.lock"_q;
}

void EnterAutoSafeMode(bool hang) {
	if (SafeMode()) {
		return;
	}
	SetSafeMode(true);
	base::call_delayed(3000, [=] {
		const auto window = Core::App().activePrimaryWindow();
		const auto controller = window ? window->sessionController() : nullptr;
		if (!controller) {
			return;
		}
		controller->show(Ui::MakeConfirmBox({
			.text = (hang
				? QString::fromUtf8(
					"Плагин заблокировал интерфейс. "
					"Whitegram перезапустит плагины в безопасном режиме.")
				: QString::fromUtf8(
					"Похоже, плагин вызвал сбой Whitegram. "
					"Плагины запущены в безопасном режиме.")),
			.confirmed = [](Fn<void()> close) { close(); },
			.confirmText = QString::fromUtf8("Понятно"),
			.title = QString::fromUtf8("Безопасный режим"),
		}));
	});
}

void StartWatchdog() {
	if (WatchdogRunning.exchange(true)) {
		return;
	}
	const auto path = HangMarkerPath();
	std::thread([path] {
		while (WatchdogRunning.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			const auto started = CallStarted.load();
			if (started
				&& !HangReported.load()
				&& (crl::now() - started) > kHangTimeout) {
				HangReported = true;
				auto marker = QFile(path);
				if (marker.open(QIODevice::WriteOnly)) {
					marker.write("1");
				}
			}
		}
	}).detach();
}

// Entities.

[[nodiscard]] EntityType TypeFromKind(const QString &kind) {
	if (kind == u"bold"_q) {
		return EntityType::Bold;
	} else if (kind == u"italic"_q) {
		return EntityType::Italic;
	} else if (kind == u"underline"_q) {
		return EntityType::Underline;
	} else if (kind == u"strike"_q || kind == u"strikethrough"_q) {
		return EntityType::StrikeOut;
	} else if (kind == u"code"_q) {
		return EntityType::Code;
	} else if (kind == u"pre"_q) {
		return EntityType::Pre;
	} else if (kind == u"blockquote"_q) {
		return EntityType::Blockquote;
	} else if (kind == u"spoiler"_q) {
		return EntityType::Spoiler;
	} else if (kind == u"url"_q || kind == u"text_link"_q) {
		return EntityType::CustomUrl;
	} else if (kind == u"custom_emoji"_q) {
		return EntityType::CustomEmoji;
	}
	return EntityType::Invalid;
}

[[nodiscard]] EntitiesInText EntitiesFromJsonArray(const QJsonArray &array) {
	auto result = EntitiesInText();
	for (const auto &value : array) {
		const auto entry = value.toObject();
		if (entry.isEmpty()) {
			continue;
		}
		const auto type = TypeFromKind(entry.value(u"type"_q).toString());
		const auto length = entry.value(u"length"_q).toInt();
		if (type == EntityType::Invalid || length <= 0) {
			continue;
		}
		auto data = QString();
		if (entry.contains(u"url"_q)) {
			data = entry.value(u"url"_q).toString();
		} else if (entry.contains(u"language"_q)) {
			data = entry.value(u"language"_q).toString();
		}
		result.push_back({
			type,
			entry.value(u"offset"_q).toInt(),
			length,
			data,
		});
	}
	return result;
}

// Message sending from plugins.

[[nodiscard]] PeerData *ResolveDialog(
		not_null<Main::Session*> session,
		int64 dialogId) {
	const auto owner = &session->data();
	if (dialogId > 0) {
		if (const auto user = owner->peerLoaded(
				peerFromUser(UserId(dialogId)))) {
			return user;
		}
		return owner->peer(peerFromUser(UserId(dialogId)));
	} else if (dialogId < 0) {
		const auto bare = BareId(-dialogId);
		if (const auto channel = owner->peerLoaded(
				peerFromChannel(ChannelId(bare)))) {
			return channel;
		}
		if (const auto chat = owner->peerLoaded(peerFromChat(ChatId(bare)))) {
			return chat;
		}
		return owner->peer(peerFromChannel(ChannelId(bare)));
	}
	return nullptr;
}

void SendFromPlugin(
		int64 dialogId,
		const QString &text,
		const QString &entities,
		int64 replyTo) {
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	const auto session = &controller->session();
	const auto peer = (dialogId != 0)
		? ResolveDialog(session, dialogId)
		: (controller->activeChatCurrent().peer());
	if (!peer) {
		ShowToast(QString::fromUtf8("Не удалось найти чат для сообщения плагина."));
		return;
	}
	const auto history = session->data().history(peer);
	auto message = Api::MessageToSend(Api::SendAction(history));
	auto parsed = TextWithEntities{
		text,
		EntitiesFromJsonArray(
			QJsonDocument::fromJson(entities.toUtf8()).array()),
	};
	message.textWithTags = TextWithTags{
		parsed.text,
		TextUtilities::ConvertEntitiesToTextTags(parsed.entities),
	};
	if (replyTo > 0) {
		message.action.replyTo.messageId = FullMsgId(
			peer->id,
			MsgId(replyTo));
	}
	BypassFlag = true;
	session->api().sendMessage(std::move(message));
	BypassFlag = false;
}

void EditFromPlugin(
		int64 dialogId,
		int64 msgId,
		const QString &text,
		const QString &entities) {
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	const auto session = &controller->session();
	const auto peer = ResolveDialog(session, dialogId);
	if (!peer) {
		return;
	}
	const auto item = session->data().message(
		FullMsgId(peer->id, MsgId(msgId)));
	if (!item) {
		ShowToast(QString::fromUtf8("Сообщение не найдено."));
		return;
	}
	Api::EditTextMessage(
		item,
		TextWithEntities{
			text,
			EntitiesFromJsonArray(
				QJsonDocument::fromJson(entities.toUtf8()).array()),
		},
		{},
		{},
		nullptr,
		nullptr,
		false);
}

void ShowAlert(const QString &payload) {
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	const auto data = QJsonDocument::fromJson(payload.toUtf8()).object();
	const auto id = data.value(u"id"_q).toInt();
	const auto title = data.value(u"title"_q).toString();
	const auto message = data.value(u"message"_q).toString();
	const auto buttons = data.value(u"buttons"_q).toArray();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto answered = std::make_shared<bool>(false);
		const auto answer = [=](int which) {
			if (*answered) {
				return;
			}
			*answered = true;
			Call(u"alert_click"_q, QJsonObject{
				{ u"id"_q, id },
				{ u"which"_q, which },
			});
		};
		if (!title.isEmpty()) {
			box->setTitle(rpl::single(title));
		}
		if (!message.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(message),
				st::boxLabel))->setSelectable(true);
		}
		for (const auto &value : buttons) {
			const auto entry = value.toObject();
			const auto which = entry.value(u"which"_q).toInt();
			box->addButton(
				rpl::single(entry.value(u"text"_q).toString()),
				[=] {
					answer(which);
					box->closeBox();
				});
		}
		if (buttons.isEmpty()) {
			box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		}
	}));
}

// Native module methods (Python -> C++).

PyObject *NativeLog(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	LOG(("WhitegramPlugins: %1").arg(QString::fromUtf8(text)));
	return Py_BuildValue("");
}

PyObject *NativeToast(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(text);
	crl::on_main([=] { ShowToast(value); });
	return Py_BuildValue("");
}

PyObject *NativeOpenUrl(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(text);
	crl::on_main([=] { UrlClickHandler::Open(value); });
	return Py_BuildValue("");
}

PyObject *NativeCopy(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(text);
	crl::on_main([=] { QGuiApplication::clipboard()->setText(value); });
	return Py_BuildValue("");
}

PyObject *NativeSendMessage(PyObject *self, PyObject *args) {
	long long dialog = 0;
	const char *text = nullptr;
	const char *entities = nullptr;
	long long reply = 0;
	if (!PyArg_ParseTuple(args, "LssL", &dialog, &text, &entities, &reply)) {
		return nullptr;
	}
	const auto textValue = QString::fromUtf8(text);
	const auto entitiesValue = QString::fromUtf8(entities);
	crl::on_main([=] {
		SendFromPlugin(dialog, textValue, entitiesValue, reply);
	});
	return Py_BuildValue("");
}

PyObject *NativeEditMessage(PyObject *self, PyObject *args) {
	long long dialog = 0;
	long long msgId = 0;
	const char *text = nullptr;
	const char *entities = nullptr;
	if (!PyArg_ParseTuple(args, "LLss", &dialog, &msgId, &text, &entities)) {
		return nullptr;
	}
	const auto textValue = QString::fromUtf8(text);
	const auto entitiesValue = QString::fromUtf8(entities);
	crl::on_main([=] {
		EditFromPlugin(dialog, msgId, textValue, entitiesValue);
	});
	return Py_BuildValue("");
}

PyObject *NativeRunOnMain(PyObject *self, PyObject *args) {
	PyObject *callable = nullptr;
	int delay = 0;
	if (!PyArg_ParseTuple(args, "O|i", &callable, &delay)) {
		return nullptr;
	}
	if (!PyCallable_Check(callable)) {
		return Py_BuildValue("");
	}
	Py_INCREF(callable);
	const auto run = [=] {
		auto gil = Gil();
		const auto result = PyObject_CallNoArgs(callable);
		if (!result) {
			PyErr_Print();
		}
		Py_XDECREF(result);
		Py_DECREF(callable);
	};
	if (delay > 0) {
		crl::on_main([=] { base::call_delayed(delay, run); });
	} else {
		crl::on_main(run);
	}
	return Py_BuildValue("");
}

PyObject *NativeShowAlert(PyObject *self, PyObject *args) {
	const char *payload = nullptr;
	if (!PyArg_ParseTuple(args, "s", &payload)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(payload);
	crl::on_main([=] { ShowAlert(value); });
	return Py_BuildValue("");
}

PyObject *NativeSettingsChanged(PyObject *self, PyObject *args) {
	const char *id = nullptr;
	if (!PyArg_ParseTuple(args, "s", &id)) {
		return nullptr;
	}
	crl::on_main([=] { Instance().listChanges.fire({}); });
	return Py_BuildValue("");
}

PyObject *NativeCurrentPeer(PyObject *self, PyObject *args) {
	if (const auto controller = ActiveController()) {
		if (const auto peer = controller->activeChatCurrent().peer()) {
			return PyLong_FromLongLong(DialogId(peer));
		}
	}
	return PyLong_FromLongLong(CurrentDialog.load());
}

PyObject *NativeSelfId(PyObject *self, PyObject *args) {
	if (const auto controller = ActiveController()) {
		return PyLong_FromLongLong(
			int64(controller->session().userId().bare));
	}
	return PyLong_FromLongLong(0);
}

PyObject *NativePeerInfo(PyObject *self, PyObject *args) {
	long long dialog = 0;
	if (!PyArg_ParseTuple(args, "L", &dialog)) {
		return nullptr;
	}
	const auto controller = ActiveController();
	const auto peer = controller
		? ResolveDialog(&controller->session(), dialog)
		: nullptr;
	if (!peer) {
		return Py_BuildValue("s", "{}");
	}
	auto info = QJsonObject{
		{ u"id"_q, double(dialog) },
		{ u"dialog_id"_q, double(dialog) },
		{ u"type"_q, peer->isUser()
			? u"user"_q
			: peer->isChat() ? u"chat"_q : u"channel"_q },
		{ u"name"_q, peer->name() },
		{ u"username"_q, peer->username() },
	};
	if (const auto user = peer->asUser()) {
		info.insert(u"phone"_q, user->phone());
		info.insert(u"bot"_q, user->isBot());
		info.insert(u"bare_id"_q, QString::number(peerToUser(peer->id).bare));
		info.insert(u"self"_q, user->isSelf());
	} else if (const auto channel = peer->asChannel()) {
		info.insert(u"bare_id"_q,
			QString::number(peerToChannel(peer->id).bare));
		info.insert(u"megagroup"_q, channel->isMegagroup());
	} else if (const auto chat = peer->asChat()) {
		info.insert(u"bare_id"_q, QString::number(peerToChat(peer->id).bare));
	}
	return Py_BuildValue("s",
		QJsonDocument(info).toJson(QJsonDocument::Compact).constData());
}

PyObject *NativeGetHistory(PyObject *self, PyObject *args) {
	long long dialog = 0;
	int limit = 20;
	if (!PyArg_ParseTuple(args, "L|i", &dialog, &limit)) {
		return nullptr;
	}
	limit = std::clamp(limit, 1, 500);
	auto &state = Instance();
	auto array = QJsonArray();
	for (auto it = state.recent.rbegin()
		; it != state.recent.rend() && array.size() < limit
		; ++it) {
		if (dialog == 0
			|| int64(it->value(u"dialog_id"_q).toDouble()) == dialog) {
			array.append(*it);
		}
	}
	return Py_BuildValue("s",
		QJsonDocument(array).toJson(QJsonDocument::Compact).constData());
}

PyMethodDef NativeMethods[] = {
	{ "log", NativeLog, METH_VARARGS, nullptr },
	{ "toast", NativeToast, METH_VARARGS, nullptr },
	{ "open_url", NativeOpenUrl, METH_VARARGS, nullptr },
	{ "copy", NativeCopy, METH_VARARGS, nullptr },
	{ "send_message", NativeSendMessage, METH_VARARGS, nullptr },
	{ "edit_message", NativeEditMessage, METH_VARARGS, nullptr },
	{ "run_on_main", NativeRunOnMain, METH_VARARGS, nullptr },
	{ "show_alert", NativeShowAlert, METH_VARARGS, nullptr },
	{ "settings_changed", NativeSettingsChanged, METH_VARARGS, nullptr },
	{ "current_peer", NativeCurrentPeer, METH_VARARGS, nullptr },
	{ "self_id", NativeSelfId, METH_VARARGS, nullptr },
	{ "peer_info", NativePeerInfo, METH_VARARGS, nullptr },
	{ "get_history", NativeGetHistory, METH_VARARGS, nullptr },
	{ nullptr, nullptr, 0, nullptr },
};

PyModuleDef NativeModule = {
	PyModuleDef_HEAD_INIT,
	"_whitegram",
	nullptr,
	-1,
	NativeMethods,
};

PyObject *InitNativeModule() {
	return PyModule_Create(&NativeModule);
}

[[nodiscard]] bool InitInterpreter(const QString &runtime) {
	auto config = PyConfig();
#ifdef Q_OS_WIN
	PyConfig_InitIsolatedConfig(&config);
#else
	PyConfig_InitPythonConfig(&config);
#endif
	const auto finish = [&](PyStatus status) {
		if (PyStatus_Exception(status)) {
			Instance().error = QString::fromUtf8(status.err_msg
				? status.err_msg
				: "Python init failed");
			PyConfig_Clear(&config);
			return false;
		}
		return true;
	};
	const auto home = QDir::toNativeSeparators(runtime).toStdWString();
	if (!finish(PyConfig_SetString(&config, &config.home, home.c_str()))) {
		return false;
	}
	config.module_search_paths_set = 1;
	const auto zipName = QString(WG_PYTHON_DLL_NAME)
		.replace(u".dll"_q, u".zip"_q);
	const auto paths = QStringList{
		runtime + u"whitegram"_q,
		runtime,
		runtime + zipName,
		runtime + u"Lib"_q,
		runtime + u"site-packages"_q,
	};
	for (const auto &path : paths) {
		const auto value = QDir::toNativeSeparators(path).toStdWString();
		if (!finish(PyWideStringList_Append(
				&config.module_search_paths,
				value.c_str()))) {
			return false;
		}
	}
	config.write_bytecode = 0;
	config.install_signal_handlers = 0;
	if (!finish(Py_InitializeFromConfig(&config))) {
		return false;
	}
	PyConfig_Clear(&config);
	return true;
}

[[nodiscard]] QJsonObject MessageToJson(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto from = item->from();
	return QJsonObject{
		{ u"id"_q, double(item->id.bare) },
		{ u"dialog_id"_q, double(DialogId(peer)) },
		{ u"out"_q, item->out() },
		{ u"date"_q, double(item->date()) },
		{ u"sender_id"_q, double(from ? DialogId(from) : 0) },
		{ u"text"_q, item->originalText().text },
	};
}

} // namespace

bool Compiled() {
	return true;
}

bool Available() {
#ifdef Q_OS_WIN
	return QFile::exists(RuntimePath() + WG_PYTHON_DLL_NAME)
		&& QFile::exists(HostScriptPath());
#else
	return QFile::exists(HostScriptPath());
#endif
}

bool Started() {
	return Instance().started;
}

QString RuntimeError() {
	return Instance().error;
}

QString PythonVersion() {
	return QString::fromUtf8(WG_PYTHON_VERSION_STR);
}

void Start() {
	auto &state = Instance();
	if (state.started || !EngineEnabled()) {
		return;
	}
	QDir().mkpath(DataPath());
	if (!Available()) {
		state.error = QString::fromUtf8("Python рантайм не найден");
		return;
	}
#ifdef Q_OS_WIN
	const auto dll = QDir::toNativeSeparators(
		RuntimePath() + WG_PYTHON_DLL_NAME).toStdWString();
	if (!LoadLibraryW(dll.c_str())) {
		state.error = QString::fromUtf8(
			WG_PYTHON_DLL_NAME " не может быть загружена");
		return;
	}
#endif // Q_OS_WIN
	if (!state.interpInit) {
		if (PyImport_AppendInittab("_whitegram", InitNativeModule) == -1) {
			state.error = QString::fromUtf8("Ошибка регистрации нативного модуля");
			return;
		}
		if (!InitInterpreter(RuntimePath())) {
			LOG(("WhitegramPlugins Error: %1").arg(state.error));
			return;
		}
		const auto module = PyImport_ImportModule("whitegram_host");
		if (!module) {
			PyErr_Print();
			state.error = QString::fromUtf8("Ошибка загрузки whitegram_host");
			state.mainThread = PyEval_SaveThread();
			state.interpInit = true;
			return;
		}
		state.dispatch = PyObject_GetAttrString(module, "dispatch");
		Py_DECREF(module);
		state.mainThread = PyEval_SaveThread();
		state.interpInit = true;
	}
	state.started = (state.dispatch != nullptr);
	if (!state.started) {
		return;
	}
	QDir().mkpath(PluginsDir());
	QDir().mkpath(SettingsDir());
	if (QFile::exists(HangMarkerPath())) {
		QFile::remove(HangMarkerPath());
		EnterAutoSafeMode(true);
	}
	if (QFile::exists(StartMarkerPath())) {
		EnterAutoSafeMode(false);
	}
	{
		auto marker = QFile(StartMarkerPath());
		if (marker.open(QIODevice::WriteOnly)) {
			marker.write("1");
		}
	}
	base::call_delayed(30000, [] {
		QFile::remove(StartMarkerPath());
	});
	Call(u"init"_q, QJsonObject{
		{ u"plugins"_q, PluginsDir() },
		{ u"settings"_q, SettingsDir() },
		{ u"safe_mode"_q, SafeMode() },
		{ u"unknown_sources"_q, AllowUnknownSources() },
	});
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"app"_q },
		{ u"name"_q, u"app_start"_q },
	});
	StartWatchdog();
	state.listChanges.fire({});
}

void Shutdown() {
	auto &state = Instance();
	if (!state.started) {
		return;
	}
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"app"_q },
		{ u"name"_q, u"app_stop"_q },
	});
	Call(u"shutdown"_q);
	WatchdogRunning = false;
	state.started = false;
	QFile::remove(StartMarkerPath());
}

QJsonValue Call(const QString &method, const QJsonObject &args) {
	auto &state = Instance();
	if (!state.started) {
		return QJsonValue();
	}
	const auto payload = QJsonDocument(args).toJson(QJsonDocument::Compact);
	auto response = QString();
	const auto outer = (CallStarted.load() == 0);
	if (outer) {
		CallStarted = crl::now();
	}
	{
		auto gil = Gil();
		const auto result = PyObject_CallFunction(
			state.dispatch,
			"ss",
			method.toUtf8().constData(),
			payload.constData());
		if (result) {
			response = FromPy(result);
			Py_DECREF(result);
		} else {
			PyErr_Print();
		}
	}
	if (outer) {
		CallStarted = 0;
		if (HangReported.exchange(false)) {
			QFile::remove(HangMarkerPath());
			LOG(("WhitegramPlugins Error: %1 blocked the interface").arg(method));
			EnterAutoSafeMode(true);
		}
	}
	if (response.isEmpty()) {
		return QJsonValue();
	}
	const auto object = QJsonDocument::fromJson(response.toUtf8()).object();
	if (!object.value(u"ok"_q).toBool()) {
		LOG(("WhitegramPlugins Error: %1 failed: %2")
			.arg(method)
			.arg(object.value(u"error"_q).toString()));
		return QJsonValue();
	}
	return object.value(u"result"_q);
}

SendResult OnSendMessage(
		not_null<History*> history,
		const QString &text,
		MsgId replyTo) {
	if (!Instance().started || BypassFlag) {
		return {};
	}
	const auto dialog = DialogId(history->peer);
	CurrentDialog = dialog;
	const auto result = Call(u"send_hook"_q, QJsonObject{
		{ u"peer"_q, double(dialog) },
		{ u"text"_q, text },
		{ u"reply_to"_q, double(replyTo.bare) },
	}).toObject();
	const auto strategy = result.value(u"strategy"_q).toString();
	if (strategy == u"cancel"_q) {
		return { SendStrategy::Cancel };
	} else if (strategy == u"modify"_q) {
		return {
			SendStrategy::Modify,
			result.value(u"text"_q).toString(),
			EntitiesFromJsonArray(result.value(u"entities"_q).toArray()),
		};
	}
	return {};
}

bool Bypassing() {
	return BypassFlag;
}

int64 DialogId(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return int64(peerToUser(user->id).bare);
	} else if (const auto chat = peer->asChat()) {
		return -int64(peerToChat(chat->id).bare);
	} else if (const auto channel = peer->asChannel()) {
		return -int64(peerToChannel(channel->id).bare);
	}
	return 0;
}

void NotifyNewMessage(not_null<HistoryItem*> item) {
	auto &state = Instance();
	if (!state.started || state.eventInFlight) {
		return;
	}
	const auto json = MessageToJson(item);
	state.recent.push_back(json);
	while (state.recent.size() > 500) {
		state.recent.pop_front();
	}
	state.eventInFlight = true;
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"update"_q },
		{ u"name"_q, u"updateNewMessage"_q },
		{ u"update"_q, json },
	});
	state.eventInFlight = false;
}

void NotifyAppEvent(const QString &name) {
	auto &state = Instance();
	if (!state.started || state.eventInFlight) {
		return;
	}
	state.eventInFlight = true;
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"app"_q },
		{ u"name"_q, name },
	});
	state.eventInFlight = false;
}

std::vector<MenuItem> MenuItems(const QString &type) {
	auto result = std::vector<MenuItem>();
	const auto list = Call(u"menu_list"_q, QJsonObject{
		{ u"type"_q, type },
	}).toArray();
	for (const auto &value : list) {
		const auto entry = value.toObject();
		result.push_back({
			entry.value(u"id"_q).toString(),
			entry.value(u"text"_q).toString(),
		});
	}
	return result;
}

void ClickMenuItem(const QString &id, QJsonObject context) {
	if (context.contains(u"dialog_id"_q)) {
		CurrentDialog = int64(context.value(u"dialog_id"_q).toDouble());
	}
	Call(u"menu_click"_q, QJsonObject{
		{ u"id"_q, id },
		{ u"context"_q, context },
	});
}

QString TransformDraft(const QString &text) {
	const auto result = Call(u"draft_transform"_q, QJsonObject{
		{ u"text"_q, text },
	}).toObject();
	const auto transformed = result.value(u"text"_q).toString();
	if (transformed.isEmpty() || transformed == text) {
		ShowToast(QString::fromUtf8(
			"Ни один плагин не обработал черновик."));
		return QString();
	}
	ShowToast(QString::fromUtf8("Трансформировано: %1")
		.arg(result.value(u"plugin"_q).toString()));
	return transformed;
}

rpl::producer<> listChanges() {
	return Instance().listChanges.events();
}

void notifyListChanged() {
	crl::on_main([] {
		Instance().listChanges.fire({});
	});
}

bool EngineEnabled() {
	return QSettings(EngineIniPath(), QSettings::IniFormat)
		.value(u"enabled"_q, true).toBool();
}

void SetEngineEnabled(bool enabled) {
	QSettings(EngineIniPath(), QSettings::IniFormat)
		.setValue(u"enabled"_q, enabled);
	notifyListChanged();
}

bool SafeMode() {
	return QSettings(EngineIniPath(), QSettings::IniFormat)
		.value(u"safe_mode"_q, false).toBool();
}

void SetSafeMode(bool enabled) {
	QSettings(EngineIniPath(), QSettings::IniFormat)
		.setValue(u"safe_mode"_q, enabled);
	Call(u"safe_mode"_q, QJsonObject{
		{ u"enabled"_q, enabled },
	});
	notifyListChanged();
}

bool AllowUnknownSources() {
	return QSettings(EngineIniPath(), QSettings::IniFormat)
		.value(u"allow_unknown"_q, false).toBool();
}

void SetAllowUnknownSources(bool enabled) {
	QSettings(EngineIniPath(), QSettings::IniFormat)
		.setValue(u"allow_unknown"_q, enabled);
	Call(u"unknown_sources"_q, QJsonObject{
		{ u"enabled"_q, enabled },
	});
	notifyListChanged();
}

QString DataPath() {
	return cWorkingDir() + u"tdata/whitegram/plugins/"_q;
}

QString PluginsDir() {
	return DataPath() + u"plugins/"_q;
}

QString SettingsDir() {
	return DataPath() + u"settings/"_q;
}

} // namespace Whitegram::Plugins

#else // WG_WITH_PYTHON

namespace Whitegram::Plugins {

bool Compiled() {
	return false;
}

bool Available() {
	return false;
}

bool Started() {
	return false;
}

QString RuntimeError() {
	return QString::fromUtf8("Python движок не собран.");
}

QString PythonVersion() {
	return QString();
}

void Start() {
}

void Shutdown() {
}

QJsonValue Call(const QString &method, const QJsonObject &args) {
	return QJsonValue();
}

SendResult OnSendMessage(
		not_null<History*> history,
		const QString &text,
		MsgId replyTo) {
	return {};
}

bool Bypassing() {
	return false;
}

void NotifyNewMessage(not_null<HistoryItem*> item) {
}

void NotifyAppEvent(const QString &name) {
}

std::vector<MenuItem> MenuItems(const QString &type) {
	return {};
}

void ClickMenuItem(const QString &id, QJsonObject context) {
}

QString TransformDraft(const QString &text) {
	return QString();
}

rpl::producer<> listChanges() {
	return rpl::never<>();
}

void notifyListChanged() {
}

bool EngineEnabled() {
	return false;
}

void SetEngineEnabled(bool enabled) {
}

bool SafeMode() {
	return false;
}

void SetSafeMode(bool enabled) {
}

bool AllowUnknownSources() {
	return false;
}

void SetAllowUnknownSources(bool enabled) {
}

QString DataPath() {
	return cWorkingDir() + u"tdata/whitegram/plugins/"_q;
}

QString PluginsDir() {
	return DataPath() + u"plugins/"_q;
}

QString SettingsDir() {
	return DataPath() + u"settings/"_q;
}

} // namespace Whitegram::Plugins

#endif // WG_WITH_PYTHON
