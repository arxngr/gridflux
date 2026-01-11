@echo off
setlocal enabledelayedexpansion

:: Change to project root directory (parent of scripts/)
cd /d "%~dp0\.."

echo ================================================
echo GridFlux Windows Installer Builder
echo ================================================
echo.

:: Check for required tools
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake.
    exit /b 1
)

:: Detect available build system
set "BUILD_GENERATOR="
set "BUILD_TOOL="

:: Check for MinGW
where mingw32-make >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "BUILD_GENERATOR=MinGW Makefiles"
    set "BUILD_TOOL=MinGW"
    echo Found MinGW
    goto :build_found
)

where make >nul 2>nul
if %ERRORLEVEL% equ 0 (
    where gcc >nul 2>nul
    if %ERRORLEVEL% equ 0 (
        set "BUILD_GENERATOR=Unix Makefiles"
        set "BUILD_TOOL=Make + GCC"
        echo Found Make and GCC
        goto :build_found
    )
)

:: Check for Visual Studio
where cl >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "BUILD_GENERATOR=NMake Makefiles"
    set "BUILD_TOOL=Visual Studio"
    echo Found Visual Studio
    goto :build_found
)

:: Check for Ninja
where ninja >nul 2>nul
if %ERRORLEVEL% equ 0 (
    where gcc >nul 2>nul
    if %ERRORLEVEL% equ 0 (
        set "BUILD_GENERATOR=Ninja"
        set "BUILD_TOOL=Ninja + GCC"
        echo Found Ninja and GCC
        goto :build_found
    )
)

:: Not found
echo ERROR: No build system found!
echo.
echo Please install one of:
echo   - MinGW-w64: https://www.mingw-w64.org/
echo   - MSYS2: https://www.msys2.org/ (recommended)
echo   - Visual Studio: https://visualstudio.microsoft.com/
echo.
echo For MSYS2 (recommended):
echo   1. Download from https://www.msys2.org/
echo   2. Install and run MSYS2 MINGW64
echo   3. Run: pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-gtk4 mingw-w64-x86_64-json-c
echo   4. Add to PATH: C:\msys64\mingw64\bin
exit /b 1

:build_found
echo Using build tool: %BUILD_TOOL%

:: Find NSIS - check PATH first
set "NSIS_EXE="
where makensis >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "NSIS_EXE=makensis"
    echo Found NSIS in PATH
    goto :nsis_found
)

:: Check Program Files x86
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    set NSIS_EXE=C:\Program Files (x86^)\NSIS\makensis.exe
    echo Found NSIS at: C:\Program Files (x86^)\NSIS\
    goto :nsis_found
)

:: Check Program Files
if exist "C:\Program Files\NSIS\makensis.exe" (
    set NSIS_EXE=C:\Program Files\NSIS\makensis.exe
    echo Found NSIS at: C:\Program Files\NSIS\
    goto :nsis_found
)

:: Not found
echo ERROR: NSIS not found. Please install NSIS from https://nsis.sourceforge.io/
echo Checked locations:
echo   - PATH
echo   - C:\Program Files (x86^)\NSIS\
echo   - C:\Program Files\NSIS\
exit /b 1

:nsis_found

:: Clean previous build
echo Cleaning previous build...
if exist build rmdir /s /q build
if exist CMakeCache.txt del CMakeCache.txt

:: Convert PNG to ICO if not exists
if not exist icons\gridflux.ico (
    echo Converting PNG to ICO...
    if exist "C:\Program Files\ImageMagick-7*\magick.exe" (
        for /f "delims=" %%i in ('dir /b "C:\Program Files\ImageMagick-7*\magick.exe" 2^>nul') do set MAGICK=%%i
        "!MAGICK!" convert icons\gridflux-48.png -define icon:auto-resize=16,32,48,256 icons\gridflux.ico
    ) else (
        echo WARNING: ImageMagick not found. Using PNG as fallback.
        echo Please manually convert icons\gridflux-48.png to icons\gridflux.ico
    )
)

:: Build project
echo Building GridFlux...
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release .
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed
    exit /b 1
)

cmake --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b 1
)

:: Check if executables were built
if not exist build\gridflux.exe (
    echo ERROR: gridflux.exe not found
    exit /b 1
)

if not exist build\gridflux-gui.exe (
    echo ERROR: gridflux-gui.exe not found
    exit /b 1
)

if not exist build\gridflux-cli.exe (
    echo ERROR: gridflux-cli.exe not found
    exit /b 1
)

echo.
echo Build successful!
echo.

:: Collect DLLs
echo Collecting required DLLs...
mkdir build\bin 2>nul

:: Copy GTK4 and dependencies (adjust paths for your MSYS2/MinGW installation)
set MINGW_PATH=C:\msys64\mingw64\bin
if exist "%MINGW_PATH%" (
    echo Copying DLLs from %MINGW_PATH%...
    
    :: Core GTK4 DLLs
    copy "%MINGW_PATH%\libgtk-4-1.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libglib-2.0-0.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libgobject-2.0-0.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libgio-2.0-0.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libgdk_pixbuf-2.0-0.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libcairo-2.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libpango-1.0-0.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libpangocairo-1.0-0.dll" build\bin\ >nul 2>nul
    
    :: JSON-C
    copy "%MINGW_PATH%\libjson-c-5.dll" build\bin\ >nul 2>nul
    
    :: Other dependencies
    copy "%MINGW_PATH%\libintl-8.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libiconv-2.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libpcre2-8-0.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\zlib1.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libffi-8.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libpng16-16.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libwinpthread-1.dll" build\bin\ >nul 2>nul
    copy "%MINGW_PATH%\libgcc_s_seh-1.dll" build\bin\ >nul 2>nul
    
    echo DLLs copied successfully
) else (
    echo WARNING: MinGW path not found at %MINGW_PATH%
    echo Please manually copy required DLLs to build\bin\
)

:: Sign executables (optional, prevents virus warnings)
echo.
echo ================================================
echo Code Signing (Optional - Prevents False Positives)
echo ================================================
echo.
echo To prevent Microsoft Defender warnings, you should sign your executables.
echo.
echo Options:
echo 1. Get a code signing certificate from a trusted CA (DigiCert, Sectigo, etc.)
echo 2. Use signtool.exe from Windows SDK:
echo    signtool sign /f your-certificate.pfx /p password /t http://timestamp.digicert.com build\gridflux.exe
echo    signtool sign /f your-certificate.pfx /p password /t http://timestamp.digicert.com build\gridflux-gui.exe
echo    signtool sign /f your-certificate.pfx /p password /t http://timestamp.digicert.com build\gridflux-cli.exe
echo.
echo For now, continuing without signing...
echo.

:: Create installer
echo Creating NSIS installer...
"%NSIS_EXE%" gridflux-installer.nsi
if %ERRORLEVEL% neq 0 (
    echo ERROR: NSIS installer creation failed
    exit /b 1
)

echo.
echo ================================================
echo Build Complete!
echo ================================================
echo.
echo Installer created: GridFlux-Installer-2.0.0.exe
echo.
echo To prevent virus false positives:
echo 1. Sign the installer with a code signing certificate
echo 2. Submit the installer to Microsoft for analysis:
echo    https://www.microsoft.com/en-us/wdsi/filesubmission
echo.

endlocal