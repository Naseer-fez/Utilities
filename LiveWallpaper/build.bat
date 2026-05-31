@echo off
echo Stopping running instances of LiveWallpaper...
taskkill /f /im LiveWallpaper.exe 2>nul

echo.
echo Configuring CMake...
cmake -S . -B build

echo.
echo Building project in Release mode...
cmake --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Copying executable to root directory...
copy /y "build\LiveWallpaper.exe" "LiveWallpaper.exe"
if %ERRORLEVEL% neq 0 (
    echo Failed to copy LiveWallpaper.exe to root directory!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed successfully! LiveWallpaper.exe is now in the root directory.
pause
