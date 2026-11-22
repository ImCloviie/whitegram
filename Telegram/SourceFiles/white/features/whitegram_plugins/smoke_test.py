import json
import os
import sys
import tempfile
import types

# Stub the native module that the C++ client provides.
native_log = []
native_calls = []


class _Stub(types.ModuleType):
    def __getattr__(self, name):
        def fn(*args, **kwargs):
            native_calls.append((name,) + tuple(args))
            if name == "log":
                native_log.append(args[0])
            if name == "peer_info":
                return json.dumps({"id": 1, "name": "Test"})
            if name == "get_history":
                return json.dumps([])
            return None
        return fn


sys.modules["_whitegram"] = _Stub("_whitegram")

sdk_dir = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "python")
sys.path.insert(0, sdk_dir)

import whitegram_host as host  # noqa: E402

root = tempfile.mkdtemp(prefix="wgplugins")
plugins = os.path.join(root, "plugins")
settings = os.path.join(root, "settings")
os.makedirs(plugins)
os.makedirs(settings)

res = json.loads(host.dispatch("init", json.dumps({
    "plugins": plugins,
    "settings": settings,
    "safe_mode": False,
    "unknown_sources": False,
})))
print("init:", res["ok"])

# Copy examples in.
examples = os.path.normpath(os.path.join(
    sdk_dir, "..", "..", "..", "..", "..", "..", "plugins", "examples"))
names = []
for fn in os.listdir(examples):
    if fn.endswith(".plugin"):
        src = os.path.join(examples, fn)
        with open(src, "rb") as f:
            data = f.read()
        metadata = json.loads(host.dispatch("metadata", json.dumps({"path": src})))
        assert metadata["ok"] and not metadata["result"]["trusted"]
        # Install through the host (copies + trusts + activates).
        info = json.loads(host.dispatch("install", json.dumps(
            {"path": src, "trust": True})))
        names.append(info["result"]["id"])
        metadata = json.loads(host.dispatch("metadata", json.dumps({"path": src})))
        assert metadata["result"]["trusted"]
        changed = os.path.join(root, "changed.plugin")
        with open(changed, "wb") as f:
            f.write(data + b"\n# changed after approval\n")
        metadata = json.loads(host.dispatch("metadata", json.dumps({"path": changed})))
        assert not metadata["result"]["trusted"]
print("installed:", names)

lst = json.loads(host.dispatch("list", "{}"))["result"]
for entry in lst:
    assert not entry["error"], f"{entry['id']}: {entry['error']}"
    assert entry["loaded"], entry["id"]
print("list ok:", [(e["id"], e["trusted"], e["loaded"]) for e in lst])

# App event.
res = json.loads(host.dispatch("event", json.dumps(
    {"type": "app", "name": "app_start"})))
print("app event:", res)

# New message event (should not crash plugins).
res = json.loads(host.dispatch("event", json.dumps(
    {"type": "update", "name": "updateNewMessage",
     "update": {"id": 5, "dialog_id": 42, "out": False,
                "date": 0, "sender_id": 7, "text": "!mark"}})))
print("update event:", res["ok"])

# Slash command: /ping hello world -> cancelled, sends reply.
native_calls.clear()
res = json.loads(host.dispatch("send_hook", json.dumps(
    {"peer": 42, "text": "/ping hello world", "reply_to": 0})))
print("command hook:", res["result"])
assert res["result"]["strategy"] == "cancel"
assert any(c[0] == "send_message" for c in native_calls), native_calls

# Non-command text passes through; autocaps modifies it.
res = json.loads(host.dispatch("send_hook", json.dumps(
    {"peer": 42, "text": "hello there", "reply_to": 0})))
print("send hook:", res["result"])
assert res["result"]["strategy"] == "modify"
assert res["result"]["text"] == "Hello there"

# Unknown command is not swallowed.
res = json.loads(host.dispatch("send_hook", json.dumps(
    {"peer": 42, "text": "/othercmd text", "reply_to": 0})))
print("unknown command:", res["result"])
assert res["result"]["strategy"] == "default"

# Draft transform.
res = json.loads(host.dispatch("draft_transform", json.dumps(
    {"text": "one two three"})))
print("draft transform:", res["result"])
assert res["result"].get("plugin") in ("wordstats", "testbench")
assert res["result"]["text"] and res["result"]["text"] != "one two three"

# Menu items.
res = json.loads(host.dispatch("menu_list", json.dumps(
    {"type": "message_context_menu"})))
items = res["result"]
print("menu items:", items)
assert any(i["text"] == "👍 Реакция" for i in items)
res = json.loads(host.dispatch("menu_click", json.dumps(
    {"id": items[0]["id"], "context": {"dialog_id": 42, "message_id": 5}})))
print("menu click:", res)

# Plugin settings.
res = json.loads(host.dispatch("settings", json.dumps({"id": "hello"})))
print("plugin settings:", res["result"])
assert res["result"][1]["type"] == "switch"
res = json.loads(host.dispatch("setting_changed", json.dumps(
    {"id": "hello", "index": 1, "value": True})))
assert res["result"]

# Set enabled / safe mode / delete.
res = json.loads(host.dispatch("set_enabled", json.dumps(
    {"id": "autocaps", "enabled": False})))
assert not res["result"]["loaded"]
res = json.loads(host.dispatch("send_hook", json.dumps(
    {"peer": 42, "text": "lowercase stays", "reply_to": 0})))
assert res["result"]["strategy"] == "default"
res = json.loads(host.dispatch("safe_mode", json.dumps({"enabled": True})))
assert all(r["safe_mode"] for r in res["result"])
res = json.loads(host.dispatch("safe_mode", json.dumps({"enabled": False})))
assert res["ok"]

print()
print("HOST LOG:")
for line in native_log:
    print("  ", line)
print("OK - all smoke checks passed")
