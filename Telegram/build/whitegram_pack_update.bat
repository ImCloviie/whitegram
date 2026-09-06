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
  echo Run: python backend\deploy\setup_update_signing.py
  echo Then reconfigure CMake and rebuild Telegram target.
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

set "UpdateFile=tx64upd%AppVersion%"
set "D3D=modules\x64\d3d\d3dcompiler_47.dll"

cd /d "%ReleasePath%"
if exist "%UpdateFile%" del /q "%UpdateFile%"

if exist "%D3D%" (
  Packer.exe -version %AppVersion% -path Whitegram.exe -path Updater.exe -path "%D3D%" -target win64
) else (
  echo Warning: %D3D% not found, packing without it.
  Packer.exe -version %AppVersion% -path Whitegram.exe -path Updater.exe -target win64
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
