@echo off
setlocal enabledelayedexpansion

:: Change to project root directory
cd /d "%~dp0\.."

echo ================================================
echo GridFlux Windows MSI Builder
echo ================================================
echo.

:: Check for required tools
where cmake >nul 2>nul || (
    echo ERROR: CMake not found.
    exit /b 1
)

where wix >nul 2>nul || (
    echo ERROR: WiX Toolset not found.
    echo Install with: winget install WiXToolset.WiXToolset
    exit /b 1
)

echo Found WiX Toolset


echo Cleaning previous build...
if exist build rmdir /s /q build
if exist CMakeCache.txt del CMakeCache.txt

echo Building GridFlux...
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release .
if %ERRORLEVEL% neq 0 exit /b 1

cmake --build build --config Release
if %ERRORLEVEL% neq 0 exit /b 1

for %%f in (gridflux.exe gridflux-gui.exe gridflux-cli.exe) do (
    if not exist build\%%f (
        echo ERROR: build\%%f missing
        exit /b 1
    )
)

echo Build successful!
echo.


echo Collecting GTK DLLs...
mkdir build\bin 2>nul

set MINGW_PATH=C:\msys64\mingw64\bin
if not exist "%MINGW_PATH%" (
    echo ERROR: MinGW runtime not found
    exit /b 1
)

copy "%MINGW_PATH%\*.dll" build\bin\ >nul 2>nul

echo DLL collection done.
echo.

echo Signing MSI...

if exist "cert.pfx" (
    signtool sign ^
      /fd SHA256 ^
      /f cert.pfx ^
      /t http://timestamp.digicert.com ^
      %MSI_NAME%
) else (
    echo WARNING: cert.pfx not found, skipping MSI signing
)

echo Building MSI installer
echo.

set MSI_NAME=GridFlux-0.1.0.msi

wix build ^
  installer\gridflux.wxs ^
  -o %MSI_NAME%

if %ERRORLEVEL% neq 0 (
    echo ERROR: MSI build failed
    exit /b 1
)

echo.
echo Build Complete!
echo.
echo MSI created: %MSI_NAME%
echo.
echo Silent install:
echo   msiexec /i %MSI_NAME% /qn
echo.
echo To avoid SmartScreen warnings:
echo - Sign the MSI using signtool.exe
echo.

endlocal
