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


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    if SRC.exists():
        shutil.rmtree(SRC, onerror=lambda f, p, e: (os.chmod(p, 0o700), f(p)))
    run(["git", "clone", "-q", "--depth", "1", "--branch", TAG, REPO, str(SRC)])

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
