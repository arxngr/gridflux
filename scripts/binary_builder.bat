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

:: WiX Toolset path (default install)
set WIX_PATH=C:\Program Files (x86)\WiX Toolset v3.14\bin
set PATH=%WIX_PATH%;%PATH%

:: Check for WiX binaries
where candle.exe >nul 2>nul || (
    echo ERROR: WiX Toolset not found.
    echo Install with: winget install WiX.Toolset
    exit /b 1
)
where light.exe >nul 2>nul || (
    echo ERROR: WiX Toolset not found.
    echo Install with: winget install WiX.Toolset
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

echo Generating wix_dlls.wxs...
set WIX_DLL_FILE=build\wix_dlls.wxs

(
echo ^<?xml version="1.0" encoding="UTF-8"?^>
echo ^<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi"^>
echo ^<Fragment^>
echo   ^<DirectoryRef Id="BinDir"^>
) > %WIX_DLL_FILE%

:: Loop through all DLLs in build\bin
for %%f in (build\bin\*.dll) do (
    set "DLL_NAME=%%~nxf"
    set "DLL_ID=!DLL_NAME:.=_!"
    set "DLL_ID=!DLL_ID:-=_!"
    set "DLL_ID=!DLL_ID: =_!"
    set "DLL_ID=!DLL_ID:+=_!"
    set "DLL_ID=!DLL_ID:#=_!"
    set "DLL_ID=Dll_!DLL_ID!"

    for /f %%G in ('powershell -NoProfile -Command "[guid]::NewGuid().ToString()"') do set "COMP_GUID=%%G"

    :: FIXED: Added Win64="yes" here
    echo     ^<Component Id="!DLL_ID!" Guid="{!COMP_GUID!}" KeyPath="yes" Win64="yes"^> >> %WIX_DLL_FILE%
    echo       ^<File Source="%%f" /^> >> %WIX_DLL_FILE%
    echo     ^</Component^> >> %WIX_DLL_FILE%
)

(
echo   ^</DirectoryRef^>
echo   ^<ComponentGroup Id="GtkRuntime"^>
) >> %WIX_DLL_FILE%

:: Add ComponentRefs for all DLLs
for %%f in (build\bin\*.dll) do (
    set "DLL_NAME=%%~nxf"
    :: sanitize again
    set "DLL_ID=!DLL_NAME:.=_!"
    set "DLL_ID=!DLL_ID:-=_!"
    set "DLL_ID=!DLL_ID: =_!"
    set "DLL_ID=!DLL_ID:+=_!"
    set "DLL_ID=!DLL_ID:#=_!"
    set "DLL_ID=Dll_!DLL_ID!"

    echo     ^<ComponentRef Id="!DLL_ID!" /^> >> %WIX_DLL_FILE%
)

(
echo   ^</ComponentGroup^>
echo ^</Fragment^>
echo ^</Wix^>
) >> %WIX_DLL_FILE%

echo wix_dlls.wxs generated!


echo.

echo Building MSI installer
echo.

set MSI_NAME=GridFlux-0.1.0.msi

:: ADDED: -ext WixUIExtension
candle.exe -ext WixUIExtension gridflux.wxs build\wix_dlls.wxs -out build\
if %ERRORLEVEL% neq 0 (
    echo ERROR: candle.exe failed
    exit /b 1
)

:: ADDED: -ext WixUIExtension
light.exe -ext WixUIExtension build\*.wixobj -out %MSI_NAME%
if %ERRORLEVEL% neq 0 (
    echo ERROR: light.exe failed
    exit /b 1
)

echo.

:: Signing MSI if cert.pfx exists
if exist "cert.pfx" (
    echo Signing MSI...
    signtool sign ^
      /fd SHA256 ^
      /f cert.pfx ^
      /t http://timestamp.digicert.com ^
      %MSI_NAME%
) else (
    echo WARNING: cert.pfx not found, skipping MSI signing
)

echo.
echo Build Complete!
echo MSI created: %MSI_NAME%
echo.
echo Silent install:
echo   msiexec /i %MSI_NAME% /qn
echo.
echo To avoid SmartScreen warnings:
echo - Sign the MSI using signtool.exe
echo.

endlocal
