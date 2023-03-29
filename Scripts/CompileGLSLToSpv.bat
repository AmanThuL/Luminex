@echo off

@REM Check if glslc is in the path
where glslc > nul

set pwd=%~dp0
set project_dir=%pwd%..\

pushd %project_dir%

set shader_in_dir=%CD%\Source\Shaders\
set shader_out_dir=%CD%\Source\App\Shaders\

@REM Create the shader output directory if it doesn't exist
if not exist %shader_out_dir% mkdir %shader_out_dir%

glslc "%shader_in_dir%main.vert" -o  "%shader_out_dir%vert.spv"
glslc "%shader_in_dir%main.frag" -o  "%shader_out_dir%frag.spv"

popd

pushd %shader_out_dir%

@REM List all files in the shader output directory
echo All compiled shaders in %shader_out_dir%:
dir /b

popd
