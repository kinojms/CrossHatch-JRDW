@echo off
REM Setup script for vcpkg and required dependencies
REM This script will clone vcpkg, bootstrap it, and install OpenCV

echo ============================================================
echo AnitoScan - vcpkg Setup Script
echo ============================================================
echo.

REM Check if vcpkg already exists
if exist "vcpkg\scripts\buildsystems\vcpkg.cmake" (
    echo vcpkg already exists. Skipping clone...
    goto :bootstrap
)

echo Step 1: Cloning vcpkg...
git clone https://github.com/Microsoft/vcpkg.git
if errorlevel 1 (
    echo ERROR: Failed to clone vcpkg. Make sure git is installed and accessible.
    pause
    exit /b 1
)

:bootstrap
echo.
echo Step 2: Bootstrapping vcpkg...
cd vcpkg
call bootstrap-vcpkg.bat
if errorlevel 1 (
    echo ERROR: Failed to bootstrap vcpkg.
    cd ..
    pause
    exit /b 1
)

echo.
echo Step 3: Installing OpenCV (this may take 10-30 minutes)...
call vcpkg install opencv4:x64-windows
if errorlevel 1 (
    echo ERROR: Failed to install OpenCV.
    cd ..
    pause
    exit /b 1
)

cd ..
echo.
echo ============================================================
echo Setup complete! You can now run CMake configuration.
echo ============================================================
pause
