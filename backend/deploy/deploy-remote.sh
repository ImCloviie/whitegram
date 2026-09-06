#!/bin/bash
set -euo pipefail

APP_DIR="${HOME}/backend"
cd "${APP_DIR}"

npm install --omit=dev

if [[ ! -f .env ]]; then
	cp .env.production .env
fi

sudo systemctl restart whitegram-backend
sudo systemctl --no-pager status whitegram-backend
