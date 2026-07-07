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

:: Check for WiX Toolset v4+ (wix.exe)
where wix.exe >nul 2>nul || (
    echo ERROR: WiX Toolset not found.
    echo Install with: dotnet tool install --global wix
    exit /b 1
)

echo Found WiX Toolset

echo Cleaning previous build...
:: Stop any running GridFlux binaries first. While they run, their .exe files in
:: build\ are locked, so cleaning build\ (or re-linking later) fails with
:: "Access is denied". Stopping your own processes needs no administrator rights.
echo Stopping any running GridFlux processes...
for %%P in (gridflux.exe gridflux-gui.exe gridflux-cli.exe gridflux-launcher.exe) do (
    taskkill /f /im %%P >nul 2>nul
)

if exist build rmdir /s /q build
if exist CMakeCache.txt del CMakeCache.txt

:: Detect MSYS2 environment from PATH
set "MINGW_PATH="
for /f "delims=" %%I in ('where gcc 2^>nul') do (
    if not defined MINGW_PATH (
        set "GCC_PATH=%%~dpI"
        :: Remove trailing backslash
        set "MINGW_PATH=!GCC_PATH:~0,-1!"
    )
)

if not defined MINGW_PATH (
    echo ERROR: gcc not found in PATH. Please ensure your MSYS2 MinGW environment is activated.
    exit /b 1
)

set "MSYS2_ENV="
echo !MINGW_PATH! | findstr /i "ucrt64" >nul && set "MSYS2_ENV=ucrt64"
echo !MINGW_PATH! | findstr /i "mingw64" >nul && set "MSYS2_ENV=mingw64"
echo !MINGW_PATH! | findstr /i "clang64" >nul && set "MSYS2_ENV=clang64"
echo !MINGW_PATH! | findstr /i "mingw32" >nul && set "MSYS2_ENV=mingw32"

if not defined MSYS2_ENV (
    echo WARNING: Could not determine MSYS2 subsystem from !MINGW_PATH!
    set "MSYS2_ENV=mingw64"
)

echo Detected MSYS2 environment: !MSYS2_ENV!
echo MinGW Path: !MINGW_PATH!

:: Get MSYS2 root directory (two levels up from bin, e.g. C:\msys64\mingw64\bin -> C:\msys64)
for %%I in ("!MINGW_PATH!\..") do set "MSYS2_PREFIX=%%~fI"
for %%I in ("!MSYS2_PREFIX!\..") do set "MSYS2_ROOT=%%~fI"

set "PKG_CONFIG_EXE=!MINGW_PATH!\pkgconf.exe"
if not exist "!PKG_CONFIG_EXE!" (
    set "PKG_CONFIG_EXE=!MINGW_PATH!\pkg-config.exe"
)

echo Building GridFlux...
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_C_COMPILER="!MINGW_PATH!\gcc.exe" -DPKG_CONFIG_EXECUTABLE="!PKG_CONFIG_EXE!" .
if !ERRORLEVEL! neq 0 exit /b 1

cmake --build build --config MinSizeRel -j
if !ERRORLEVEL! neq 0 exit /b 1

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

:: Gather dependencies dynamically using ldd
echo Resolving and copying exact DLL dependencies...
set "LDD_CMD=!MSYS2_ROOT!\usr\bin\ldd.exe"
if not exist "%LDD_CMD%" (
    echo ERROR: ldd not found in MSYS2.
    exit /b 1
)

:: Put all gridflux binaries into a single ldd invocation, output to a temp file
"%LDD_CMD%" build\gridflux.exe build\gridflux-gui.exe build\gridflux-cli.exe > build\ldd_out.txt

:: Extract DLL filenames from ldd output for the detected MSYS2 environment
:: Note: We extract just the DLL filename and copy from MINGW_PATH to avoid
:: batch delayed-expansion issues with path string substitution.
set "ENV_PATTERN=/%MSYS2_ENV%/"
for /f "tokens=1,3" %%A in ('findstr /i "%MSYS2_ENV%" build\ldd_out.txt') do (
    set "DLL_NAME=%%A"
    :: Only copy .dll files (skip header lines)
    echo !DLL_NAME! | findstr /i "\.dll" >nul 2>nul
    if !ERRORLEVEL! equ 0 (
        if exist "!MINGW_PATH!\!DLL_NAME!" (
            copy /y "!MINGW_PATH!\!DLL_NAME!" build\bin\ >nul
        )
    )
)

