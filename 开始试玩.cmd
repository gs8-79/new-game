@echo off
chcp 65001 >nul
pushd "%~dp0"
mud-demos.exe
echo.
pause
popd

