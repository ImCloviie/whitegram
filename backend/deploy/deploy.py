import io
import os
import sys
import tarfile
import time
from pathlib import Path

import paramiko

HOST = "31.76.111.240"
USER = "whitegram"
PASSWORD = "KFd@&*SDd78@fds@g"
REMOTE_DIR = "/home/whitegram/backend"
ROOT = Path(__file__).resolve().parents[1]

SKIP = {
	"node_modules",
	".git",
	"deploy/deploy.py",
	".env",
	"data",
}


def should_skip(rel: str) -> bool:
	parts = Path(rel).parts
	return any(part in SKIP for part in parts)


def build_archive() -> bytes:
	buffer = io.BytesIO()
	with tarfile.open(fileobj=buffer, mode="w:gz") as tar:
		for dirpath, dirnames, filenames in os.walk(ROOT):
			dirnames[:] = [d for d in dirnames if d not in SKIP and not d.startswith(".")]
			for fname in filenames:
				full_path = Path(dirpath) / fname
				rel = full_path.relative_to(ROOT).as_posix()
				if should_skip(rel):
					continue
				tar.add(full_path, arcname=rel)
	buffer.seek(0)
	return buffer.read()


def run(client: paramiko.SSHClient, command: str) -> None:
	print(f"$ {command}", flush=True)
	_, stdout, stderr = client.exec_command(command, get_pty=True)
	exit_code = stdout.channel.recv_exit_status()
	out = stdout.read().decode("utf-8", errors="replace")
	err = stderr.read().decode("utf-8", errors="replace")
	if out.strip():
		print(out.rstrip())
	if err.strip():
		print(err.rstrip(), file=sys.stderr)
	if exit_code != 0:
		raise RuntimeError(f"Command failed ({exit_code}): {command}")


def main() -> int:
	print(f"Building archive from {ROOT}...", flush=True)
	payload = build_archive()
	print(f"Archive size: {len(payload)} bytes", flush=True)

	client = paramiko.SSHClient()
	client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
	try:
		print(f"Connecting to {USER}@{HOST}...", flush=True)
		client.connect(
			HOST,
			username=USER,
			password=PASSWORD,
			timeout=30,
			allow_agent=False,
			look_for_keys=False,
		)
	except paramiko.AuthenticationException:
		print(
			"\nSSH authentication failed.\n"
			"Run on the VPS as root:\n"
			f"  curl -fsSL https://raw.githubusercontent.com/...  # or copy deploy/bootstrap-root.sh\n"
			f"  bash bootstrap-root.sh\n"
			"Then rerun: python deploy/deploy.py",
			file=sys.stderr,
		)
		return 1

	sftp = client.open_sftp()
	try:
		run(client, "mkdir -p /home/whitegram")
		remote_tar = "/home/whitegram/backend-deploy.tgz"
		with sftp.file(remote_tar, "wb") as remote_file:
			remote_file.write(payload)
		run(client, f"mkdir -p {REMOTE_DIR}")
		run(client, f"tar -xzf {remote_tar} -C {REMOTE_DIR}")
		run(client, f"rm -f {remote_tar}")
		run(client, f"chmod +x {REMOTE_DIR}/deploy/deploy-remote.sh")
		run(client, f"bash {REMOTE_DIR}/deploy/deploy-remote.sh")
	except paramiko.SSHException as error:
		print(f"Deploy error: {error}", file=sys.stderr)
		return 1
	finally:
		sftp.close()
		client.close()

	for _ in range(10):
		try:
			import urllib.request

			with urllib.request.urlopen(f"http://{HOST}:8787/health", timeout=5) as resp:
				print(resp.read().decode("utf-8"))
			print(f"Deployed: http://{HOST}:8787")
			return 0
		except Exception:
			time.sleep(2)

	print("Files uploaded, but health check did not pass yet.")
	print(f"Check on server: sudo systemctl status whitegram-backend")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
