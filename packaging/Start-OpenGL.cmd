@echo off
setlocal
cd /d "%~dp0"
set "QSG_RHI_BACKEND=opengl"
start "" /wait "%~dp0bitmusic_visualizer.exe" --diagnostics
