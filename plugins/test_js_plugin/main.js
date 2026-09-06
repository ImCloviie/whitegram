const readline = require('readline');

class Plugin {
    constructor() {
        this.commandHandlers = new Map();
    }

    onCommand(name, handler) {
        this.commandHandlers.set(name, handler);
        this._rpcCall('register_command', { command: name });
    }

    _rpcCall(method, params = {}) {
        const payload = { jsonrpc: '2.0', method, params };
        process.stdout.write(JSON.stringify(payload) + '\n');
    }

    showToast(text) {
        this._rpcCall('show_toast', { text });
    }

    run() {
        const rl = readline.createInterface({
            input: process.stdin,
            output: process.stdout,
            terminal: false
        });

        rl.on('line', (line) => {
            if (!line.trim()) return;
            try {
                const data = JSON.parse(line);
                if (data.event === 'on_command') {
                    const cmd = data.data ? data.data.command : null;
                    if (cmd && this.commandHandlers.has(cmd)) {
                        this.commandHandlers.get(cmd)(data.data || {});
                    }
                }
            } catch (e) {}
        });
    }
}

const plugin = new Plugin();

plugin.onCommand('jstest', (data) => {
    plugin.showToast('⚡ JavaScript плагин работает! Команда /jstest вызвана.');
});

plugin.run();
