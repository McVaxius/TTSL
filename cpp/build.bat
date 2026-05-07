@echo off
setlocal EnableExtensions

cd /d "%~dp0" || exit /b 1

if not "%~2"=="" goto :usage

set "REQUEST=%~1"
if "%REQUEST%"=="" set "REQUEST=all"

set "DO_CLEAN=0"
if /I "%REQUEST%"=="clean" (
    set "DO_CLEAN=1"
    set "REQUEST=all"
)

set "BUILD_DEBUG=0"
set "BUILD_RELEASE=0"

if /I "%REQUEST%"=="all" (
    set "BUILD_DEBUG=1"
    set "BUILD_RELEASE=1"
) else if /I "%REQUEST%"=="Debug" (
    set "BUILD_DEBUG=1"
) else if /I "%REQUEST%"=="Release" (
    set "BUILD_RELEASE=1"
) else (
    goto :usage
)

set "MISSING_CMAKE=1"

for /f "delims=" %%I in ('where cmake 2^>nul') do (
    set "CMAKE_EXE=%%I"
    set "MISSING_CMAKE=0"
    goto :cmake_done
)

for %%I in (
    "C:\Program Files\CMake\bin\cmake.exe"
    "C:\Program Files (x86)\CMake\bin\cmake.exe"
) do (
    if exist "%%~I" (
        set "CMAKE_EXE=%%~I"
        set "MISSING_CMAKE=0"
        goto :cmake_done
    )
)

for %%E in (BuildTools Community Professional Enterprise) do (
    for %%R in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\%%E"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E"
    ) do (
        if exist "%%~R\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "CMAKE_EXE=%%~R\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            set "MISSING_CMAKE=0"
            goto :cmake_done
        )
    )
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -version "[17.0,18.0)" -products * -property installationPath 2^>nul`) do (
        if exist "%%I\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "CMAKE_EXE=%%I\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            set "MISSING_CMAKE=0"
            goto :cmake_done
        )
    )
)

:cmake_done
set "MISSING_VSCPP=1"

for %%E in (BuildTools Community Professional Enterprise) do (
    for %%R in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\%%E"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E"
    ) do (
        if exist "%%~R\VC\Auxiliary\Build\vcvars64.bat" if exist "%%~R\MSBuild\Current\Bin\MSBuild.exe" (
            set "VS_CPP_ROOT=%%~R"
            set "MISSING_VSCPP=0"
            goto :vs_cpp_done
        )
    )
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Workload.VCTools -property installationPath 2^>nul`) do (
        if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" if exist "%%I\MSBuild\Current\Bin\MSBuild.exe" (
            set "VS_CPP_ROOT=%%I"
            set "MISSING_VSCPP=0"
            goto :vs_cpp_done
        )
    )
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -version "[17.0,18.0)" -products * -property installationPath 2^>nul`) do (
        if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" if exist "%%I\MSBuild\Current\Bin\MSBuild.exe" (
            set "VS_CPP_ROOT=%%I"
            set "MISSING_VSCPP=0"
            goto :vs_cpp_done
        )
    )
)

where cl >nul 2>nul
if not errorlevel 1 (
    where msbuild >nul 2>nul
    if not errorlevel 1 (
        set "VS_CPP_ROOT=PATH"
        set "MISSING_VSCPP=0"
        goto :vs_cpp_done
    )
)

:vs_cpp_done
if "%MISSING_CMAKE%"=="1" if "%MISSING_VSCPP%"=="1" goto :tools_missing
if "%MISSING_CMAKE%"=="1" goto :cmake_missing
if "%MISSING_VSCPP%"=="1" goto :vscpp_missing

echo Using CMake: "%CMAKE_EXE%"
echo Using Visual Studio C++ tools: "%VS_CPP_ROOT%"

if "%DO_CLEAN%"=="1" (
    if exist "build" (
        echo Removing build directory...
        rmdir /s /q "build"
        if errorlevel 1 exit /b 1
    )
)

set "STALE_CMAKE_CACHE=0"
if exist "build\CMakeCache.txt" call :check_cmake_cache
if errorlevel 1 exit /b 1
if "%STALE_CMAKE_CACHE%"=="1" (
    echo CMake cache was generated for a different checkout path. Removing build directory...
    rmdir /s /q "build"
    if errorlevel 1 exit /b 1
)

echo Configuring...
"%CMAKE_EXE%" -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1

if "%BUILD_DEBUG%"=="1" (
    echo Building Debug...
    "%CMAKE_EXE%" --build build --config Debug --parallel
    if errorlevel 1 exit /b 1
)

if "%BUILD_RELEASE%"=="1" (
    echo Building Release...
    "%CMAKE_EXE%" --build build --config Release --parallel
    if errorlevel 1 exit /b 1
)

echo Build complete.
exit /b 0

:check_cmake_cache
set "CACHE_HOME="
set "CACHE_BUILD="
set "CURRENT_SOURCE_DIR=%CD:\=/%"
set "CURRENT_BUILD_DIR=%CD:\=/%/build"
for /f "tokens=1,* delims==" %%A in ('findstr /B /C:"CMAKE_HOME_DIRECTORY:INTERNAL=" /C:"CMAKE_CACHEFILE_DIR:INTERNAL=" "build\CMakeCache.txt" 2^>nul') do (
    if /I "%%A"=="CMAKE_HOME_DIRECTORY:INTERNAL" set "CACHE_HOME=%%B"
    if /I "%%A"=="CMAKE_CACHEFILE_DIR:INTERNAL" set "CACHE_BUILD=%%B"
)
set "CACHE_HOME=%CACHE_HOME:\=/%"
set "CACHE_BUILD=%CACHE_BUILD:\=/%"
if defined CACHE_HOME if /I not "%CACHE_HOME%"=="%CURRENT_SOURCE_DIR%" set "STALE_CMAKE_CACHE=1"
if defined CACHE_BUILD if /I not "%CACHE_BUILD%"=="%CURRENT_BUILD_DIR%" set "STALE_CMAKE_CACHE=1"
exit /b 0

:tools_missing
echo.
echo Build dependencies not found.
echo CMake and Visual Studio 2022 C++ Build Tools are missing.
echo.
echo Open PowerShell as Administrator and run:
echo   winget install --id Kitware.CMake --exact --source winget
echo   winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
echo Close and reopen your terminal, then run:
echo   where cmake
echo   cmd /c build.bat
exit /b 1

:cmake_missing
echo.
echo CMake not found.
echo.
echo Open PowerShell as Administrator and run:
echo   winget install --id Kitware.CMake --exact --source winget
echo.
echo Close and reopen your terminal, then run:
echo   where cmake
echo   cmd /c build.bat
echo.
echo Searched PATH, common CMake install paths, Visual Studio 2022 bundled CMake paths, and Visual Studio Installer vswhere results.
exit /b 1

:vscpp_missing
echo.
echo Visual Studio 2022 C++ Build Tools not found.
echo.
echo CMake was found: "%CMAKE_EXE%"
echo.
echo Open PowerShell as Administrator and run:
echo   winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
echo Close and reopen your terminal, then run:
echo   cmd /c build.bat
exit /b 1

:usage
echo Usage: build.bat [all^|Debug^|Release^|clean]
echo.
echo   build.bat          Configure, build Debug, build Release
echo   build.bat all      Configure, build Debug, build Release
echo   build.bat Debug    Configure, build Debug
echo   build.bat Release  Configure, build Release
echo   build.bat clean    Delete build, configure, build Debug and Release
exit /b 1
