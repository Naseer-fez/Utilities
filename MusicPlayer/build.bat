@echo off
echo Compiling Music Player...
windres src\resource.rc -O coff -o src\resource.res
g++ -std=c++17 -O2 -mwindows -municode -static-libgcc -static-libstdc++ -o player.exe src\main.cpp src\PlayerGUI.cpp src\AudioEngine.cpp src\Shuffler.cpp src\resource.res -lwinmm -luser32 -lgdi32 -lcomdlg32 -lole32 -lcomctl32
if %errorlevel% neq 0 (
    echo Compilation failed!
    exit /b %errorlevel%
)

echo Compilation successful! player.exe created.
echo.
echo Building Installer...
makensis installer.nsi
if %errorlevel% neq 0 (
    echo Installer build failed!
    exit /b %errorlevel%
)

echo Installer built successfully! AntigravityPlayerSetup.exe created.
