@echo off
echo Compiling shaders...

if not defined VULKAN_SDK (
    echo ERROR: VULKAN_SDK environment variable not set!
    echo Please install Vulkan SDK from https://vulkan.lunarg.com/
    pause
    exit /b 1
)

:: Use glslc from the detected Vulkan SDK
set GLSLC=%VULKAN_SDK%\Bin\glslc.exe

:: Verify glslc exists
if not exist "%GLSLC%" (
    echo ERROR: glslc.exe not found at: %GLSLC%
    echo Please check your Vulkan SDK installation.
    pause
    exit /b 1
)

echo Using Vulkan SDK: %VULKAN_SDK%
echo Using glslc: %GLSLC%
echo.

:: Change to the shaders directory
cd /d "%~dp0"

:: Compile each shader to the output directory
echo Compiling pbr shaders...
"%GLSLC%" pbr.vert -o "pbr.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile pbr.vert
    pause
    exit /b 1
)
"%GLSLC%" pbrIbl.vert -o "pbrIbl.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile pbrIbl.vert
    pause
    exit /b 1
)

"%GLSLC%" pbr.frag -o "pbr.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile pbr.frag
    pause
    exit /b 1
)

echo Compiling skybox shaders...
"%GLSLC%" skybox.vert -o "skybox.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile skybox.vert
    pause
    exit /b 1
)

"%GLSLC%" skybox.frag -o "skybox.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile skybox.frag
    pause
    exit /b 1
)

echo Compiling triangle shaders...
"%GLSLC%" triangle.vert -o "triangle.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile triangle.vert
    pause
    exit /b 1
)

"%GLSLC%" triangle.frag -o "triangle.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile triangle.frag
    pause
    exit /b 1
)
"%GLSLC%" genbrdflut.vert -o "genbrdflut.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile genbrdflut.vert
    pause
    exit /b 1
)
"%GLSLC%" genbrdflut.frag -o "genbrdflut.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile genbrdflut.frag
    pause
    exit /b 1
)
"%GLSLC%" filtercube.vert -o "filtercube.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile filtercube.vert
    pause
    exit /b 1
)
"%GLSLC%" irradiancecube.frag -o "irradiancecube.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile irradiancecube.frag
    pause
    exit /b 1
)
"%GLSLC%" prefilterenvmap.frag -o "prefilterenvmap.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile prefilterenvmap.frag
    pause
    exit /b 1
)
"%GLSLC%" material_pbr.frag -o "material_pbr.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile "material_pbr.frag
    pause
    exit /b 1
)
"%GLSLC%" material_unlit.frag -o "material_unlit.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile "material_unlit.frag
    pause
    exit /b 1
)

echo Compiling UI shaders...
"%GLSLC%" ui.vert -o "ui.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile ui.vert
    pause
    exit /b 1
)

"%GLSLC%" ui.frag -o "ui.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile ui.frag
    pause
    exit /b 1
)

"%GLSLC%" ./deferredShaders/deferred_geometry.vert -o "deferred_geometry.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile deferred_geometry.vert
    pause
    exit /b 1
)
"%GLSLC%" ./deferredShaders/deferred_geometry.frag -o "deferred_geometry.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile deferred_geometry.frag
    pause
    exit /b 1
)
"%GLSLC%" ./deferredShaders/ssao.vert -o "ssao.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile ssao.vert
    pause
    exit /b 1
)
"%GLSLC%" ./deferredShaders/ssao.frag -o "ssao.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile ssao.frag
    pause
    exit /b 1
)
"%GLSLC%" ./deferredShaders/ssao_blur.vert -o "ssao_blur.vert.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile ssao_blur.vert
    pause
    exit /b 1
)
"%GLSLC%" ./deferredShaders/ssao_blur.frag -o "ssao_blur.frag.spv"
if errorlevel 1 (
    echo ERROR: Failed to compile ssao_blur.frag
    pause
    exit /b 1
)


echo.
echo ===================================
echo All shaders compiled successfully!
echo ===================================
pause