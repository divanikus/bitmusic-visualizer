@echo off
setlocal
cd /d "%~dp0"
start "" /wait "%~dp0bitmusic_visualizer.exe" --software-scopes --diagnostics
