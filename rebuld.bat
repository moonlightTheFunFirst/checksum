@echo off
setlocal

set "ROOT=%~dp0."
set "BUILD_DIR=%ROOT%\build"

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%"
exit /b %errorlevel%
