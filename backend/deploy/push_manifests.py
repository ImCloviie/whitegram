import json
import paramiko
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = "31.76.111.240"
USER = "whitegram"
PASSWORD = "KFd@&*SDd78@fds@g"

files = {
	ROOT / "data/client-info.json": "/home/whitegram/backend/data/client-info.json",
	ROOT / "data/updates/current.json": "/home/whitegram/backend/data/updates/current.json",
}

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(HOST, username=USER, password=PASSWORD, timeout=15)
sftp = client.open_sftp()
for local, remote in files.items():
	with sftp.file(remote, "w") as remote_file:
		remote_file.write(local.read_text(encoding="utf-8"))
	print(f"uploaded {local.name}")
sftp.close()
client.exec_command("sudo systemctl restart whitegram-backend")
client.close()
print("done")