echo Adding dynamically loaded DLLs that ldd may miss...
:: Complete list of all required runtime DLLs including json-c, GTK4, and transitive deps
set "RUNTIME_DLLS=libjson-c-5.dll libgtk-4-1.dll libgdk_pixbuf-2.0-0.dll libglib-2.0-0.dll libgobject-2.0-0.dll libgio-2.0-0.dll libgmodule-2.0-0.dll libpangocairo-1.0-0.dll libpango-1.0-0.dll libcairo-gobject-2.dll libcairo-2.dll libepoxy-0.dll libharfbuzz-0.dll libintl-8.dll libsqlite3-0.dll libtiff-6.dll libjpeg-8.dll libpng16-16.dll zlib1.dll libffi-8.dll libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll libfribidi-0.dll libthai-0.dll libiconv-2.dll libpcre2-8-0.dll libdatrie-1.dll libjbig-0.dll libdeflate.dll liblzma-5.dll libzstd.dll libwebp-7.dll libLerc.dll libfreetype-6.dll libbrotlidec.dll libbz2-1.dll libbrotlicommon.dll libgraphite2.dll libsharpyuv-0.dll libgraphene-1.0-0.dll libfontconfig-1.dll libcairo-script-interpreter-2.dll libpangoft2-1.0-0.dll libpangowin32-1.0-0.dll libpixman-1-0.dll libexpat-1.dll liblzo2-2.dll libharfbuzz-subset-0.dll liborc-0.4-0.dll"

for %%D in (%RUNTIME_DLLS%) do (
    if not exist "build\bin\%%D" (
        if exist "!MINGW_PATH!\%%D" (
            copy /y "!MINGW_PATH!\%%D" build\bin\ >nul
        )
    )
)

:: Verify all required MSYS2 DLLs are present
echo Verifying all required DLLs are bundled...
set "MISSING_COUNT=0"
for /f "tokens=1,3" %%A in ('findstr /i "%MSYS2_ENV%" build\ldd_out.txt') do (
    set "DLL_NAME=%%A"
    echo !DLL_NAME! | findstr /i "\.dll" >nul 2>nul
    if !ERRORLEVEL! equ 0 (
        if not exist "build\bin\!DLL_NAME!" (
            echo   MISSING: !DLL_NAME!
            set /a MISSING_COUNT+=1
        )
    )
)
if !MISSING_COUNT! gtr 0 (
    echo ERROR: !MISSING_COUNT! required DLL^(s^) are missing from build\bin!
    echo The installer would be broken. Aborting.
    exit /b 1
)

echo Adding GTK/GLib compiled schemas...
mkdir "build\share\glib-2.0\schemas" 2>nul
if exist "!MSYS2_PREFIX!\share\glib-2.0\schemas\gschemas.compiled" (
    copy /y "!MSYS2_PREFIX!\share\glib-2.0\schemas\gschemas.compiled" "build\share\glib-2.0\schemas\" >nul
)

:: Fallback: compile schemas if precompiled file was not found
if not exist "build\share\glib-2.0\schemas\gschemas.compiled" (
    echo Precompiled gschemas not found, attempting to compile...
    set "GLIB_COMPILE=!MSYS2_PREFIX!\bin\glib-compile-schemas.exe"
    if exist "!GLIB_COMPILE!" (
        "!GLIB_COMPILE!" "!MSYS2_PREFIX!\share\glib-2.0\schemas" --targetdir="build\share\glib-2.0\schemas"
    ) else (
        echo WARNING: glib-compile-schemas not found, creating empty placeholder.
        type nul > "build\share\glib-2.0\schemas\gschemas.compiled"
    )
)

echo Stripping collected DLLs and executables to reduce size...
for %%f in (build\bin\*.dll) do strip "%%f"
strip build\gridflux.exe build\gridflux-gui.exe build\gridflux-cli.exe 2>nul

echo Compressing DLLs and executables with UPX...
set "UPX_CMD=!MSYS2_PREFIX!\bin\upx.exe"
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
echo ^<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs"^>
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

    :: FIXED: Added Bitness="always64" for WiX v4+
    echo     ^<Component Id="!DLL_ID!" Guid="{!COMP_GUID!}" KeyPath="yes" Bitness="always64"^> >> %WIX_DLL_FILE%
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

:: Derive release version from the latest git tag (e.g. v1.3.0 -> 1.3.0),
:: falling back to 1.0.0 when no tag is reachable.
set "GF_VERSION=1.0.0"
for /f "delims=" %%i in ('git describe --tags --abbrev^=0 2^>nul') do set "GF_TAG=%%i"
if defined GF_TAG (
    if "!GF_TAG:~0,1!"=="v" (set "GF_VERSION=!GF_TAG:~1!") else (set "GF_VERSION=!GF_TAG!")
)
set "GF_VERSION4=!GF_VERSION!.0"
echo Release version: !GF_VERSION!

set "MSI_NAME=GridFlux-!GF_VERSION!.msi"

:: Build MSI with WiX v4+ command (version reaches gridflux.wxs via $(var.Version))
wix build -arch x64 -acceptEula wix7 -d Version=!GF_VERSION! -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext gridflux.wxs build\wix_dlls.wxs -out "%MSI_NAME%"
if !ERRORLEVEL! neq 0 (
    echo ERROR: wix build failed
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
echo     Version="!GF_VERSION4!"
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
"!MAKEAPPX!" pack /d "!MSIX_DIR!" /p "GridFlux-!GF_VERSION!.msix" /o /l

if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to build MSIX package.
) else (
    echo.
    echo ================================================
    echo MSIX build Complete!
    echo Output: GridFlux-!GF_VERSION!.msix
    echo Submit this file to the Microsoft Store via Partner Center.
    echo ================================================
)

:SkipMSIX
endlocal
