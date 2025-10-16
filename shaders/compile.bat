@echo off
echo Compiling shaders...

:: Path to Vulkan SDK's glslc need to change it to your version, update to not use absolute path
set GLSLC=C:/VulkanSDK/1.4.313.2/Bin/glslc.exe

:: Change to the shaders directory
cd /d "%~dp0"

:: Compile each shader to the output directory
%GLSLC% pbr.vert -o "pbr.vert.spv"
%GLSLC% pbr.frag -o "pbr.frag.spv"

%GLSLC% skybox.vert -o "skybox.vert.spv"
%GLSLC% skybox.frag -o "skybox.frag.spv"

%GLSLC% triangle.vert -o "triangle.vert.spv"
%GLSLC% triangle.frag -o "triangle.frag.spv"

%GLSLC% ui.vert -o "ui.vert.spv"
%GLSLC% ui.frag -o "ui.frag.spv"

echo Done.
pause