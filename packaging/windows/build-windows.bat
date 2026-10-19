@echo off
rem Builds every plugin (VST3 + standalone .exe) on Windows.
rem Needs Visual Studio 2022 (Desktop C++ workload), CMake and Git.
cd /d "%~dp0\..\.."
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :fail
cmake --build build --config Release --parallel || goto :fail
echo.
echo Done. Standalone apps are in build\*_artefacts\Release\Standalone
echo VST3 plugins are in build\*_artefacts\Release\VST3
pause
exit /b 0
:fail
echo Build failed.
pause
exit /b 1
