# Whitegram plugin host: runs inside the embedded CPython interpreter,
# owns the plugin registry and dispatches calls from C++.
# Ported and simplified from the exDesktop (extera_host) plugin idea.
#
# This file is part of Whitegram Desktop, a Telegram Desktop fork.
# Copyright (c) 2026 Whitegram Team.

import contextlib
import importlib
import importlib.abc
import importlib.machinery
import importlib.util
import json
import os
import sys
import threading
import traceback
import uuid

import _whitegram

PLUGINS_DIR = ""
SETTINGS_DIR = ""
_lock = threading.RLock()
_records = {}
_commands = {}
_menu_items = {}
_alerts = {}
_alert_seq = 0
_safe_mode = False
_unknown_allowed = False
_trusted_cache = None
_plugin_stack = []


# Log

def log(message):
    try:
        _whitegram.log(str(message))
    except Exception:
        pass


def toast(text):
    _whitegram.toast(str(text))


def current_plugin_id():
    return _plugin_stack[-1] if _plugin_stack else None


@contextlib.contextmanager
def _plugin_context(plugin_id):
    _plugin_stack.append(plugin_id)
    try:
        yield
    finally:
        _plugin_stack.pop()


# Client API (re-exported for plugins through base_plugin helpers)

def open_url(url):
    _whitegram.open_url(str(url))


def copy(text):
    _whitegram.copy(str(text))


def current_peer():
    return _whitegram.current_peer()


def self_id():
    return _whitegram.self_id()


def send_message(text, dialog_id=None, reply_to=0, entities=None):
    _whitegram.send_message(
        int(dialog_id or 0),
        str(text),
        _entities_to_json(entities),
        int(reply_to or 0))


def edit_message(dialog_id, msg_id, text, entities=None):
    _whitegram.edit_message(
        int(dialog_id or 0),
        int(msg_id or 0),
        str(text),
        _entities_to_json(entities))


def peer_info(dialog_id=None):
    try:
        return json.loads(_whitegram.peer_info(int(dialog_id or 0)))
    except Exception:
        return {}


def get_history(dialog_id=None, limit=50):
    try:
        return json.loads(_whitegram.get_history(int(dialog_id or 0), int(limit)))
    except Exception:
        return []


def run_on_main(fn, delay_ms=0):
    _whitegram.run_on_main(fn, int(delay_ms or 0))


def show_alert(title, message, buttons=("OK",), on_click=None):
    global _alert_seq
    _alert_seq += 1
    alert_id = _alert_seq
    if on_click is not None:
        _alerts[alert_id] = on_click
    payload = {
        "id": alert_id,
        "title": str(title or ""),
        "message": str(message or ""),
        "buttons": [
            {"text": str(b), "which": i}
            for i, b in enumerate(buttons)
        ],
    }
    _whitegram.show_alert(json.dumps(payload, ensure_ascii=False))
    return alert_id


def _entity_to_wire(entity):
    if isinstance(entity, dict):
        return entity
    if isinstance(entity, (list, tuple)) and len(entity) >= 3:
        kind, offset, length = entity[0], int(entity[1]), int(entity[2])
        data = entity[3] if len(entity) > 3 else ""
        wire = {"type": kind, "offset": offset, "length": length}
        if kind in ("url", "text_link"):
            wire["url"] = str(data)
        elif kind == "pre":
            wire["language"] = str(data)
        return wire
    return None


def _entities_to_json(entities):
    if not entities:
        return "[]"
    wire = [e for e in (_entity_to_wire(entity) for entity in entities) if e]
    return json.dumps(wire, ensure_ascii=False)


def reload_settings(plugin_id):
    _whitegram.settings_changed(str(plugin_id))


# Registrations

def add_command(plugin_id, name, handler):
    _commands[str(name)] = (plugin_id, handler)


def remove_command(plugin_id, name):
    entry = _commands.get(str(name))
    if entry and entry[0] == plugin_id:
        del _commands[str(name)]


def add_menu_item(plugin_id, data):
    item_id = data.item_id or (str(plugin_id) + ":" + uuid.uuid4().hex[:8])
    _menu_items[item_id] = (plugin_id, data)
    return item_id


def remove_menu_item(plugin_id, item_id):
    entry = _menu_items.get(item_id)
    if entry and entry[0] == plugin_id:
        del _menu_items[item_id]
        return True
    return False


def remove_all_for_plugin(plugin_id):
    for name in [k for k, v in _commands.items() if v[0] == plugin_id]:
        del _commands[name]
    for item_id in [k for k, v in _menu_items.items() if v[0] == plugin_id]:
        del _menu_items[item_id]


