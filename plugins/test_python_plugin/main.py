import sys
import json

class Plugin:
    def __init__(self):
        self.command_handlers = {}
        self.message_handlers = []

    def command(self, name):
        def decorator(func):
            self.command_handlers[name] = func
            self._rpc_call("register_command", {"command": name})
            return func
        return decorator

    def on_message(self, func):
        self.message_handlers.append(func)
        return func

    def _rpc_call(self, method, params=None):
        payload = {"jsonrpc": "2.0", "method": method, "params": params or {}}
        sys.stdout.write(json.dumps(payload) + "\n")
        sys.stdout.flush()

    def show_toast(self, text):
        self._rpc_call("show_toast", {"text": text})

    def run(self):
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                data = json.loads(line)
                if data.get("event") == "on_command":
                    cmd_data = data.get("data", {})
                    cmd = cmd_data.get("command")
                    if cmd in self.command_handlers:
                        self.command_handlers[cmd](cmd_data)
                elif data.get("event") == "on_message_received":
                    msg_data = data.get("data", {})
                    for handler in self.message_handlers:
                        handler(msg_data)
            except Exception:
                pass

plugin = Plugin()

@plugin.command("pytest")
def handle_pytest(data):
    plugin.show_toast("🚀 Python плагин работает! Команда /pytest вызвана.")

@plugin.on_message
def handle_msg(msg):
    text = msg.get("text", "")
    if text == "!ping":
        plugin.show_toast("🏓 Pong! (Python плагин)")

plugin.run()
