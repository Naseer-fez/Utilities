@echo off
title StudyFlow AI - High-End Launcher
color 0B
echo =======================================================================
echo          STUDYFLOW AI: AGENTIC RAG STUDY ASSISTANT LAUNCHER
echo =======================================================================
echo.

cd /d "%~dp0"

:: Step 1: Check for Python installation
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Python is not installed or not in your PATH.
    pause
    exit /b 1
)

:: Step 2: Set up virtual environment
if not exist ".venv" (
    echo [INFO] Creating Python virtual environment (.venv)...
    python -m venv .venv
    if %errorlevel% neq 0 (
        echo [ERROR] Failed to create virtual environment.
        pause
        exit /b 1
    )
)

:: Step 3: Activate venv and install dependencies
echo [INFO] Activating virtual environment...
call .venv\Scripts\activate.bat

echo [INFO] Installing required python backend packages...
pip install -r requirements.txt
if %errorlevel% neq 0 (
    echo [ERROR] Failed to install Python dependencies.
    pause
    exit /b 1
)

:: Step 4: Create default directories
if not exist "study_docs" mkdir study_docs
if not exist "backend\db" mkdir backend\db
if not exist ".env" copy .env.example .env >nul

:: Step 5: Check and configure Frontend React packages
echo [INFO] Checking frontend Node packages...
if not exist "frontend\node_modules" (
    echo [INFO] Installing frontend npm packages (this may take a minute)...
    cd frontend
    cmd /c "npm install"
    cd ..
)

echo =======================================================================
echo Please select run mode:
echo   [1] Production Mode (Vite built production bundle, served at http://localhost:8000)
echo   [2] Developer Mode (FastAPI port 8000 + Vite Hot-Reload port 5173)
echo =======================================================================
set /p runmode="Enter choice [1 or 2, default is 1]: "

if "%runmode%"=="2" (
    echo.
    echo [INFO] Launching Developer Mode...
    echo [INFO] Booting FastAPI backend on http://127.0.0.1:8000
    echo [INFO] Booting Vite React dev server on http://localhost:5173
    echo.

    :: Start FastAPI backend in a separate window
    start "StudyFlow Backend Server" cmd /k "call .venv\Scripts\activate && uvicorn backend.app:app --host 127.0.0.1 --port 8000 --reload"

    :: Start Vite frontend server in a separate window
    cd frontend
    start "StudyFlow Vite Frontend" cmd /k "npm run dev"
    cd ..

    :: Open developer browser after 4 seconds
    start /b cmd /c "timeout /t 4 >nul && start http://localhost:5173"

    echo [SUCCESS] Both servers running concurrently in background command lines.
    echo You can now use the hot-reload interface!
    pause
    exit /b 0
)

:: Option 1: Build & Serve Unified Production
echo.
echo [INFO] Packaging and building React production bundle...
if not exist "frontend\dist" (
    cd frontend
    cmd /c "npm run build"
    cd ..
) else (
    echo [INFO] Built React files found. (Delete 'frontend\dist' to force rebuild).
)

echo.
echo [INFO] Starting Unified Production server...
echo [INFO] Once started, access your study space at: http://localhost:8000
echo.

:: Automatically open browser after 3 seconds
start /b cmd /c "timeout /t 3 >nul && start http://localhost:8000"

:: Start Uvicorn serving backend and statically serving built React
uvicorn backend.app:app --host 0.0.0.0 --port 8000

pause
