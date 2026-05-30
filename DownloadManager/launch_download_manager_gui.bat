@echo off
cd /d "%~dp0"
python download_manager_gui.py
if errorlevel 1 pause
