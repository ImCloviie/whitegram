#!/usr/bin/env python3
"""Upload signed update pack and manifests to the Whitegram VDS."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
HOST = "31.76.111.240"
USER = "whitegram"
PASSWORD = "KFd@&*SDd78@fds@g"

VERSION_FILE = ROOT.parent / "Telegram/build/version"
CURRENT_JSON = ROOT / "data/updates/current.json"
CLIENT_INFO = ROOT / "data/client-info.json"
REMOTE_DATA = "/home/whitegram/backend/data"


def read_app_version() -> int:
	for line in VERSION_FILE.read_text(encoding="utf-8").splitlines():
		if line.startswith("AppVersion"):
			return int(line.split()[1])
	raise RuntimeError("AppVersion not found")


def main() -> int:
	app_version = read_app_version()
	pack_name = f"tx64upd{app_version}"
	local_pack = ROOT / "data/updates" / pack_name
	if not local_pack.exists():
		print(f"Missing {local_pack}")
		print("Run: python backend/deploy/pack_update.py")
		return 1
	manifest = json.loads(CURRENT_JSON.read_text(encoding="utf-8"))
	link = manifest.get("win64", {}).get("stable", {}).get("link", "")
	if link != f"/updates/{pack_name}":
		print(f"Warning: current.json link is {link!r}, expected /updates/{pack_name}")

	client = paramiko.SSHClient()
	client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
	client.connect(HOST, username=USER, password=PASSWORD, timeout=60)
	sftp = client.open_sftp()
	try:
		remote_pack = f"{REMOTE_DATA}/updates/{pack_name}"
		print(f"Uploading {local_pack.name} ({local_pack.stat().st_size} bytes)...")
		sftp.put(str(local_pack), remote_pack)
		with sftp.file(f"{REMOTE_DATA}/updates/current.json", "w") as f:
			f.write(CURRENT_JSON.read_text(encoding="utf-8"))
		print("uploaded current.json")
		with sftp.file(f"{REMOTE_DATA}/client-info.json", "w") as f:
			f.write(CLIENT_INFO.read_text(encoding="utf-8"))
		print("uploaded client-info.json")
	finally:
		sftp.close()
	client.exec_command("sudo systemctl restart whitegram-backend")
	client.close()
	print("done")
	return 0


if __name__ == "__main__":
	sys.exit(main())
