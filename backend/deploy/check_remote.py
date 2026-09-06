import paramiko

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(
	"31.76.111.240",
	username="whitegram",
	password="KFd@&*SDd78@fds@g",
	timeout=15,
)
cmds = [
	"pgrep -af 'node.*server.js' || true",
	"systemctl is-active whitegram-backend 2>&1 || true",
	"systemctl is-enabled whitegram-backend 2>&1 || true",
]
for cmd in cmds:
	print(f"\n$ {cmd}")
	_, stdout, stderr = client.exec_command(cmd)
	print(stdout.read().decode("utf-8", errors="replace"))
client.close()
