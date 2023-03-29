@echo off

set pwd=%~dp0
set project_dir=%pwd%..\
set python=python3

pushd %project_dir%
call %python% %pwd%PythonUtils\RunClangFormat.py -i -r Source
popd