@echo off
echo Compiling shaders...

:: Path to Vulkan SDK's glslc need to change it to your version, I will update this soon
set GLSLC=C:/VulkanSDK/1.4.313.2/Bin/glslc.exe

:: Change to the shaders directory
cd /d "%~dp0"

:: Compile each shader to the output directory
%GLSLC% pbr.vert -o pbr_vert.spv
%GLSLC% pbr.frag -o pbr_frag.spv

echo Done.
pause