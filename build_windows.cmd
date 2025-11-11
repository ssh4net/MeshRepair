@echo off
REM Build script for Windows using Visual Studio
REM Usage: build_windows.cmd [Debug|Release]

setlocal

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

echo Building MeshRepair for Windows (%BUILD_TYPE%)

REM Create build directory
if not exist build_win mkdir build_win
cd build_win

REM Configure CMake for Visual Studio
REM Adjust CMAKE_PREFIX_PATH to your dependency location (e.g., e:/DVS or e:/UBS)
cmake -G "Visual Studio 17 2022" ^
    -D CMAKE_BUILD_TYPE=Debug,Release ^
    -D CMAKE_MSVC_RUNTIME_LIBRARY="$<$<CONFIG:Debug>:MultiThreadedDebug>$<$<CONFIG:Release>:MultiThreaded>" ^
    -D BUILD_SHARED_LIBS=OFF ^
    -D CMAKE_INSTALL_PREFIX=INSTALL ^
    -D CMAKE_DEBUG_POSTFIX=d ^
    -D CMAKE_PREFIX_PATH=e:/UBS ^
    ..

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

REM Build
echo.
echo Building %BUILD_TYPE% configuration...
cmake --build . --config %BUILD_TYPE% -j 8

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

REM Install (optional)
REM cmake --install . --config %BUILD_TYPE%

echo.
echo Build completed successfully!
echo Executable location: build_win\%BUILD_TYPE%\mesh_hole_filler.exe
echo.

endlocal
