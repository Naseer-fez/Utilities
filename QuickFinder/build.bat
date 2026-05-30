@echo off
echo ============================================
echo   QuickFinder - Ultra-Fast Build System
echo ============================================
echo.

if not exist "bin" mkdir bin

echo [1/2] Compiling resources...
windres src/resources.rc -O coff -o bin/resources.res

echo [2/2] Compiling with maximum optimizations...
g++ -O3 -march=native -std=c++20 -mwindows -municode ^
    -flto -fomit-frame-pointer -funroll-loops ^
    src/main.cpp src/search_engine.cpp src/word_finder_engine.cpp src/gui.cpp bin/resources.res ^
    -o bin/QuickFinder.exe ^
    -ldwmapi -lshlwapi -lole32 -lcomctl32 -luxtheme -lgdi32 ^
    -static-libgcc -static-libstdc++

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Built: bin\QuickFinder.exe
exit /b 0
