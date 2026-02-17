@echo off
REM Build script for Autosave mod - Vice City Release configuration

setlocal enabledelayedexpansion

REM Try to find MSBuild using vswhere (if available)
set "MSBUILD_PATH="
set "VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE_PATH%" (
    echo Searching for MSBuild using vswhere...
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE_PATH%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD_PATH=%%i"
        goto :found_msbuild
    )
)

:found_msbuild
REM If vswhere didn't find it, try common locations
if "!MSBUILD_PATH!"=="" (
    echo Searching common Visual Studio locations...
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    )
)

REM Check if MSBuild was found
if "!MSBUILD_PATH!"=="" (
    echo.
    echo ERROR: Could not find MSBuild.exe
    echo.
    echo Please either:
    echo   1. Install Visual Studio 2019 or 2022 with C++ workload
    echo   2. Or run this from Visual Studio Developer Command Prompt
    echo   3. Or manually set MSBUILD_PATH environment variable
    echo.
    pause
    exit /b 1
)

echo Found MSBuild: !MSBUILD_PATH!

REM Change to solution directory
cd /d "%~dp0"

REM Build Release configuration for Vice City
echo.
echo Building Autosave mod for Vice City (Release configuration)...
"!MSBUILD_PATH!" Autosave.sln /t:Clean,Build /p:Configuration=Release /p:Platform=VC /v:minimal /nologo

if %errorlevel% equ 0 (
    echo.
    echo Build succeeded!
) else (
    echo.
    echo Build failed!
    pause
    exit /b 1
)

endlocal
