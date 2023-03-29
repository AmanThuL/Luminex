@echo off

set pwd=%~dp0
set project_dir=%pwd%..\


pushd %project_dir%

echo "Current directory is %cd%"

@echo on
call %cd%\Tools\Premake\Bin\Windows\premake5.exe vs2022
@echo off

@REM Start a yes/no prompt to open the solution
set /p open_solution=Open Visual Studio solution? (y/n)
if %open_solution%==y (
    start Luminex.sln
)

@REM START Luminex.sln

popd
