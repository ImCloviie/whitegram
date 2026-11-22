"""Builds the headless tg-ws-proxy used by the second bypass block.

Clones Flowseal/tg-ws-proxy at a pinned tag (MIT), builds only its console
proxy (proxy/tg_ws_proxy.py, no tray UI) with PyInstaller as a windowless
one-file exe and puts it where Whitegram looks for it:

    out/Release/modules/x64/tgwsproxy/tg-ws-proxy.exe

Run from the repository root:  python Telegram/build/whitegram_build_tgwsproxy.py
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

TAG = "v1.11.4"
REPO = "https://github.com/Flowseal/tg-ws-proxy.git"
ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "out" / "tgwsproxy"
SRC = WORK / "src"
VENV = WORK / "venv"
TARGET = ROOT / "out" / "Release" / "modules" / "x64" / "tgwsproxy" / "tg-ws-proxy.exe"
DEPS = ["pyinstaller>=6.16", "certifi", "httpx[http2]==0.28.1", "cryptography==46.0.5"]


def run(cmd, **kw):
    print("$", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


POOL_PATCHES = [
    # An hour-long pause after failures left DC2/DC4 dead long after the network came back.
    ("REFILL_BACKOFF_MAX = 3600.0", "REFILL_BACKOFF_MAX = 30.0", 2),
    # Plain SNI is blocked in Russia, so try the sprinthost.ru front first, as bypass 1 does.
    ("self.try_fronting_first = False", "self.try_fronting_first = True", 2),
    (
        "        stats.pool_misses += 1\n"
        "        self._schedule_refill(key, target_ip, domains)\n"
        "        return None\n",
        "        stats.pool_misses += 1\n"
        "        self._schedule_refill(key, target_ip, domains)\n"
        "        if self._refill_failures.get(key):\n"
        "            # WS to this DC failed recently: fall back at once instead of waiting.\n"
        "            return None\n"
        "        try:\n"
        "            ws = await asyncio.wait_for(self._connect_one(\n"
        "                target_ip, domains,\n"
        "                WS_PATH_TEST if is_test_dc else WS_PATH), timeout=3)\n"
        "        except Exception:\n"
        "            ws = None\n"
        "        if ws is not None:\n"
        "            self._refill_failures.pop(key, None)\n"
        "            self._refill_after.pop(key, None)\n"
        "        return ws\n",
        1,
    ),
]


RAW_WS_PATCHES = [
    # Since 16.11.2026 CF domain SNI is blocked: try CF hosts over TLS and port 80, last good first.
    (
        "    @staticmethod\n"
        "    async def connect(host: str, domain: str, timeout: float = 10.0,\n",
        "    _plain_first = False\n"
        "\n"
        "    @staticmethod\n"
        "    async def connect(host: str, domain: str, timeout: float = 10.0,\n"
        "                      path: str = '/apiws', *,\n"
        "                      sni: Optional[str] = None, secure = True) -> 'RawWebSocket':\n"
        "        if not secure or host != domain:\n"
        "            return await RawWebSocket._connect(\n"
        "                host, domain, timeout, path, sni=sni, secure=secure)\n"
        "        error = None\n"
        "        for tls in ((False, True) if RawWebSocket._plain_first else (True, False)):\n"
        "            try:\n"
        "                ws = await RawWebSocket._connect(\n"
        "                    host, domain, min(timeout, 5.0), path, secure=tls)\n"
        "            except Exception as exc:\n"
        "                error = exc\n"
        "                continue\n"
        "            if RawWebSocket._plain_first == tls:\n"
        "                log.info('CF proxy: switched to %s', 'TLS' if tls else 'port 80')\n"
        "            RawWebSocket._plain_first = not tls\n"
        "            return ws\n"
        "        raise error\n"
        "\n"
        "    @staticmethod\n"
        "    async def _connect(host: str, domain: str, timeout: float = 10.0,\n",
        1,
    ),
]


def patch_file(relpath: str, patches) -> None:
    """Applies Whitegram fixes; fails loudly if upstream changed."""
    path = SRC / relpath
    text = path.read_text(encoding="utf-8")
    for old, new, count in patches:
        found = text.count(old)
        if found != count:
            raise SystemExit(f"{relpath} patch anchor found {found}x, expected {count}x: {old[:60]!r}")
        text = text.replace(old, new)
    path.write_text(text, encoding="utf-8")
    print(f"patched {relpath}")


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    if SRC.exists():
        shutil.rmtree(SRC, onerror=lambda f, p, e: (os.chmod(p, 0o700), f(p)))
    run(["git", "clone", "-q", "--depth", "1", "--branch", TAG, REPO, str(SRC)])
    patch_file("proxy/pool.py", POOL_PATCHES)
    patch_file("proxy/raw_websocket.py", RAW_WS_PATCHES)

    python = VENV / "Scripts" / "python.exe"
    if not python.exists():
        run([sys.executable, "-m", "venv", str(VENV)])
    run([str(python), "-m", "pip", "install", "-q", "--disable-pip-version-check", *DEPS])

    entry = WORK / "tg_ws_proxy_entry.py"
    entry.write_text(
        "from proxy.tg_ws_proxy import main\n\nif __name__ == '__main__':\n    main()\n",
        encoding="utf-8")

    run([
        str(python), "-m", "PyInstaller",
        "--noconfirm", "--clean", "--onefile", "--noconsole",
        "--name", "tg-ws-proxy",
        "--paths", str(SRC),
        "--distpath", str(WORK / "dist"),
        "--workpath", str(WORK / "build"),
        "--specpath", str(WORK),
        "--collect-data", "certifi",
        "--hidden-import", "h2",
        "--hidden-import", "utils.logging_setup",
        "--hidden-import", "cryptography.hazmat.primitives.ciphers",
        str(entry),
    ])

    TARGET.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(WORK / "dist" / "tg-ws-proxy.exe", TARGET)
    shutil.copy2(SRC / "LICENSE", TARGET.parent / "LICENSE.txt")
    print(f"OK: {TARGET} ({TARGET.stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
