# TTSL Native C++ Server

This folder contains the native Windows C++ server for TTSL. It builds with CMake and Visual Studio 2022.

## Prerequisites

- CMake 3.22 or newer.
- Visual Studio 2022 Build Tools or Visual Studio 2022.
- The Visual Studio workload `Desktop development with C++`.

`build.bat` searches common CMake locations, including Visual Studio 2022 bundled CMake. A permanent `PATH` change is not required.

## Fix: CMake Not Found

This project uses CMake to configure the build and Visual Studio 2022 C++ Build Tools to compile it. If `build.bat` reports missing build dependencies, install them from an Administrator PowerShell:

```powershell
winget install --id Kitware.CMake --exact --source winget
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Close and reopen your terminal, then verify CMake and build again:

```bat
where cmake
cmd /c build.bat
```

Manual fallback downloads:

- [CMake downloads](https://cmake.org/download/)
- [Visual Studio Build Tools downloads](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022)

## Recommended Build

From this folder:

```bat
build.bat
```

Options:

```bat
build.bat Debug
build.bat Release
build.bat clean
```

`build.bat` runs from its own folder, so it also works from another current directory when called by path:

```bat
Z:\ttsl\cpp\build.bat
```

Build outputs:

```text
build\Debug\ttsl-native-server.exe
build\Release\ttsl-native-server.exe
```

## Advanced Manual CMake

If you need to run CMake directly, use Developer PowerShell for VS 2022 or any shell where `cmake` is already available:

```powershell
Set-Location Z:\ttsl\cpp

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
cmake --build build --config Release --parallel
```

If CMake or the Visual Studio C++ tools are unavailable, use the install commands in [Fix: CMake Not Found](#fix-cmake-not-found).

## Run

```powershell
.\build\Debug\ttsl-native-server.exe
```

or:

```powershell
.\build\Release\ttsl-native-server.exe
```

The server listens at:

```text
http://127.0.0.1:6942/
```

## Smoke Check

This starts the Debug build, requests server state, then stops only that process.

```powershell
Set-Location Z:\ttsl\cpp

$p = Start-Process -FilePath ".\build\Debug\ttsl-native-server.exe" -PassThru
try {
    Invoke-RestMethod -Uri "http://127.0.0.1:6942/api/state"
}
finally {
    if ($p -and -not $p.HasExited) {
        Stop-Process -Id $p.Id -Force
    }
}
```

## Clean Reconfigure

If CMake cache or generator settings get stale:

```bat
build.bat clean
```
