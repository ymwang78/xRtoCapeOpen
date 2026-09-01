# ***************************************************************
#  COExamples 顶层 CMake：三个目标，仅依赖本地 include/xOpt 头文件。
#  用法（见 build.bat）：
#    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=F:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
#          -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
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

# Eigen（header-only）：罚函数梯度下降求解器的底层数学
find_package(Eigen3 CONFIG REQUIRED)

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
target_include_directories(test_splitter_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---- 测试求解器：罚函数梯度下降 DLL（Eigen）----
add_library(test_penalty_solver SHARED
    testSolver/PenaltyGradientSolver.cpp
    testSolver/SolverEntry.cpp
)
target_include_directories(test_penalty_solver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_penalty_solver PRIVATE Eigen3::Eigen)

# ---- 演示程序：运行时经 LoadLibrary 加载两个 DLL，无需链接其导入库 ----
add_executable(xopt_demo
    demo/main.cpp
    demo/ProblemFromModelT.cpp
)
target_include_directories(xopt_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_dependencies(xopt_demo test_splitter_model test_penalty_solver)

# 描述 JSON 随产物复制（真实部署形态 = UnitModel 目录下 DLL + 描述 JSON）
add_custom_command(TARGET xopt_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/testModel/Splitter_Model.json
        $<TARGET_FILE_DIR:xopt_demo>
    COMMENT "Copying Splitter_Model.json to output directory"
)
