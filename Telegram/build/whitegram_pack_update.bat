@echo OFF
setlocal enabledelayedexpansion

set "FullScriptPath=%~dp0"
set "HomePath=%FullScriptPath%.."
set "ReleasePath=%HomePath%\..\out\Release"

if not exist "%ReleasePath%\Whitegram.exe" (
  echo Missing %ReleasePath%\Whitegram.exe
  echo Build Release first: cmake --build out --config Release --target Telegram
  exit /b 1
)
if not exist "%ReleasePath%\Updater.exe" (
  echo Missing %ReleasePath%\Updater.exe
  exit /b 1
)
if not exist "%ReleasePath%\Packer.exe" (
  echo Missing %ReleasePath%\Packer.exe
  echo Build the Packer target in Release configuration.
  exit /b 1
)

set "KeysLoc=%HomePath%\Resources\update_whitegram"
set "LocalKey=%HomePath%\..\backend\deploy\keys\v2\release-local-private.pem"
set "LocalKeyId=wg-rl-2026a"
if not exist "%KeysLoc%\root-public.pem" (
  echo Missing %KeysLoc%\root-public.pem
  echo Run: python backend\deploy\setup_update_signing_v2.py
  exit /b 1
)
if not exist "%LocalKey%" (
  echo Missing %LocalKey%
  echo Run: python backend\deploy\setup_update_signing_v2.py
  exit /b 1
)

set "AppVersion="
for /f "tokens=1,2* delims= " %%i in (%FullScriptPath%version) do (
  if /i "%%i"=="AppVersion" set "AppVersion=%%j"
)
if "%AppVersion%"=="" (
  echo Could not read AppVersion from build\version
  exit /b 1
)

rem v2 envelope: td-update-win-x64-<AppVersion>, signed in one pass with
rem the local Ed25519 channel key (see backend/deploy/setup_update_signing_v2.py).
set "UpdateFile=td-update-win-x64-%AppVersion%"
set "D3D=modules\x64\d3d\d3dcompiler_47.dll"

cd /d "%ReleasePath%"
if exist "%UpdateFile%" del /q "%UpdateFile%"

if exist "%D3D%" (
  Packer.exe -version %AppVersion% -path Whitegram.exe -path Updater.exe -path "%D3D%" -target win64 -channel stable -keys-loc "%KeysLoc%" -local-key "%LocalKey%" -local-key-id %LocalKeyId%
) else (
  echo Warning: %D3D% not found, packing without it.
  Packer.exe -version %AppVersion% -path Whitegram.exe -path Updater.exe -target win64 -channel stable -keys-loc "%KeysLoc%" -local-key "%LocalKey%" -local-key-id %LocalKeyId%
)
if errorlevel 1 exit /b 1

if not exist "%UpdateFile%" (
  echo Packer did not create %UpdateFile%
  exit /b 1
)

set "BackendUpdates=%HomePath%\..\backend\data\updates"
if not exist "%BackendUpdates%" mkdir "%BackendUpdates%"
copy /y "%UpdateFile%" "%BackendUpdates%\%UpdateFile%" >nul

echo.
echo Created %ReleasePath%\%UpdateFile%
echo Copied to %BackendUpdates%\%UpdateFile%
echo.
echo Update backend/data/updates/current.json link to /updates/%UpdateFile%
echo Then run: python backend\deploy\push_update.py

exit /b 0
