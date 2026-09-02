@echo off
rem ***************************************************************
rem  COExamples one-shot build: configure + build(Release) + run demo.
rem
rem  Requires nothing but Visual Studio 2026 (x64 MSVC).
rem  No vcpkg: both xOpt/*.h and Eigen come from the repo-root
rem  include directory (..\..\..\include), which CMakeLists.txt
rem  points at as XOPT_INCLUDE_DIR.
rem
rem  Uses the CMake shipped with Visual Studio when present -- a
rem  standalone cmake is often too old to know the installed VS
rem  generator.
rem ***************************************************************
setlocal

set VSROOT=C:\Program Files\Microsoft Visual Studio\18
set CMAKE_EXE=cmake
for %%E in (Enterprise Professional Community BuildTools) do (
    if exist "%VSROOT%\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set CMAKE_EXE="%VSROOT%\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        goto :found
    )
)
:found

%CMAKE_EXE% -S . -B build -G "Visual Studio 18 2026" -A x64
if errorlevel 1 exit /b 1

%CMAKE_EXE% --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo ==== running xopt_demo ====
build\bin\Release\xopt_demo.exe
exit /b %errorlevel%
