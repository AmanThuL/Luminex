@echo off

@REM Check if dxc.exe is in the path
where dxc.exe > nul

set pwd=%~dp0
set project_dir=%pwd%..\

pushd %project_dir%

set shader_in_dir=%CD%\Source\Shaders\
set shader_out_dir=%CD%\Source\App\Shaders\

@REM Create the shader output directory if it doesn't exist
if not exist %shader_out_dir% mkdir %shader_out_dir%

dxc.exe -T vs_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -Fo "%shader_out_dir%vert.spv" "%shader_in_dir%vert.hlsl"
dxc.exe -T ps_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -Fo "%shader_out_dir%frag.spv" "%shader_in_dir%frag.hlsl"

popd

pushd %shader_out_dir%

@REM List all files in the shader output directory
echo All compiled shaders in %shader_out_dir%:
dir /b

popd
