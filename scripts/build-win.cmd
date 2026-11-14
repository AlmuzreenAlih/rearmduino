@echo off
rem Configure + build on Windows with VS 2022's MSVC, CMake and Ninja.
rem Usage: scripts\build-win.cmd [Release|Debug]   (default Release)
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release
set QTDIR=C:\Qt\5.15.2\msvc2019_64
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem Put this Qt first on PATH so windeployqt doesn't resolve DLLs from another Qt (e.g. Anaconda).
set PATH=%QTDIR%\bin;%PATH%
set BUILDDIR=%~dp0..\build-win-%CONFIG%
cmake -S "%~dp0.." -B "%BUILDDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH=%QTDIR% || exit /b 1
cmake --build "%BUILDDIR%" || exit /b 1
cmake --build "%BUILDDIR%" --target deploy || exit /b 1
echo Built: %BUILDDIR%\REARMduino.exe
