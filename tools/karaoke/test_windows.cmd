@echo off
setlocal
if not defined QT_ROOT set "QT_ROOT=D:\Libraries\win64\Qt-5.15.18"
if not defined ZLIB_ROOT set "ZLIB_ROOT=D:\Libraries\win64\zlib"
if not defined VS_ROOT set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
if not exist out\karaoke-tests mkdir out\karaoke-tests
cl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /Gy /DNOMINMAX /DQT_STATIC /DQT_NO_KEYWORDS /FI tools\karaoke\test_literals.h /I . /I Telegram\SourceFiles /I "%QT_ROOT%\include" /I "%ZLIB_ROOT%" tools\karaoke\tests.cpp Telegram\SourceFiles\ayu\ui\utils\lyrics_engine.cpp Telegram\SourceFiles\ayu\ui\utils\lyrics_align.cpp /Foout\karaoke-tests\ /Feout\karaoke-tests\tests.exe /link /OPT:REF /LIBPATH:"%QT_ROOT%\lib" Qt5Core.lib qtpcre2.lib "%ZLIB_ROOT%\Release\zlibstatic.lib" mpr.lib userenv.lib version.lib netapi32.lib ws2_32.lib advapi32.lib kernel32.lib ole32.lib shell32.lib uuid.lib user32.lib winmm.lib
if errorlevel 1 exit /b 1
out\karaoke-tests\tests.exe
