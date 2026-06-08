@echo off
rem ====================================================================
rem Focus Mode Engine V2 – Quick launch script
rem ====================================================================

rem ---- Change to the project directory ----
cd /d "%~dp0"

rem ---- OPTIONAL: Re‑compile the source ----
rem Uncomment the next line if you want to force a rebuild each time.
rem call build.bat

rem ---- Start the unified Focus Engine (starts daemon and tray UI) ----
start "Focus" "focus.exe"

echo.
echo ==============================
echo Focus Mode Engine launched.
echo Check the system‑tray for the Focus icon.
echo ==============================

rem Keep the batch window open so you can see any error messages.
pause
