@echo off
rem Opens the Whitegram admin panel. The panel answers only on the VDS itself,
rem so it is reached through an SSH tunnel with the deploy key.
set "KEY=%USERPROFILE%\.ssh\vps_deploy_ed25519"
if not exist "%KEY%" (
  echo Deploy key not found: %KEY%
  pause
  exit /b 1
)
echo Opening the tunnel. Close this window to close the admin panel.
start "" /b cmd /c "timeout /t 3 /nobreak >nul & start "" http://127.0.0.1:18787/admin"
ssh -i "%KEY%" -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 -N -L 18787:127.0.0.1:8787 deploy@31.76.111.19
