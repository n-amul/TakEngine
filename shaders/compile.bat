@echo off
echo Compiling shaders...

:: Path to Vulkan SDK's glslc
set GLSLC=C:/VulkanSDK/1.4.313.2/Bin/glslc.exe

:: Path to where testbed.exe is built (adjust Debug/Release as needed)
set OUTPUT_DIR=..\build\testbed\Debug

:: Compile each shader to the output directory
%GLSLC% triangle.vert -o %OUTPUT_DIR%\triangle_vert.spv
%GLSLC% triangle.frag -o %OUTPUT_DIR%\triangle_frag.spv

echo Done.
pause