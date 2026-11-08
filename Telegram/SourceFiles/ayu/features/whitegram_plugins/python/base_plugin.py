# Whitegram Python plugin SDK: the BasePlugin class and public types.
# Ported and simplified from the exDesktop (extera) plugin idea,
# adapted for Whitegram.
#
# This file is part of Whitegram Desktop, a Telegram Desktop fork.
# Copyright (c) 2026 Whitegram Team.

import traceback
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional

import whitegram_host as _host


SDK_VERSION = "1.0.0"


# Hooks

class HookStrategy:
    DEFAULT = "DEFAULT"    # Let the message be sent as is.
    CANCEL = "CANCEL"      # Cancel sending.
    MODIFY = "MODIFY"      # Replace the message text/entities.


@dataclass
class HookResult:
    strategy: str = HookStrategy.DEFAULT
    params: Optional["SendMessageParams"] = None


@dataclass
class SendMessageParams:
    dialog_id: int = 0
    text: str = ""
    entities: List[Any] = field(default_factory=list)
    reply_to: int = 0

    @property
    def peer(self) -> int:
        return self.dialog_id


# Events

class AppEvent:
    START = "START"
    STOP = "STOP"
    PAUSE = "PAUSE"
    RESUME = "RESUME"


_APP_EVENT_FROM_NATIVE = {
    "app_start": AppEvent.START,
    "app_stop": AppEvent.STOP,
    "app_pause": AppEvent.PAUSE,
    "app_resume": AppEvent.RESUME,
    "background": AppEvent.PAUSE,
    "foreground": AppEvent.RESUME,
}


def _app_event_from_native(name: str) -> Optional[str]:
    return _APP_EVENT_FROM_NATIVE.get(name)


# Menus

class MenuItemType:
    MESSAGE_CONTEXT_MENU = "MESSAGE_CONTEXT_MENU"


@dataclass
class MenuItemData:
    menu_type: str
    text: str
    on_click: Optional[Callable[[Dict[str, Any]], None]] = None
    item_id: Optional[str] = None
    priority: int = 0


# Slash commands

@dataclass
class CommandContext:
    plugin: Optional["BasePlugin"] = None
    command: str = ""
    args: List[str] = field(default_factory=list)
    text: str = ""
    dialog_id: int = 0


# Settings page items

class Switch:
    def __init__(self, label: str, key: str = "", default: bool = False,
                 on_change: Optional[Callable[[bool], None]] = None,
                 description: str = ""):
        self.label = label
        self.key = key
        self.default = default
        self.on_change = on_change
        self.description = description


class Button:
    def __init__(self, label: str, on_click: Optional[Callable[[], None]] = None,
                 description: str = ""):
        self.label = label
        self.on_click = on_click
        self.description = description


class Label:
    def __init__(self, text: str, description: str = ""):
        self.label = text
        self.description = description


# Logger

class _PluginLogger:
    def __init__(self, plugin):
        self._plugin = plugin

    def _write(self, level, message, args):
        try:
            text = str(message) % args if args else str(message)
        except Exception:
            text = str(message)
        self._plugin.log(f"[{level}] {text}")

    def debug(self, message, *args):
        self._write("debug", message, args)

    def info(self, message, *args):
        self._write("info", message, args)

    def warning(self, message, *args):
        self._write("warning", message, args)

    def error(self, message, *args):
        self._write("error", message, args)

    def exception(self, message, *args):
        self._write("error", message, args)
        self._plugin.log(traceback.format_exc(limit=6))


# Base plugin

