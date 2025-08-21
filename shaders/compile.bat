@echo off
echo Compiling shaders...

:: Path to Vulkan SDK's glslc
set GLSLC=C:/VulkanSDK/1.4.313.2/Bin/glslc.exe

:: Change to the shaders directory
cd /d "%~dp0"

:: Compile each shader to the output directory
%GLSLC% triangle.vert -o triangle_vert.spv
%GLSLC% triangle.frag -o triangle_frag.spv

echo Done.
pause