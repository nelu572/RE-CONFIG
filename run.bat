@echo off
if not exist dist\reconfig.exe (
    echo ERROR: dist\reconfig.exe not found. Run build.bat first.
    exit /b 1
)
start "" dist\reconfig.exe
