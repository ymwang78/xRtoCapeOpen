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

# ---- 前端开关（Windows）----
if(WIN32)
    option(WITH_XOPTMINLPCO_COM "Build the COM frontend + xOptMINLPco.dll" ON)
    option(WITH_XOPTMINLPCO_CORBA "Build the CORBA frontend (needs vcpkg ace[tao])" OFF)
else()
    set(WITH_XOPTMINLPCO_COM OFF)
    set(WITH_XOPTMINLPCO_CORBA OFF)
endif()

# ---- 复用 capeopen_core（COM 后端/接口头/marshaling/IID；CORBA 后端/IDL stub）----
if(WITH_XOPTMINLPCO_COM OR WITH_XOPTMINLPCO_CORBA)
    set(CAPEOPEN_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(WITH_XOPTMINLPCO_CORBA)
        set(WITH_CAPEOPEN_CORBA ON CACHE BOOL "" FORCE)  # 让 core 构建 capeopen_corba（需 TAO_TRIPLET_DIR）
    endif()
    add_subdirectory(${CAPEOPEN_CORE_DIR} ${CMAKE_BINARY_DIR}/capeopen_core_build)
endif()

# ---- N2：COM 前端 ----
if(WITH_XOPTMINLPCO_COM)
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

    # N3 测试夹具：导出 createProblem/destroyProblem 的 xOptProblem DLL（mock）
    add_library(mock_xoptproblem SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../tests/mock_xoptproblem_dll.cpp)
    target_include_directories(mock_xoptproblem PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests ${XOPT_INCLUDE_DIR})
    set_target_properties(mock_xoptproblem PROPERTIES OUTPUT_NAME "mock_xoptproblem" PREFIX "")
endif()

# ---- N4：CORBA 前端 ----
if(WITH_XOPTMINLPCO_CORBA)
    add_library(xoptminlpco_corba STATIC MINLPServant.cpp)
    # capeopen_corba PUBLIC 传递 TAO include/defines/libs + 生成的 SqpSolver* stub 头
    target_link_libraries(xoptminlpco_corba PUBLIC xoptminlpco capeopen_corba)
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

        # N3：注册 + CoCreateInstance 激活冒烟（用注册的 xOptMINLPco.dll 加载 mock DLL）
        add_executable(test_xoptminlpco_register
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_register.cpp
        )
        target_link_libraries(test_xoptminlpco_register PRIVATE capeopen_core GTest::gtest)
        target_include_directories(test_xoptminlpco_register PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_dependencies(test_xoptminlpco_register xOptMINLPco mock_xoptproblem)
        add_test(NAME XOptMINLPcoRegisterTest COMMAND test_xoptminlpco_register)
    endif()

    if(WITH_XOPTMINLPCO_CORBA)
        # 回环：mock → adapter → MINLPServant → capeopen_core CapeMINLPModelCorba（collocated）
        add_executable(test_xoptminlpco_corba
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_corba.cpp
        )
        target_link_libraries(test_xoptminlpco_corba PRIVATE xoptminlpco_corba GTest::gtest)
        add_test(NAME XOptMINLPcoCorbaTest COMMAND test_xoptminlpco_corba)
    endif()
endif()