class BasePlugin:
    _wg_plugin_id: Optional[str] = None
    MenuType = MenuItemType

    id: Optional[str] = None
    name: Optional[str] = None

    # -- Lifecycle ---------------------------------------------------------

    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass

    def on_app_event(self, event_type: str):
        pass

    # -- Messages ----------------------------------------------------------

    def on_update_hook(self, update_name: str, update: Dict[str, Any]):
        pass

    def on_send_message_hook(self, params: SendMessageParams) -> Optional[HookResult]:
        return None

    def on_draft_transform(self, text: str) -> Optional[str]:
        return None

    # -- Commands ----------------------------------------------------------

    def register_command(self, name: str, handler: Optional[Callable] = None):
        clean = str(name).lstrip("/").strip()
        if handler is None:
            def decorator(func):
                _host.add_command(self._wg_plugin_id, clean, func)
                return func
            return decorator
        _host.add_command(self._wg_plugin_id, clean, handler)
        return handler

    def remove_command(self, name: str):
        _host.remove_command(self._wg_plugin_id, str(name).lstrip("/").strip())

    # -- Menus -------------------------------------------------------------

    def add_menu_item(self, menu_type_or_data, text=None, *, on_click=None,
                      priority=0, item_id=None) -> Optional[str]:
        data = menu_type_or_data
        if not isinstance(data, MenuItemData):
            data = MenuItemData(str(data), text, on_click, item_id, priority)
        added = _host.add_menu_item(self._wg_plugin_id, data)
        if data.on_click is not None and added:
            self._wg_menu_callbacks()[added] = data.on_click
        return added

    def remove_menu_item(self, item_id: str):
        self._wg_menu_callbacks().pop(item_id, None)
        _host.remove_menu_item(self._wg_plugin_id, item_id)

    # -- Settings ----------------------------------------------------------

    def create_settings(self) -> Optional[list]:
        return None

    def get_setting(self, key: str, default=None):
        import plugin_settings
        return plugin_settings.get_setting(self.id, key, default)

    def set_setting(self, key: str, value, reload_settings: bool = False):
        import plugin_settings
        plugin_settings.set_setting(self.id, key, value, reload_settings)

    def export_settings(self) -> dict:
        import plugin_settings
        return plugin_settings.get_settings(self.id)

    # -- Host integration (called from whitegram_host) ---------------------

    def _wg_attach(self, plugin_id: str):
        self._wg_plugin_id = plugin_id
        self.id = plugin_id
        if "_wg_menu" not in self.__dict__:
            self.__dict__["_wg_menu"] = {}

    def _wg_menu_callbacks(self) -> dict:
        value = self.__dict__.get("_wg_menu")
        if not isinstance(value, dict):
            value = self.__dict__["_wg_menu"] = {}
        return value

    def _wg_dispatch_menu_click(self, item_id: str, context: Dict[str, Any]):
        callback = self._wg_menu_callbacks().get(item_id)
        if callback is None:
            return
        callback(context)

    def _wg_has_update_hook(self) -> bool:
        return type(self).on_update_hook is not BasePlugin.on_update_hook

    def _wg_has_send_hook(self) -> bool:
        return type(self).on_send_message_hook is not BasePlugin.on_send_message_hook

    def _wg_has_draft_transform(self) -> bool:
        return type(self).on_draft_transform is not BasePlugin.on_draft_transform

    def _wg_has_app_event(self) -> bool:
        return type(self).on_app_event is not BasePlugin.on_app_event

    def _wg_has_settings(self) -> bool:
        return type(self).create_settings is not BasePlugin.create_settings

    def _wg_cleanup(self):
        _host.remove_all_for_plugin(self._wg_plugin_id)

    # -- Convenience API ---------------------------------------------------

    def log(self, message: str):
        _host.log(f"[{self._wg_plugin_id or '?'}] {message}")

    @property
    def logger(self):
        if "_wg_logger" not in self.__dict__:
            self.__dict__["_wg_logger"] = _PluginLogger(self)
        return self.__dict__["_wg_logger"]

    def toast(self, text: str):
        _host.toast(text)

    def self_id(self) -> int:
        return _host.self_id()

    def current_dialog(self) -> int:
        return _host.current_peer()

    def show_alert(self, title, message, buttons=("OK",), on_click=None):
        return _host.show_alert(title, message, buttons, on_click)

    def run_later(self, fn, delay_ms: int = 0):
        _host.run_on_main(fn, delay_ms)

    def send_message(self, text: str, dialog_id: Optional[int] = None,
                     reply_to: int = 0):
        _host.send_message(text, dialog_id, reply_to)

    def peer_info(self, dialog_id: Optional[int] = None) -> dict:
        return _host.peer_info(dialog_id)

    def history(self, dialog_id: Optional[int] = None, limit: int = 50) -> list:
        return _host.get_history(dialog_id, limit)