# Loading

class Record:
    def __init__(self, path, meta):
        self.path = path
        self.meta = meta
        self.id = meta["id"]
        self.module = None
        self.instance = None
        self.error = None

    def info(self):
        error = self.error
        if error == "UNKNOWN_SOURCE":
            pass
        elif error:
            error = error.splitlines()[0]
        return {
            "id": self.id,
            "name": self.meta.get("name", self.id),
            "description": self.meta.get("description", ""),
            "author": self.meta.get("author", ""),
            "version": self.meta.get("version", ""),
            "enabled": is_enabled(self.id),
            "safe_mode": _safe_mode,
            "trusted": is_trusted_file(self.path),
            "loaded": self.instance is not None,
            "error": error or "",
            "has_settings": bool(
                self.instance is not None and self.instance._wg_has_settings()),
            "path": self.path,
            "permissions": self.meta.get("permissions") or [],
            "app_version": self.meta.get("app_version"),
        }


def _base():
    import base_plugin
    return base_plugin


def _state_path():
    return os.path.join(SETTINGS_DIR, "_state.json")


def _load_state():
    try:
        with open(_state_path(), encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def _save_state(state):
    try:
        os.makedirs(SETTINGS_DIR, exist_ok=True)
        with open(_state_path(), "w", encoding="utf-8") as f:
            json.dump(state, f)
    except Exception as e:
        log(f"state save failed: {e}")


def is_enabled(plugin_id):
    return bool(_load_state().get(plugin_id, True))


def _set_enabled_state(plugin_id, enabled):
    state = _load_state()
    state[plugin_id] = bool(enabled)
    _save_state(state)


class _PluginModuleFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if path is not None or fullname in sys.modules:
            return None
        record = _records.get(fullname)
        if record is None:
            return None
        loader = importlib.machinery.SourceFileLoader(fullname, record.path)
        return importlib.util.spec_from_file_location(
            fullname, record.path, loader=loader)


def _install_plugin_importer():
    if not any(isinstance(f, _PluginModuleFinder) for f in sys.meta_path):
        sys.meta_path.insert(0, _PluginModuleFinder())
    if PLUGINS_DIR and PLUGINS_DIR not in sys.path:
        sys.path.append(PLUGINS_DIR)


# Trust

def file_sha256(path):
    import hashlib
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _trusted_path():
    return os.path.join(SETTINGS_DIR, "_trusted.json")


def _trusted_hashes():
    global _trusted_cache
    if _trusted_cache is None:
        try:
            with open(_trusted_path(), encoding="utf-8") as f:
                _trusted_cache = set(json.load(f))
        except Exception:
            _trusted_cache = set()
    return _trusted_cache


def _add_trusted(digest):
    hashes = _trusted_hashes()
    if digest in hashes:
        return
    hashes.add(digest)
    try:
        os.makedirs(SETTINGS_DIR, exist_ok=True)
        with open(_trusted_path(), "w", encoding="utf-8") as f:
            json.dump(sorted(hashes), f)
    except Exception as e:
        log(f"trusted save failed: {e}")


def is_trusted_file(path):
    try:
        return file_sha256(path) in _trusted_hashes()
    except Exception:
        return False


def _blocked_reason(record):
    if not _unknown_allowed and not is_trusted_file(record.path):
        return "UNKNOWN_SOURCE"
    return None


def _instantiate(record):
    base = _base()
    module = importlib.import_module(record.id)
    record.module = module
    plugin_class = None
    for value in list(module.__dict__.values()):
        if (isinstance(value, type)
                and issubclass(value, base.BasePlugin)
                and value is not base.BasePlugin
                and value.__module__ == module.__name__):
            plugin_class = value
    if plugin_class is None:
        raise RuntimeError("No BasePlugin subclass found")
    instance = plugin_class()
    instance._wg_attach(record.id)
    instance.name = record.meta.get("name", record.id)
    record.instance = instance


def _activate(record):
    record.error = None
    blocked = _blocked_reason(record)
    if blocked:
        record.error = blocked
        log(f"plugin {record.id} blocked: {blocked}")
        return
    try:
        if record.instance is None:
            _instantiate(record)
        with _plugin_context(record.id):
            record.instance.on_plugin_load()
        log(f"plugin {record.id} loaded")
    except Exception:
        record.error = traceback.format_exc(limit=6)
        record.instance = None
        remove_all_for_plugin(record.id)
        log(f"plugin {record.id} failed:\n{record.error}")


def _deactivate(record):
    if record.instance is not None:
        try:
            with _plugin_context(record.id):
                record.instance._wg_cleanup()
                record.instance.on_plugin_unload()
        except Exception:
            log(f"plugin {record.id} unload failed:\n"
                f"{traceback.format_exc(limit=4)}")
    remove_all_for_plugin(record.id)
    record.instance = None
    record.module = None
    sys.modules.pop(record.id, None)


def load_all():
    names = sorted(os.listdir(PLUGINS_DIR)) if os.path.isdir(PLUGINS_DIR) else []
    for name in names:
        if not name.endswith(".plugin"):
            continue
        path = os.path.join(PLUGINS_DIR, name)
        try:
            import metadata_parser
            meta = metadata_parser.read_metadata(path)
        except Exception as e:
            log(f"bad plugin file {name}: {e}")
            continue
        record = Record(path, meta)
        old = _records.get(record.id)
        if old is not None:
            _deactivate(old)
        _records[record.id] = record
    _install_plugin_importer()
    for record in list(_records.values()):
        if is_enabled(record.id) and not _safe_mode:
            _activate(record)


# Dispatch helpers

def _send_hook(args):
    base = _base()
    text = args.get("text", "")
    peer = int(args.get("peer", 0))
    reply_to = int(args.get("reply_to", 0))

    # Slash commands take priority and are always swallowed.
    stripped = text.strip()
    if stripped.startswith("/"):
        parts = stripped[1:].split()
        if parts:
            entry = _commands.get(parts[0])
            if entry is not None:
                plugin_id, handler = entry
                record = _records.get(plugin_id)
                context = base.CommandContext(
                    plugin=record.instance if record else None,
                    command=parts[0],
                    args=parts[1:],
                    text=stripped,
                    dialog_id=peer)
                try:
                    with _plugin_context(plugin_id):
                        result = handler(context)
                    if isinstance(result, str) and result:
                        send_message(result, peer, reply_to)
                except Exception:
                    log(f"[{plugin_id}] command '{parts[0]}' failed:\n"
                        f"{traceback.format_exc(limit=4)}")
                return {"strategy": "cancel"}

    # Message hooks of the plugins that implemented them.
    params = base.SendMessageParams(
        dialog_id=peer, text=text, reply_to=reply_to)
    for record in list(_records.values()):
        instance = record.instance
        if instance is None or not instance._wg_has_send_hook():
            continue
        try:
            with _plugin_context(record.id):
                result = instance.on_send_message_hook(params)
        except Exception:
            log(f"[{record.id}] send hook failed:\n"
                f"{traceback.format_exc(limit=4)}")
            continue
        if result is None:
            continue
        strategy = getattr(result, "strategy", base.HookStrategy.DEFAULT)
        if getattr(result, "params", None) is not None:
            params = result.params
        if strategy == base.HookStrategy.CANCEL:
            return {"strategy": "cancel"}
        if strategy == base.HookStrategy.MODIFY:
            entities = getattr(params, "entities", None) or []
            wire = [e for e in (_entity_to_wire(e) for e in entities) if e]
            return {"strategy": "modify",
                    "text": params.text,
                    "entities": wire}

    return {"strategy": "default"}


def _draft_transform(args):
    text = args.get("text", "")
    base = _base()
    for record in list(_records.values()):
        instance = record.instance
        if instance is None or not instance._wg_has_draft_transform():
            continue
        try:
            with _plugin_context(record.id):
                result = instance.on_draft_transform(text)
        except Exception:
            log(f"[{record.id}] draft transform failed:\n"
                f"{traceback.format_exc(limit=4)}")
            continue
        if isinstance(result, str) and result != text:
            return {"text": result, "plugin": record.id}
    return {}


def _settings_items(plugin_id):
    record = _records.get(plugin_id)
    if not record or not record.instance:
        return []
    base = _base()
    try:
        with _plugin_context(plugin_id):
            items = record.instance.create_settings() or []
    except Exception:
        log(f"[{plugin_id}] create_settings failed:\n"
            f"{traceback.format_exc(limit=4)}")
        return []
    result = []
    for index, item in enumerate(items):
        entry = {"index": index, "description": getattr(item, "description", "")}
        if isinstance(item, base.Switch):
            entry.update({
                "type": "switch",
                "label": str(item.label),
                "key": str(item.key or ""),
                "value": bool(get_setting_value(
                    plugin_id, item.key, item.default)),
            })
        elif isinstance(item, base.Button):
            entry.update({
                "type": "button",
                "label": str(item.label),
            })
        elif isinstance(item, base.Label):
            entry.update({
                "type": "label",
                "label": str(item.label),
            })
        else:
            continue
        result.append(entry)
    return result


def get_setting_value(plugin_id, key, default=None):
    import plugin_settings
    return plugin_settings.get_setting(plugin_id, key, default)


def _setting_by_index(plugin_id, index):
    record = _records.get(plugin_id)
    if not record or not record.instance:
        return None
    base = _base()
    try:
        with _plugin_context(plugin_id):
            items = record.instance.create_settings() or []
    except Exception:
        return None
    if index < 0 or index >= len(items):
        return None
    return items[index]


def _setting_changed(args):
    plugin_id = args["id"]
    item = _setting_by_index(plugin_id, int(args.get("index", -1)))
    if item is None:
        return False
    base = _base()
    value = args.get("value")
    if isinstance(item, base.Switch):
        if getattr(item, "key", None):
            import plugin_settings
            plugin_settings.set_setting(plugin_id, item.key, value)
        callback = getattr(item, "on_change", None)
        if callback:
            try:
                with _plugin_context(plugin_id):
                    callback(value)
            except Exception:
                log(f"[{plugin_id}] switch on_change failed:\n"
                    f"{traceback.format_exc(limit=4)}")
    return True


def _setting_click(args):
    plugin_id = args["id"]
    item = _setting_by_index(plugin_id, int(args.get("index", -1)))
    if item is None:
        return False
    callback = getattr(item, "on_click", None)
    if callback is None:
        return False
    try:
        with _plugin_context(plugin_id):
            callback()
    except Exception:
        log(f"[{plugin_id}] button on_click failed:\n"
            f"{traceback.format_exc(limit=4)}")
    return True


_MENU_TYPE_ALIASES = {
    "message_context_menu": "MESSAGE_CONTEXT_MENU",
}


def _menu_list(args):
    base = _base()
    wanted = args.get("type", "")
    wanted = _MENU_TYPE_ALIASES.get(str(wanted).lower(), str(wanted).upper())
    result = []
    for item_id, (plugin_id, data) in _menu_items.items():
        record = _records.get(plugin_id)
        if record is None or record.instance is None:
            continue
        menu_type = str(getattr(data, "menu_type", ""))
        if menu_type.upper() != wanted:
            continue
        text = data.text() if callable(data.text) else data.text
        result.append({
            "id": item_id,
            "text": str(text),
            "priority": int(getattr(data, "priority", 0) or 0),
        })
    result.sort(key=lambda x: -x["priority"])
    return result


def _menu_click(args):
    entry = _menu_items.get(args.get("id"))
    if not entry:
        return False
    plugin_id, data = entry
    record = _records.get(plugin_id)
    if record is None or record.instance is None:
        return False
    context = dict(args.get("context") or {})
    try:
        with _plugin_context(plugin_id):
            record.instance._wg_dispatch_menu_click(args.get("id"), context)
    except Exception:
        log(f"[{plugin_id}] menu click failed:\n"
            f"{traceback.format_exc(limit=4)}")
    return True


def _event(args):
    base = _base()
    kind = args.get("type")
    if kind == "update":
        name = args.get("name", "")
        update = args.get("update") or {}
        for record in list(_records.values()):
            instance = record.instance
            if instance is None or not instance._wg_has_update_hook():
                continue
            try:
                with _plugin_context(record.id):
                    instance.on_update_hook(name, update)
            except Exception:
                log(f"[{record.id}] update hook failed:\n"
                    f"{traceback.format_exc(limit=4)}")
        return True
    if kind == "app":
        event = _app_event_name(args.get("name", ""))
        if event is None:
            return False
        for record in list(_records.values()):
            instance = record.instance
            if instance is None or not instance._wg_has_app_event():
                continue
            try:
                with _plugin_context(record.id):
                    instance.on_app_event(event)
            except Exception:
                log(f"[{record.id}] app event failed:\n"
                    f"{traceback.format_exc(limit=4)}")
        return True
    return False


_APP_EVENT_MAP = {
    "app_start": "START",
    "app_stop": "STOP",
    "app_pause": "PAUSE",
    "app_resume": "RESUME",
    "background": "PAUSE",
    "foreground": "RESUME",
}


def _app_event_name(name):
    return _APP_EVENT_MAP.get(name)


def _alert_click(args):
    alert_id = int(args.get("id", 0))
    which = int(args.get("which", 0))
    callback = _alerts.pop(alert_id, None)
    if callback is None:
        return False
    try:
        callback(which)
    except Exception:
        log(f"alert callback failed:\n{traceback.format_exc(limit=4)}")
    return True


# Plugin management methods

def _install(args):
    import shutil
    import metadata_parser
    path = args["path"]
    meta = metadata_parser.read_metadata(path)
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    target = os.path.join(PLUGINS_DIR, meta["id"] + ".plugin")
    old = _records.get(meta["id"])
    if old:
        _deactivate(old)
    if os.path.abspath(path) != os.path.abspath(target):
        shutil.copyfile(path, target)
    record = Record(target, meta)
    _records[record.id] = record
    _set_enabled_state(record.id, True)
    if bool(args.get("trust", True)):
        _add_trusted(file_sha256(target))
    if not _safe_mode:
        _activate(record)
    return record.info()


def _delete(args):
    record = _records.pop(args["id"], None)
    if not record:
        return False
    _deactivate(record)
    try:
        os.remove(record.path)
    except Exception:
        pass
    return True


def _set_enabled(args):
    record = _records.get(args["id"])
    if not record:
        return None
    if _safe_mode and args["enabled"]:
        return record.info()
    _set_enabled_state(record.id, args["enabled"])
    if args["enabled"] and not _safe_mode:
        _activate(record)
    else:
        _deactivate(record)
    return record.info()


def _trust(args):
    path = args.get("path")
    digest = args.get("hash") or (file_sha256(path) if path else None)
    if digest:
        _add_trusted(digest)
        record = next(
            (r for r in _records.values() if r.path == path), None)
        if record is not None and record.error == "UNKNOWN_SOURCE" \
                and not _safe_mode:
            _activate(record)
    return True


def _set_unknown_sources(args):
    global _unknown_allowed
    _unknown_allowed = bool(args.get("enabled"))
    for record in list(_records.values()):
        if not is_enabled(record.id) or _safe_mode:
            continue
        if record.instance is None:
            _activate(record)
    return [r.info() for r in _records.values()]


def _source(args):
    path = args.get("path")
    if not path and args.get("id") in _records:
        path = _records[args["id"]].path
    with open(path, "rb") as f:
        data = f.read(4 * 1024 * 1024)
    return data.decode("utf-8", "replace")


def _metadata(args):
    import metadata_parser
    metadata = dict(metadata_parser.read_metadata(args["path"]))
    metadata["trusted"] = is_trusted_file(args["path"])
    return metadata


def _init(args):
    global PLUGINS_DIR, SETTINGS_DIR, _safe_mode, _unknown_allowed
    PLUGINS_DIR = args["plugins"]
    SETTINGS_DIR = args["settings"]
    _safe_mode = bool(args.get("safe_mode"))
    _unknown_allowed = bool(args.get("unknown_sources"))
    import plugin_settings
    plugin_settings.configure(SETTINGS_DIR)
    plugin_settings.set_notifier(
        lambda plugin_id: _whitegram.settings_changed(plugin_id))
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    os.makedirs(SETTINGS_DIR, exist_ok=True)
    load_all()
    return [r.info() for r in _records.values()]


def _set_safe_mode(args):
    global _safe_mode
    enabled = bool(args.get("enabled"))
    if enabled == _safe_mode:
        return [r.info() for r in _records.values()]
    _safe_mode = enabled
    for record in list(_records.values()):
        if enabled:
            _deactivate(record)
        elif is_enabled(record.id):
            _activate(record)
    return [r.info() for r in _records.values()]


def _shutdown(args):
    for record in list(_records.values()):
        _deactivate(record)
    return True


_METHODS = {
    "init": _init,
    "shutdown": _shutdown,
    "safe_mode": _set_safe_mode,
    "unknown_sources": _set_unknown_sources,
    "trust": _trust,
    "source": _source,
    "metadata": _metadata,
    "list": lambda args: [r.info() for r in _records.values()],
    "install": _install,
    "delete": _delete,
    "set_enabled": _set_enabled,
    "send_hook": _send_hook,
    "draft_transform": _draft_transform,
    "settings": lambda args: _settings_items(args["id"]),
    "setting_changed": _setting_changed,
    "setting_click": _setting_click,
    "alert_click": _alert_click,
    "menu_list": _menu_list,
    "menu_click": _menu_click,
    "event": _event,
}


def dispatch(method, payload):
    try:
        args = json.loads(payload) if payload else {}
        with _lock:
            result = _METHODS[method](args)
        return json.dumps({"ok": True, "result": result}, ensure_ascii=False)
    except Exception:
        error = traceback.format_exc(limit=6)
        log(f"dispatch {method} failed:\n{error}")
        return json.dumps({"ok": False, "error": error}, ensure_ascii=False)
