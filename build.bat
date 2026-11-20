@echo off
setlocal

echo ========================================================
echo  Building JamTabaVST3Wrapper (Windows x64 Release)
echo ========================================================

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found. Please install CMake 3.20+ and add it to PATH.
    pause
    exit /b 1
)

where git >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Git not found. Please install Git (needed to fetch Steinberg VST3 SDK).
    pause
    exit /b 1
)

cmake -S . -B build -A x64
if errorlevel 1 (
    echo [ERROR] CMake configure failed. Make sure Visual Studio 2022 (C++ Desktop workload) is installed.
    pause
    exit /b 1
)

cmake --build build --config Release --target JamTabaVST3Wrapper -j
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo ========================================================
echo  Build succeeded!
echo  Output bundle: build\VST3\Release\JamTabaVST3Wrapper.vst3
echo.
echo  Next steps:
echo  1. Copy JamTabaVST3Wrapper.vst3 to:
echo       C:\Program Files\Common Files\VST3\
echo  2. Copy your 64-bit JamtabaVST2.dll to:
echo       C:\Program Files\Common Files\VST3\JamTabaVST3Wrapper.vst3\Contents\x86_64-win\JamtabaVST2.dll
echo     (Or leave it in C:\Program Files\VSTPlugins\JamtabaVST2.dll)
echo ========================================================
pause
