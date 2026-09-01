@echo off
rem ***************************************************************
rem  COExamples one-shot build: configure + build(Release) + run demo.
rem  Requires: Visual Studio 2026 (x64 MSVC), F:\vcpkg
rem            (eigen3:x64-windows-static-md installed).
rem  Uses the CMake shipped with Visual Studio when present (the
rem  standalone cmake may be too old for the installed VS).
rem ***************************************************************
setlocal

set VCPKG_ROOT=F:\vcpkg
set TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
set VSCMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if exist %VSCMAKE% (
    set CMAKE_EXE=%VSCMAKE%
) else (
    set CMAKE_EXE=cmake
)

%CMAKE_EXE% -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN% -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
if errorlevel 1 exit /b 1

%CMAKE_EXE% --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo ==== running xopt_demo ====
build\bin\Release\xopt_demo.exe
exit /b %errorlevel%
