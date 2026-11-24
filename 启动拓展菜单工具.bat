@echo off
chcp 65001 >nul
title Sony A7R3 纯内存拓展菜单控制台
python "%~dp0tools\a7r3_addon_console.py" %*
if errorlevel 1 pause
