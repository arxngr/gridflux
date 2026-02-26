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
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel .
if %ERRORLEVEL% neq 0 exit /b 1

cmake --build build --config MinSizeRel
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

:: Gather dependencies dynamically using ldd
echo Resolving and copying exact DLL dependencies...
set "LDD_CMD=C:\msys64\usr\bin\ldd.exe"
if not exist "%LDD_CMD%" (
    echo ERROR: ldd not found in MSYS2.
    exit /b 1
)

:: Put all gridflux binaries into a single ldd invocation, output to a temp file
"%LDD_CMD%" build\gridflux.exe build\gridflux-gui.exe build\gridflux-cli.exe > build\ldd_out.txt

:: Extract /mingw64/bin/... paths, convert them to Windows paths, and copy
for /f "tokens=3" %%A in ('findstr /i "mingw64" build\ldd_out.txt') do (
    set "DLL_PATH=%%A"
    :: Strip leading /mingw64/ and convert / to \ 
    set "DLL_PATH=!DLL_PATH:/mingw64/=>!"
    set "DLL_PATH=!DLL_PATH:>=!"
    set "DLL_PATH=!DLL_PATH:/=\!"
    
    :: Final path
    set "FULL_PATH=C:\msys64\mingw64\!DLL_PATH!"
    if exist "!FULL_PATH!" (
        copy /y "!FULL_PATH!" build\bin\ >nul
    )
)

echo Stripping collected DLLs and executables to reduce size...
for %%f in (build\bin\*.dll) do strip "%%f"
strip build\gridflux.exe build\gridflux-gui.exe build\gridflux-cli.exe 2>nul

echo Compressing DLLs and executables with UPX...
set "UPX_CMD=C:\msys64\mingw64\bin\upx.exe"
if exist "%UPX_CMD%" (
    for %%f in (build\bin\*.dll) do "%UPX_CMD%" -9 "%%f" >nul 2>nul
    "%UPX_CMD%" -9 build\gridflux.exe build\gridflux-gui.exe build\gridflux-cli.exe >nul 2>nul
    echo UPX compression done.
) else (
    echo WARNING: upx not found! Skipping extreme compression.
)

echo DLL collection, stripping, and compression done.
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

:: ADDED: -ext WixUIExtension -ext WixUtilExtension
candle.exe -ext WixUIExtension -ext WixUtilExtension gridflux.wxs build\wix_dlls.wxs -out build\
if %ERRORLEVEL% neq 0 (
    echo ERROR: candle.exe failed
    exit /b 1
)

:: ADDED: -ext WixUIExtension -ext WixUtilExtension
light.exe -ext WixUIExtension -ext WixUtilExtension build\*.wixobj -out %MSI_NAME%
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
