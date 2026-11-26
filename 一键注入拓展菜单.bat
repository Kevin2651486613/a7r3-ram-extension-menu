@echo off
chcp 65001 >nul
title Sony A7R3 One-Click Pure-RAM Injector

net session >nul 2>&1
if errorlevel 1 (
    echo [*] Requesting Administrator privilege for automatic Sony USB driver setup/restore...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%*' -WorkingDirectory '%~dp0' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"

set "PY="
if exist "%~dp0.venv\Scripts\python.exe" set "PY=%~dp0.venv\Scripts\python.exe"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY where py >nul 2>nul && set "PY=py -3"
if not defined PY (
    echo [!] Python 3.10+ was not found. Install it from https://www.python.org/ and tick "Add to PATH",
    echo     or create a .venv in this folder, then run this script again.
    pause
    exit /b 1
)

"%PY%" -c "import importlib.util, sys; ok = all(importlib.util.find_spec(m) for m in ('keystone', 'usb')) and bool(importlib.util.find_spec('Cryptodome') or importlib.util.find_spec('Crypto')); sys.exit(0 if ok else 1)" >nul 2>nul
if errorlevel 1 (
    echo [*] Missing Python packages detected. Installing requirements.txt...
    "%PY%" -m pip install -r "%~dp0requirements.txt"
    if errorlevel 1 (
        echo [!] Failed to install Python dependencies from requirements.txt.
        echo     Please check your network connection or run: pip install -r requirements.txt
        pause
        exit /b 1
    )
)

"%PY%" "%~dp0tools\a7r3_addon_console.py" one-click %*
set "RC=%errorlevel%"
echo.
if not "%RC%"=="0" (
    echo [!] One-click flow finished with exit code %RC%.
)
pause
exit /b %RC%
