@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo [1/3] Initializing MSVC x64 Build Environment...
echo ===================================================
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cd /d "%~dp0"

echo.
echo ===================================================
echo [2/3] Configuring CMake Project (MSVC Release)...
echo ===================================================
cmake -B build -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ===================================================
echo [3/3] Building CH570_Monitor.exe (Release)...
echo ===================================================
cmake --build build --config Release

if %ERRORLEVEL% equ 0 (
    echo.
    echo ===================================================
    echo [SUCCESS] Build finished successfully!
    echo Output binary: %~dp0build\Release\CH570_Monitor.exe
    echo ===================================================
) else (
    echo.
    echo [ERROR] Build failed!
)
