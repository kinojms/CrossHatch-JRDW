@echo off
REM Compile vertex color shaders
REM This script compiles the BGFX shaders to binary format

setlocal enabledelayedexpansion

REM Check if shaderc exists, if not build it
if not exist "build\x64\Release\bin\shaderc.exe" (
    echo Building shaderc tool...
    cd build
    cmake --build . --config Release --target shaderc
    cd ..
)

if not exist "build\x64\Release\bin\shaderc.exe" (
    echo ERROR: Could not find or build shaderc.exe
    exit /b 1
)

set SHADERC="build\x64\Release\bin\shaderc.exe"
set SHADER_DIR=shaders
set VARYING_DEF=baseshaders\varying.def.sc

echo Compiling vertex color shaders...

REM Compile vertex shader (Direct3D 11)
%SHADERC% -f %SHADER_DIR%\v_vertex_colors.sc -o %SHADER_DIR%\v_vertex_colors.bin -p vs_5_0 --platform windows --type vertex --varyingdef %VARYING_DEF%

if !errorlevel! neq 0 (
    echo ERROR: Failed to compile v_vertex_colors.sc
    exit /b 1
)

REM Compile fragment shader (Direct3D 11)
%SHADERC% -f %SHADER_DIR%\f_vertex_colors.sc -o %SHADER_DIR%\f_vertex_colors.bin -p ps_5_0 --platform windows --type fragment --varyingdef %VARYING_DEF%

if !errorlevel! neq 0 (
    echo ERROR: Failed to compile f_vertex_colors.sc
    exit /b 1
)

echo.
echo Shader compilation complete!
echo Generated files:
echo   - %SHADER_DIR%\v_vertex_colors.bin
echo   - %SHADER_DIR%\f_vertex_colors.bin
