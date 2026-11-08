# Public plugin-facing API module: `import whitegram` inside .plugin files.
#
# This file is part of Whitegram Desktop, a Telegram Desktop fork.
# Copyright (c) 2026 Whitegram Team.

import whitegram_host as _host
from base_plugin import (
    SDK_VERSION,
    AppEvent,
    BasePlugin,
    Button,
    CommandContext,
    HookResult,
    HookStrategy,
    Label,
    MenuItemData,
    MenuItemType,
    SendMessageParams,
    Switch,
)


def log(message):
    """Write a message to the Whitegram log."""
    _host.log(message)


def toast(text):
    """Show a Telegram-style toast notification."""
    _host.toast(text)


def open_url(url):
    """Open an http(s):// or tg:// URL in the system browser."""
    _host.open_url(url)


def copy(text):
    """Copy text to the clipboard."""
    _host.copy(text)


def send_message(text, dialog_id=None, reply_to=0, entities=None):
    """Send a text message; dialog_id=None uses the currently open chat."""
    _host.send_message(text, dialog_id, reply_to, entities)


def edit_message(dialog_id, msg_id, text, entities=None):
    """Edit a message text (only messages of the current account)."""
    _host.edit_message(dialog_id, msg_id, text, entities)


def show_alert(title, message, buttons=("OK",), on_click=None):
    """Show a modal dialog box; on_click receives the pressed button index."""
    _host.show_alert(title, message, buttons, on_click)


def current_peer():
    """Dialog id of the currently open chat (0 if none)."""
    return _host.current_peer()


def self_id():
    """Dialog id of the current account owner."""
    return _host.self_id()


def peer_info(dialog_id=None):
    """Return a dict with id, type, name, username of a peer."""
    return _host.peer_info(dialog_id)


def history(dialog_id=None, limit=50):
    """Return recent messages seen during this session, newest first."""
    return _host.get_history(dialog_id, limit)


def run_on_main(fn, delay_ms=0):
    """Schedule a Python callable on the client UI thread."""
    _host.run_on_main(fn, delay_ms)


def get_setting(plugin_id, key, default=None):
    import plugin_settings
    return plugin_settings.get_setting(plugin_id, key, default)


def set_setting(plugin_id, key, value, reload_settings=False):
    import plugin_settings
    plugin_settings.set_setting(plugin_id, key, value, reload_settings)


Plugin = BasePlugin
