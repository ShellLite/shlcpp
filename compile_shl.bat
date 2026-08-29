@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
  set InstallDir=%%i
)

if exist "%InstallDir%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%InstallDir%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo Warning: Could not automatically find vcvars64.bat. Build may fail if run outside Developer Command Prompt.
)

if not exist build_cpp mkdir build_cpp
cd build_cpp

echo Configuring CMake...
cmake ..

echo Building Release Configuration...
cmake --build . --config Release

echo Copying binary...
copy /Y Release\shell_lite_exec.exe ..\shlcpp.exe
copy /Y Release\shell_lite_lib.dll ..\shell_lite_lib.dll

cd ..
echo Build complete!
