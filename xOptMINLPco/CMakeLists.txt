# ***************************************************************
#  xOptMINLPco —— 把 xOptProblem DLL 发布为 CAPE-OPEN MINLP 组件
#  (capeopen_core 的反向)。详见 docs/xOptMINLPco_design.md
#    N1：provider 核心 XOptMINLPAdapter + 单测（零外部传输依赖）
#    N2：COM 前端 CoMINLP + COM 服务端 DLL(xOptMINLPco.dll) + 回环测试
# ***************************************************************
cmake_minimum_required(VERSION 3.10)
project(xOptMINLPco CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CAPEOPEN_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../core")
set(XOPT_INCLUDE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")

# ---- N1：provider 核心（传输无关）----
add_library(xoptminlpco STATIC
    XOptMINLPAdapter.cpp
)
target_include_directories(xoptminlpco PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CAPEOPEN_CORE_DIR}   # CapeMINLPModel.h
    ${XOPT_INCLUDE_DIR}    # xOpt/xOptProblem.h
)

# ---- N2：COM 前端（默认在 Windows 上启用）----
if(WIN32)
    option(WITH_XOPTMINLPCO_COM "Build the COM frontend + xOptMINLPco.dll" ON)
else()
    set(WITH_XOPTMINLPCO_COM OFF)
endif()
if(WITH_XOPTMINLPCO_COM)
    # 复用 capeopen_core（含 COM 后端、ICapeMINLP 接口头、VARIANT marshaling、官方 IID）。
    set(CAPEOPEN_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CAPEOPEN_CORE_DIR} ${CMAKE_BINARY_DIR}/capeopen_core_build)

    add_library(xoptminlpco_com STATIC CoMINLP.cpp)
    target_link_libraries(xoptminlpco_com PUBLIC xoptminlpco capeopen_core)

    # COM inproc server DLL：产物 xOptMINLPco.dll
    add_library(xOptMINLPco SHARED
        xOptMINLPcoServer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/xOptMINLPco.def
    )
    target_link_libraries(xOptMINLPco PRIVATE xoptminlpco_com)
    # DLL 名为 xOptMINLPco.dll；导入库改名避免与静态库 xoptminlpco.lib 大小写冲突(LNK1149)。
    set_target_properties(xOptMINLPco PROPERTIES
        OUTPUT_NAME "xOptMINLPco" PREFIX ""
        ARCHIVE_OUTPUT_NAME "xOptMINLPco_import")
endif()

# ---- 单元测试 ----
option(XOPTMINLPCO_BUILD_TESTS "Build xOptMINLPco unit tests" ON)
if(XOPTMINLPCO_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(test_xoptminlpco_adapter
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_adapter.cpp
    )
    target_link_libraries(test_xoptminlpco_adapter PRIVATE xoptminlpco GTest::gtest)
    add_test(NAME XOptMINLPcoAdapterTest COMMAND test_xoptminlpco_adapter)

    if(WITH_XOPTMINLPCO_COM)
        # 回环：mock → adapter → CoMINLP → capeopen_core CapeMINLPModelCom
        add_executable(test_xoptminlpco_com
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_com.cpp
        )
        target_link_libraries(test_xoptminlpco_com PRIVATE xoptminlpco_com GTest::gtest)
        add_test(NAME XOptMINLPcoComTest COMMAND test_xoptminlpco_com)
    endif()
endif()
