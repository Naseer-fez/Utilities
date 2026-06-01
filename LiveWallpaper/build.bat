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
echo Building Rust Direct3D 11 Shader Host library...
cd live_wallpaper_rust
cargo build --release
if %ERRORLEVEL% neq 0 (
    echo Rust library build failed!
    cd ..
    pause
    exit /b %ERRORLEVEL%
)
cd ..

echo.
echo Copying live_wallpaper_rust.dll to root directory...
copy /y "live_wallpaper_rust\target\release\live_wallpaper_rust.dll" "live_wallpaper_rust.dll"
if %ERRORLEVEL% neq 0 (
    echo Failed to copy live_wallpaper_rust.dll to root directory!
    pause
    exit /b %ERRORLEVEL%
)


echo.
echo Build completed successfully! LiveWallpaper.exe is now in the root directory.
pause
