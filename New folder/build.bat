@echo off
setlocal enabledelayedexpansion

echo ============================================================================
echo LiveWallpaper — MinGW Build Script
echo ============================================================================

REM Check if g++ is in PATH
where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] g++ was not found in your PATH.
    echo Please make sure MinGW-w64 is installed and added to your System PATH.
    echo Current PATH: !PATH!
    pause
    exit /b 1
)

REM Check if windres is in PATH
where windres >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] windres was not found in your PATH.
    echo This is required to compile Windows resources.
    pause
    exit /b 1
)

REM Stop any running instances of LiveWallpaper.exe to release the file lock for compiling/linking
taskkill /f /im LiveWallpaper.exe >nul 2>&1

REM Setup build directories
if not exist bin mkdir bin
if not exist bin\objs mkdir bin\objs

echo.
echo [1/3] Compiling resources...
windres res/app.rc -O coff -o bin/objs/app_res.o
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Resource compilation failed.
    pause
    exit /b 1
)

echo [2/3] Compiling C++ source files...
set SOURCES=main config power_monitor renderer timer video_decoder wallpaper_host
for %%f in (%SOURCES%) do (
    echo   Compiling src/%%f.cpp...
    g++ -c src/%%f.cpp -o bin/objs/%%f.o -std=c++17 -Isrc -DUNICODE -D_UNICODE -municode -O2 -mstackrealign -Wall -Wextra -Wno-unknown-pragmas
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Compilation of src/%%f.cpp failed.
        pause
        exit /b 1
    )
)

echo [3/3] linking executable...
g++ bin/objs/main.o bin/objs/config.o bin/objs/power_monitor.o bin/objs/renderer.o bin/objs/timer.o bin/objs/video_decoder.o bin/objs/wallpaper_host.o bin/objs/app_res.o -o LiveWallpaper.exe -ld3d11 -ldxgi -lmf -lmfplat -lmfreadwrite -lmfuuid -lshlwapi -lshell32 -luser32 -lgdi32 -lole32 -luuid -lcomctl32 -lcomdlg32 -ldwmapi -lwtsapi32 -mwindows -municode -O2 -s -static-libgcc -static-libstdc++
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Linking failed.
    pause
    exit /b 1
)

echo.
echo ============================================================================
echo [SUCCESS] Build completed successfully!
echo Created: LiveWallpaper.exe
echo File size: 236 KB (approx)
echo ============================================================================
echo.
pause
