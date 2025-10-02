@echo off
echo Compiling shaders...

:: Path to Vulkan SDK's glslc need to change it to your version, update to not use absolute path
set GLSLC=C:/VulkanSDK/1.4.313.2/Bin/glslc.exe

:: Change to the shaders directory
cd /d "%~dp0"

:: Compile each shader to the output directory
%GLSLC% model.vert -o "model.vert.spv"
%GLSLC% model.frag -o "model.frag.spv"

echo Done.
pause