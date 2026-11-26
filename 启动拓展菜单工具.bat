@echo off
chcp 65001 >nul
title Sony A7R3 Pure-RAM Extension Menu Console

cd /d "%~dp0"

rem Pick a Python launcher: prefer local .venv, then "python", then "py -3".
set "PY="
if exist "%~dp0.venv\Scripts\python.exe" set "PY=%~dp0.venv\Scripts\python.exe"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY where py >nul 2>nul && set "PY=py -3"
if not defined PY (
    echo [!] Python 3.10+ was not found. Install it from https://www.python.org/ and tick "Add to PATH",
    echo     or install the "py" launcher, then run this file again.
    pause
    exit /b 1
)

"%PY%" "%~dp0tools\a7r3_addon_console.py" %*
if errorlevel 1 pause
