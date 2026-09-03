# ***************************************************************
#  COExamples 顶层 CMake：三个目标，只依赖仓库根的 include 目录。
#  用法（见 build.bat）：
#    cmake -S . -B build -G "Visual Studio 18 2026" -A x64
#    cmake --build build --config Release
#  无外部依赖、无 vcpkg：xOpt 的 4 个头文件与 Eigen 都在仓库根 include 下。
#  产物统一输出到 build/bin/<Config>，DLL 与 exe 同目录（相对名加载）。
# ***************************************************************
cmake_minimum_required(VERSION 3.16)
project(COExamples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # 源文件为 UTF-8 BOM；/utf-8 保证中文注释与输出正确
    add_compile_options(/utf-8 /W3)
endif()

# 仓库根 include：xOpt 平台公共头（xOptInterface.h / xOptModel.h / xOptProblem.h /
# xOptSolver.h）与 Eigen（header-only，求解器用）都在这里。
# examples 目录为 libsrc/xRtoCapeOpen/examples，仓库根 include 即 ../../../include。
# 刻意不在本目录留一份副本：副本会与平台头文件静默漂移，而"漂移"在 C ABI 上的
# 表现是结构体尾部字段错位，编译期无征兆。core/CMakeLists.txt 与
# xOptMINLPco/CMakeLists.txt 用的是同一条路径。
set(XOPT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")

# Eigen 不走 find_package/vcpkg：仓库根 include 下就有一份（include/Eigen），
# 与 xOpt 头共用同一个 include 目录。少一个外部依赖，示例就少一条"装了才能编"
# 的前置条件——它存在的意义是被人直接编起来看。

# 三个目标统一输出到 build/bin/<Config>（多配置生成器逐配置指定）
set(XOPT_OUT_BASE ${CMAKE_BINARY_DIR}/bin)
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} ${XOPT_OUT_BASE}/${cfg})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} ${XOPT_OUT_BASE}/${cfg})
endforeach()
# 单配置生成器兜底
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${XOPT_OUT_BASE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${XOPT_OUT_BASE})

# ---- 测试模型：黑箱 Splitter DLL（只依赖本地头文件，不链接平台库）----
add_library(test_splitter_model SHARED
    testModel/SplitterModel.cpp
)
target_include_directories(test_splitter_model PRIVATE ${XOPT_INCLUDE_DIR})

# ---- 测试求解器：罚函数梯度下降 DLL（Eigen，来自同一个 include 目录）----
add_library(test_penalty_solver SHARED
    testSolver/PenaltyGradientSolver.cpp
    testSolver/SolverEntry.cpp
)
target_include_directories(test_penalty_solver PRIVATE ${XOPT_INCLUDE_DIR})

# ---- 演示程序：运行时经 LoadLibrary 加载两个 DLL，无需链接其导入库 ----
add_executable(xopt_demo
    demo/main.cpp
    demo/ProblemFromModelT.cpp
)
target_include_directories(xopt_demo PRIVATE ${XOPT_INCLUDE_DIR})
add_dependencies(xopt_demo test_splitter_model test_penalty_solver)

# 描述 JSON 随产物复制（真实部署形态 = UnitModel 目录下 DLL + 描述 JSON）
add_custom_command(TARGET xopt_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/testModel/Splitter_Model.json
        $<TARGET_FILE_DIR:xopt_demo>
    COMMENT "Copying Splitter_Model.json to output directory"
)
