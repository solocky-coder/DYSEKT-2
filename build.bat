@echo off
setlocal

echo ============================================
echo  DYSEKT-SF - Clean Configure + Build
echo ============================================
echo.

:: --- Locate vcpkg toolchain ---
set TOOLCHAIN=
if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
) else if exist "%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%/vcpkg/scripts/buildsystems/vcpkg.cmake
) else (
    echo [WARNING] vcpkg not found at C:\vcpkg or %USERPROFILE%\vcpkg
    echo [WARNING] Continuing without vcpkg toolchain -- FluidSynth may not resolve.
    echo.
)

:: --- Step 1: Wipe old build folder ---
echo [1/3] Removing old build folder...
if exist build (
    rmdir /s /q build
    echo       Done.
) else (
    echo       No existing build folder found, skipping.
)
echo.

:: --- Step 2: CMake Configure ---
echo [2/3] Configuring with CMake...
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN%
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed. See output above.
    pause
    exit /b 1
)
echo.

:: --- Step 3: Build ---
echo [3/3] Building Release...
cmake --build build --config Release
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. See output above.
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Build complete!
echo  Check: C:\Program Files\Common Files\VST3\
echo ============================================
pause
