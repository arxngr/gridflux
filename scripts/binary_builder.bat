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

echo Adding dynamically loaded GTK runtime DLLs missed by ldd...
:: GTK4 dynamically loads these at runtime for icons, parsing, and rendering
set "GTK_RUNTIME_DLLS=libgdk_pixbuf-2.0-0.dll libglib-2.0-0.dll libgobject-2.0-0.dll libgio-2.0-0.dll libgmodule-2.0-0.dll libpangocairo-1.0-0.dll libpango-1.0-0.dll libcairo-gobject-2.dll libcairo-2.dll libepoxy-0.dll libharfbuzz-0.dll libintl-8.dll libsqlite3-0.dll libtiff-6.dll libjpeg-8.dll libpng16-16.dll zlib1.dll libffi-8.dll libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll"

for %%D in (%GTK_RUNTIME_DLLS%) do (
    if exist "C:\msys64\mingw64\bin\%%D" (
        copy /y "C:\msys64\mingw64\bin\%%D" build\bin\ >nul
    )
)

echo Adding GTK/GLib compiled schemas...
mkdir "build\share\glib-2.0\schemas" 2>nul
if exist "C:\msys64\mingw64\share\glib-2.0\schemas\gschemas.compiled" (
    copy /y "C:\msys64\mingw64\share\glib-2.0\schemas\gschemas.compiled" "build\share\glib-2.0\schemas\" >nul
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
echo   ^<DirectoryRef Id="INSTALLDIR"^>
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

set MSI_NAME=GridFlux-1.0.0.msi

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

echo ================================================
echo Building MSIX Package (for Microsoft Store)
echo ================================================

:: Find makeappx.exe
set "MAKEAPPX="
for /d %%d in ("C:\Program Files (x86)\Windows Kits\10\bin\10.0.*") do (
    if exist "%%d\x64\makeappx.exe" set "MAKEAPPX=%%d\x64\makeappx.exe"
)

if not defined MAKEAPPX (
    echo WARNING: makeappx.exe not found. Skipping MSIX build.
    goto :SkipMSIX
)

echo Found makeappx: !MAKEAPPX!

set "MSIX_DIR=build\msix"
if exist "!MSIX_DIR!" rmdir /s /q "!MSIX_DIR!"
mkdir "!MSIX_DIR!"
mkdir "!MSIX_DIR!\Assets"

echo Copying binaries and DLLs...
copy /y build\*.exe "!MSIX_DIR!\" >nul
copy /y build\bin\*.dll "!MSIX_DIR!\" >nul

echo Copying assets...
copy /y icons\gridflux-48.png "!MSIX_DIR!\Assets\StoreLogo.png" >nul
copy /y icons\gridflux-48.png "!MSIX_DIR!\Assets\Square44x44Logo.png" >nul
copy /y icons\gf_logo.png "!MSIX_DIR!\Assets\Square150x150Logo.png" >nul

echo Generating AppxManifest.xml...
set "MANIFEST=!MSIX_DIR!\AppxManifest.xml"
(
echo ^<?xml version="1.0" encoding="utf-8"?^>
echo ^<Package
echo   xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
echo   xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
echo   xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
echo   xmlns:desktop="http://schemas.microsoft.com/appx/manifest/desktop/windows10"
echo   IgnorableNamespaces="uap rescap desktop"^>
echo.
echo   ^<Identity
echo     Name="ArdiNugraha.GridFluxVirtualWorkspace"
echo     Publisher="CN=C0CED2D5-3090-4872-B6DE-67F97E7E24CD"
echo     Version="1.0.0.0"
echo     ProcessorArchitecture="x64" /^>
echo.
echo   ^<Properties^>
echo     ^<DisplayName^>GridFlux Virtual Workspace^</DisplayName^>
echo     ^<PublisherDisplayName^>Ardi Nugraha^</PublisherDisplayName^>
echo     ^<Logo^>Assets\StoreLogo.png^</Logo^>
echo   ^</Properties^>
echo.
echo   ^<Dependencies^>
echo     ^<TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.19041.0" /^>
echo   ^</Dependencies^>
echo.
echo   ^<Resources^>
echo     ^<Resource Language="EN-US" /^>
echo   ^</Resources^>
echo.
echo   ^<Applications^>
echo     ^<Application
echo       Id="GridFlux"
echo       Executable="gridflux-launcher.exe"
echo       EntryPoint="Windows.FullTrustApplication"^>
echo       ^<uap:VisualElements
echo         DisplayName="GridFlux Virtual Workspace"
echo         Description="A fast and configurable tiling window manager for Windows"
echo         BackgroundColor="transparent"
echo         Square150x150Logo="Assets\Square150x150Logo.png"
echo         Square44x44Logo="Assets\Square44x44Logo.png"^>
echo       ^</uap:VisualElements^>
echo       ^<Extensions^>
echo         ^<desktop:Extension Category="windows.startupTask" Executable="gridflux-launcher.exe" EntryPoint="Windows.FullTrustApplication"^>
echo           ^<desktop:StartupTask TaskId="GridFluxStartup" Enabled="true" DisplayName="GridFlux" /^>
echo         ^</desktop:Extension^>
echo       ^</Extensions^>
echo     ^</Application^>
echo   ^</Applications^>
echo.
echo   ^<Capabilities^>
echo     ^<rescap:Capability Name="runFullTrust" /^>
echo   ^</Capabilities^>
echo ^</Package^>
) > "!MANIFEST!"

echo Building MSIX...
"!MAKEAPPX!" pack /d "!MSIX_DIR!" /p GridFlux-1.0.0.msix /o /l

if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to build MSIX package.
) else (
    echo.
    echo ================================================
    echo MSIX build Complete!
    echo Output: GridFlux-1.0.0.msix
    echo Submit this file to the Microsoft Store via Partner Center.
    echo ================================================
)

:SkipMSIX
endlocal
