@echo off
setlocal enabledelayedexpansion
REM Usage: scripts\build.bat [options] (run with /help for details)

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_DIR=%SCRIPT_DIR%\.."

set "BUILD_TYPE=Release"
set "CMAKE_OPTS="

:parse_args
if "%~1"=="" goto :build
if /i "%~1"=="/samples"     set "CMAKE_OPTS=!CMAKE_OPTS! -DRSID_SAMPLES=ON" & shift & goto :parse_args
if /i "%~1"=="/python"      set "CMAKE_OPTS=!CMAKE_OPTS! -DRSID_PY=ON"      & shift & goto :parse_args
if /i "%~1"=="/debug"       set "BUILD_TYPE=Debug"                           & shift & goto :parse_args
if /i "%~1"=="/help"     goto :usage
if /i "%~1"=="/?"        goto :usage
echo Unknown option: %~1 (try /help)
exit /b 1

:usage
echo Build RealSenseID Host SDK (Release by default)
echo Usage: scripts\build.bat [options]
echo   /samples      Build C/C++/C#/Python samples
echo   /python       Build Python wrapper (rsid_py)
echo   /debug        Build Debug instead of Release (output to build-debug\)
exit /b 0

:build
set "BUILD_DIR=%PROJECT_DIR%\build"
if /i "%BUILD_TYPE%"=="Debug" set "BUILD_DIR=%PROJECT_DIR%\build-debug"

where cmake >nul 2>nul
if errorlevel 1 (
    echo Error: cmake not found. Please install CMake and ensure it is on your PATH.
    exit /b 1
)

echo === RealSenseID Host SDK ===
echo Build type: %BUILD_TYPE%
echo.

cmake -B "%BUILD_DIR%" -DRSID_PREVIEW=ON %CMAKE_OPTS% "%PROJECT_DIR%"
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %NUMBER_OF_PROCESSORS%
if errorlevel 1 exit /b 1

echo.
echo === Build complete ===
echo Binaries: %BUILD_DIR%\bin\%BUILD_TYPE%
echo Libraries: %BUILD_DIR%\lib\%BUILD_TYPE%
