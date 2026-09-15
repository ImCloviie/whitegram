#!/usr/bin/env python3
"""Upload signed update pack and manifests to the Whitegram VDS."""

from __future__ import annotations

import json
import os
import shlex
import sys
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
HOST = os.environ.get("WHITEGRAM_DEPLOY_HOST", "31.76.111.240")
USER = os.environ.get("WHITEGRAM_DEPLOY_USER", "deploy")
SSH_KEY = Path(os.environ.get(
	"WHITEGRAM_DEPLOY_KEY",
	Path.home() / ".ssh/vps_deploy_ed25519",
))

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

	if not SSH_KEY.is_file():
		raise RuntimeError(f"SSH key not found: {SSH_KEY}")

	client = paramiko.SSHClient()
	client.load_system_host_keys()
	client.connect(
		HOST,
		username=USER,
		key_filename=str(SSH_KEY),
		look_for_keys=False,
		allow_agent=False,
		timeout=60,
	)
	remote_pack = f"{REMOTE_DATA}/updates/{pack_name}"
	uploads = (
		(local_pack, remote_pack),
		(CURRENT_JSON, f"{REMOTE_DATA}/updates/current.json"),
		(CLIENT_INFO, f"{REMOTE_DATA}/client-info.json"),
	)
	sftp = client.open_sftp()
	try:
		for local, remote in uploads:
			temporary = f"/tmp/{Path(remote).name}.{os.getpid()}"
			print(f"Uploading {local.name} ({local.stat().st_size} bytes)...")
			sftp.put(str(local), temporary)
			command = "sudo install -m 0644 -- {source} {target}".format(
				source=shlex.quote(temporary),
				target=shlex.quote(remote),
			)
			_, stdout, stderr = client.exec_command(command)
			status = stdout.channel.recv_exit_status()
			if status:
				raise RuntimeError(stderr.read().decode("utf-8", errors="replace"))
			print(f"uploaded {Path(remote).name}")
	finally:
		sftp.close()
	_, stdout, stderr = client.exec_command(
		"sudo systemctl restart whitegram-backend"
	)
	status = stdout.channel.recv_exit_status()
	if status:
		raise RuntimeError(stderr.read().decode("utf-8", errors="replace"))
	client.close()
	print("done")
	return 0


if __name__ == "__main__":
	sys.exit(main())
