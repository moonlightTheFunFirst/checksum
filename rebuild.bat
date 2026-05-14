@echo off
setlocal

set "ROOT=%~dp0."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\build"

if exist "%BUILD_DIR%" (
    echo Cleaning "%BUILD_DIR%"
    rmdir /s /q "%BUILD_DIR%"
    if errorlevel 1 exit /b %errorlevel%
)

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%"
exit /b %errorlevel%
