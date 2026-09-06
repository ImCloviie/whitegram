import json
import paramiko

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(
	"31.76.111.240",
	username="whitegram",
	password="KFd@&*SDd78@fds@g",
	timeout=15,
)
payload = json.dumps({"betaVersion": "2", "betaLabel": "beta"}, indent=2)
sftp = client.open_sftp()
with sftp.file("/home/whitegram/backend/data/client-info.json", "w") as f:
	f.write(payload)
sftp.close()
client.exec_command("sudo systemctl restart whitegram-backend")
client.close()
print("updated")
