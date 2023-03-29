@echo off

:: set the path to your visual studio vcvars script, it is different for every version of Visual Studio.
set VS2022TOOLS="C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

:: make sure we found them
if not exist %VS2022TOOLS% (
    echo "Could not find Visual Studio 2022 tools at %VS2022TOOLS%"
    exit /b 1
)

:: call that script, which essentially sets up the VS Developer Command Prompt
call %VS2022TOOLS%

:: Set some variables for the source directory and the build directory
set pwd=%~dp0
set SrcDir=%pwd%..\

echo "Src Dir is %SrcDir%"
set BuildDir=%SrcDir%\Build

:: Make the build directory if it doesn't exist
if not exist "%BuildDir%" mkdir "%BuildDir%"

:: Make sure you configure with CMake from the build directory
pushd %BuildDir%

:: run the compiler with your arguments to build the project
@REM todo Need to update this to coordinate with Premake
call cl.exe /EHsc /Zi /Fe: D:\Luminex\Build\main.exe D:\Luminex\Source\main.cpp
@REM cl.exe /EHsc /Zi /Fe: /I./Source/LMRHI/Vulkan/ ./Source/LMRHI/Vulkan/*.cpp
popd

@REM CMake Example below
@REM :: Call CMake to configure the build (generates the build scripts)
@REM cmake %SrcDir%^
@REM  -D CMAKE_C_COMPILER=cl.exe^
@REM  -D CMAKE_CXX_COMPILER=cl.exe^

@REM :: Call CMake again to build the project
@REM cmake --build %BuildDir%

@REM exit