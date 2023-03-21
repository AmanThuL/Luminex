@echo off
pushd %~dp0\..\
call Premake\Bin\Windows\premake5.exe vs2022
popd
PAUSE