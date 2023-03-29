@echo off

set pwd=%~dp0
set project_dir=%~dp0..\
set python=python3

pushd %project_dir%
call %python% %pwd%PythonUtils\Setup.py
popd