#!/usr/bin/env python3
"""Build a signed tx64upd* pack and refresh current.json."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK_BAT = ROOT / "Telegram/build/whitegram_pack_update.bat"
UPDATES_DIR = ROOT / "backend/data/updates"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import version_utils as vu


def main() -> int:
	if not PACK_BAT.exists():
		print(f"Missing {PACK_BAT}")
		return 1
	result = subprocess.run(
		["cmd", "/c", str(PACK_BAT)],
		cwd=ROOT,
	)
	if result.returncode != 0:
		return result.returncode
	app_version = vu.read_app_version()
	pack_name = f"tx64upd{app_version}"
	pack_path = UPDATES_DIR / pack_name
	if not pack_path.exists():
		print(f"Expected pack at {pack_path}")
		return 1
	vu.update_manifest(app_version, pack_name)
	print(f"Ready: {pack_path} ({pack_path.stat().st_size} bytes)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
