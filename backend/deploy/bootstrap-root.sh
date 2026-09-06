#!/bin/bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
	echo "Run as root: sudo bash bootstrap-root.sh"
	exit 1
fi

USERNAME="whitegram"
PASSWORD='KFd@&*SDd78@fds@g'
APP_DIR="/home/${USERNAME}/backend"

if ! id "${USERNAME}" >/dev/null 2>&1; then
	useradd -m -s /bin/bash "${USERNAME}"
fi

echo "${USERNAME}:${PASSWORD}" | chpasswd

cat >/etc/sudoers.d/whitegram-backend <<'EOF'
whitegram ALL=(ALL) NOPASSWD: /bin/systemctl restart whitegram-backend, /bin/systemctl status whitegram-backend, /bin/systemctl enable whitegram-backend
EOF
chmod 440 /etc/sudoers.d/whitegram-backend

if ! command -v node >/dev/null 2>&1; then
	curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
	apt-get install -y nodejs
fi

if command -v ufw >/dev/null 2>&1; then
	ufw allow OpenSSH
	ufw allow 8787/tcp
	ufw --force enable || true
fi

if command -v firewall-cmd >/dev/null 2>&1; then
	firewall-cmd --permanent --add-port=8787/tcp || true
	firewall-cmd --reload || true
fi

mkdir -p "${APP_DIR}"
chown -R "${USERNAME}:${USERNAME}" "/home/${USERNAME}"

cat >/etc/systemd/system/whitegram-backend.service <<EOF
[Unit]
Description=Whitegram Backend
After=network.target

[Service]
Type=simple
User=${USERNAME}
WorkingDirectory=${APP_DIR}
Environment=NODE_ENV=production
EnvironmentFile=${APP_DIR}/.env
ExecStart=/usr/bin/node ${APP_DIR}/src/server.js
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable whitegram-backend.service

echo "Root bootstrap complete."
echo "User ${USERNAME} ready. Deploy files to ${APP_DIR}, then:"
echo "  systemctl restart whitegram-backend"
echo "  systemctl status whitegram-backend"
