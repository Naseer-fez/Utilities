@echo off
echo ===================================================
echo   Compiling Focus Mode Engine V2 (C++17 Native Win32)
echo ===================================================

:: 1. Cleanup old executables if existing
if exist focus.exe del focus.exe
if exist focus_daemon.exe del focus_daemon.exe
if exist focus_tray.exe del focus_tray.exe
if exist focus_watchdog.exe del focus_watchdog.exe
if exist src\resource.res del src\resource.res

:: 2. Compile resources
echo Compiling resources...
windres src/resource.rc -O coff -o src/resource.res
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed!
    exit /b %errorlevel%
)

:: 3. Compile Unified Executable focus.exe
echo Compiling focus.exe...
g++ -std=c++17 -O2 -mwindows -municode -static-libgcc -static-libstdc++ ^
    -o focus.exe ^
    src/main.cpp ^
    src/daemon/main.cpp src/daemon/engine.cpp src/daemon/blocking.cpp ^
    src/tray/main.cpp src/tray/gui.cpp ^
    src/watchdog/main.cpp ^
    src/common/config.cpp src/common/state.cpp src/common/utils.cpp ^
    src/resource.res ^
    -luser32 -lgdi32 -lole32 -luuid -lshell32 -lcomctl32 -lcomdlg32
if %errorlevel% neq 0 (
    echo [ERROR] Focus Engine compilation failed!
    exit /b %errorlevel%
)

echo ===================================================
echo   Compilation Successful!
echo   focus.exe created.
echo ===================================================
