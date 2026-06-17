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

# ---- COM 后端（design §3.1/§5.2）：默认在 Windows 上启用 ----
if(WIN32)
    option(WITH_CAPEOPEN_COM "Build the COM backend (xRtoCapeOpen)" ON)
else()
    set(WITH_CAPEOPEN_COM OFF)
endif()
if(WITH_CAPEOPEN_COM)
    target_sources(capeopen_core PRIVATE
        backend/com/CapeVariantMarshal.cpp
        backend/com/CapeMINLPModelCom.cpp
        backend/com/CapeRegisterComBackend.cpp
    )
    target_compile_definitions(capeopen_core PUBLIC CAPEOPEN_WITH_COM)
    target_link_libraries(capeopen_core PUBLIC ole32 oleaut32 uuid)
endif()

# ---- CORBA/TAO 后端（design §3.3/§5.2）：默认 OFF，需 vcpkg ace[tao] ----
if(WIN32)
    option(WITH_CAPEOPEN_CORBA "Build the CORBA/TAO backend (xRtoCapeOpen)" OFF)
else()
    set(WITH_CAPEOPEN_CORBA OFF)
endif()
if(WITH_CAPEOPEN_CORBA)
    # 指向包含 ACE/TAO 的 vcpkg 安装三元组目录，例如：
    #   .../vcpkg/installed/x64-windows-static-md
    set(TAO_TRIPLET_DIR "" CACHE PATH "vcpkg installed triplet dir with ACE/TAO (ace[tao])")
    if(NOT TAO_TRIPLET_DIR OR NOT EXISTS "${TAO_TRIPLET_DIR}/include/tao")
        message(FATAL_ERROR "WITH_CAPEOPEN_CORBA needs -DTAO_TRIPLET_DIR=<vcpkg .../installed/x64-windows-static-md>")
    endif()
    set(TAO_INCLUDE_DIR "${TAO_TRIPLET_DIR}/include")
    set(TAO_LIB_DIR "${TAO_TRIPLET_DIR}/lib")
    set(TAO_IDL_EXE "${TAO_TRIPLET_DIR}/tools/ace/tao_idl.exe")

    set(IDL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../SqpSolver.idl")
    set(IDL_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/idlgen")
    file(MAKE_DIRECTORY ${IDL_GEN_DIR})
    # tao_idl 需要显式指定预处理器(cl.exe)；用 CMAKE_CXX_COMPILER。
    add_custom_command(
        OUTPUT ${IDL_GEN_DIR}/SqpSolverC.cpp ${IDL_GEN_DIR}/SqpSolverS.cpp
               ${IDL_GEN_DIR}/SqpSolverC.h  ${IDL_GEN_DIR}/SqpSolverS.h
        COMMAND "${TAO_IDL_EXE}" -Yp,${CMAKE_CXX_COMPILER} -I "${TAO_INCLUDE_DIR}" -o "${IDL_GEN_DIR}" "${IDL_FILE}"
        DEPENDS "${IDL_FILE}"
        COMMENT "tao_idl: generating SqpSolver stubs"
        VERBATIM)

    add_library(capeopen_corba STATIC
        ${IDL_GEN_DIR}/SqpSolverC.cpp
        ${IDL_GEN_DIR}/SqpSolverS.cpp
        backend/corba/CapeMINLPModelCorba.cpp
        backend/corba/CapeRegisterCorbaBackend.cpp
    )
    target_include_directories(capeopen_corba PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${XOPT_INCLUDE_DIR} ${IDL_GEN_DIR} ${TAO_INCLUDE_DIR})
    target_compile_definitions(capeopen_corba PUBLIC
        WIN32 ACE_AS_STATIC_LIBS=1 TAO_AS_STATIC_LIBS=1 CAPEOPEN_WITH_CORBA)
    target_link_directories(capeopen_corba PUBLIC ${TAO_LIB_DIR})
    target_link_libraries(capeopen_corba PUBLIC capeopen_core
        TAO_PortableServers TAO_AnyTypeCodes TAOs ACEs
        ws2_32 mswsock advapi32 user32 iphlpapi)
endif()

# ---- 单元测试（仅依赖 GTest + capeopen_core，零外部传输依赖）----
# 注意(MSVC/vcpkg)：测试 TU 的 CRT 运行库必须与所链接的 gtest.lib 一致，
# 否则 _ITERATOR_DEBUG_LEVEL 不匹配会破坏 std::string 布局，导致 gtest 套件名
# 读到垃圾内存（表现为“mixing TEST_F and TEST”假错）。最简稳妥做法：
# 用 Release 构建（与 vcpkg 默认 release gtest 匹配），或确保 Debug 链接 debug gtest。
option(CAPEOPEN_CORE_BUILD_TESTS "Build capeopen_core unit tests" ON)
if(CAPEOPEN_CORE_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    set(CAPEOPEN_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen_problem.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen_model.cpp
    )
    if(WITH_CAPEOPEN_COM)
        # 参考 COM 组件 RefCapeMINLP 只编进测试，不进生产库
        list(APPEND CAPEOPEN_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen_com.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/backend/com/RefCapeMINLP.cpp
        )
    endif()

    add_executable(test_capeopen_problem ${CAPEOPEN_TEST_SOURCES})
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

    # CORBA 后端测试：独立 exe（自带 main），链接 capeopen_corba + TAO。
    if(WITH_CAPEOPEN_CORBA)
        add_executable(test_capeopen_corba
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen_corba.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/backend/corba/RefCapeMINLPServant.cpp
        )
        target_link_libraries(test_capeopen_corba PRIVATE capeopen_corba GTest::gtest)
        add_test(NAME CapeOpenCorbaTest COMMAND test_capeopen_corba)
    endif()
endif()
