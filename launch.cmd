@echo off
if not exist "%~dp0build\native\bitmusic_visualizer.exe" (
  echo Run build-native.ps1 -Deploy first.
  exit /b 1
)
start "" "%~dp0build\native\bitmusic_visualizer.exe" %*
