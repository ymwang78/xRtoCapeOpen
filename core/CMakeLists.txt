# ***************************************************************
#  capeopen_core  —— 后端无关核心 + 内置 Mock 后端 + DLL C 入口
#  详见 docs/capeopen_problem_design.md §3.4、§7.1
# ***************************************************************
cmake_minimum_required(VERSION 3.10)
project(capeopen_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 仓库公共 include（xOpt C 头：xOptModel.h / xOptInterface.h / xOptProblem.h）
# core 目录: libsrc/xRtoCapeOpen/core -> 仓库根 include 为 ../../../include
set(XOPT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")

set(CAPEOPEN_CORE_SOURCES
    CapeMINLPModelMock.cpp
    CapeMINLPProblemCore.cpp
    CapeBackendFactory.cpp
    xOptModelCapeOpen.cpp
)

add_library(capeopen_core STATIC ${CAPEOPEN_CORE_SOURCES})

target_include_directories(capeopen_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${XOPT_INCLUDE_DIR}
)

# ---- 单元测试（仅依赖 GTest + capeopen_core，零外部传输依赖）----
# 注意(MSVC/vcpkg)：测试 TU 的 CRT 运行库必须与所链接的 gtest.lib 一致，
# 否则 _ITERATOR_DEBUG_LEVEL 不匹配会破坏 std::string 布局，导致 gtest 套件名
# 读到垃圾内存（表现为“mixing TEST_F and TEST”假错）。最简稳妥做法：
# 用 Release 构建（与 vcpkg 默认 release gtest 匹配），或确保 Debug 链接 debug gtest。
option(CAPEOPEN_CORE_BUILD_TESTS "Build capeopen_core unit tests" ON)
if(CAPEOPEN_CORE_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(test_capeopen_problem
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen_problem.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen_model.cpp
    )
    # 测试文件自带 main()（CLAUDE.md 的 #ifndef USE_GTEST_MAIN 模板），
    # 故只链接 GTest::gtest，不链接 gtest_main，避免 main 重复。
    target_link_libraries(test_capeopen_problem PRIVATE
        capeopen_core
        GTest::gtest
    )
    target_include_directories(test_capeopen_problem PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${XOPT_INCLUDE_DIR}
    )
    add_test(NAME CapeOpenProblemTest COMMAND test_capeopen_problem)
endif()
