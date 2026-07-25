@echo off
chcp 65001 >nul
title VEX + HWiNFO64 - Build
setlocal enabledelayedexpansion

echo ==================================================
echo   VEX + HWiNFO64 - BUILD (LLVM/ClangCL)
echo ==================================================
echo.
echo NOTE: No driver embedding needed.
echo The cheat uses HWiNFO's existing kernel driver.
echo Just have HWiNFO64 running before you launch the cheat.
echo.
echo PREREQUISITES (install once):
echo   1. Visual Studio 2022 Community
echo   2. LLVM/Clang toolset - In VS Installer:
echo      Workloads > Desktop development with C++
echo      Individual components > "C++ Clang tools for Windows"
echo.
echo NOTE: No DirectX SDK needed since D3DX11 removed.

:: Check admin
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] NOT running as admin! Please right-click and "Run as Administrator"
    pause
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

:: Check ClangCL (both standard and user's D: drive path)
set "PF86=%ProgramFiles(x86)%"
set "CLANG_CHECK="
if exist "%PF86%\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe" set "CLANG_CHECK=1"
if exist "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe" set "CLANG_CHECK=1"
if defined CLANG_CHECK (
    echo [+] ClangCL: found
) else (
    echo [!] ClangCL not found.
    echo     Open Visual Studio Installer:
    echo       Workloads > Desktop development with C++
    echo       Individual components > "C++ Clang tools for Windows"
    echo     After install, re-run this script.
    pause
    exit /b 1
)

:: Find MSBuild
echo.
echo [*] Finding MSBuild...
set "MSBUILD="
set "PF=%ProgramFiles%"

if exist "%PF%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=%PF%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" & goto :msbuild_found
if exist "D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" & goto :msbuild_found
if exist "%PF%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=%PF%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" & goto :msbuild_found
if exist "%PF%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=%PF%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" & goto :msbuild_found
if exist "%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "delims=" %%a in ('"%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" -find MSBuild\**\Bin\MSBuild.exe') do (
        set "MSBUILD=%%a"
        goto :msbuild_found
    )
)

:msbuild_found
if not defined MSBUILD (
    echo [!] MSBuild not found - install Visual Studio 2022
    pause
    exit /b 1
)
echo [+] MSBuild: %MSBUILD%

:: Verify project files
echo.
echo [*] Verifying project files...
if not exist "VEX.sln" (
    echo [!] Missing: VEX.sln
    pause
    exit /b 1
)
if not exist "VEX\main.cpp" (
    echo [!] Missing: VEX\main.cpp
    pause
    exit /b 1
)
echo [+] All project files present

:: Build
echo.
echo [*] Building with MSBuild (Release x64, ClangCL)...
"%MSBUILD%" VEX.sln /p:Configuration=Release /p:Platform=x64 /nologo /v:q
if %errorlevel% equ 0 (
    echo.
    echo ================================================
    echo   BUILD SUCCESSFUL
    echo ================================================
    echo.
    echo Output: VEX\bin\Release\x64\VEX.exe
    echo.
    echo HOW TO USE:
    echo   1. Run HWiNFO64.exe as Administrator
    echo   2. Run VEX.exe as Administrator
    echo   3. F10 = PANIC
    echo.
    echo Offsets: LIVE from internet on each launch.
) else (
    echo.
    echo [!] Build had errors.
    echo     Common fixes:
    echo     - Make sure Clang toolset is installed (VS Installer)
    echo     - Make sure DirectX SDK is at C:\DXSDK
    echo     - Open VEX.sln in Visual Studio for detailed errors
)

echo.
pause
