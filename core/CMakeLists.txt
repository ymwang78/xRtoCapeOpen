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
# 后端的源码/宏/依赖同时记进累加器：下面的消费端 DLL 直接用这些源码重编一份，
# 而不是链静态库——xOptModel_createModel 是 DLL 里唯一被外部引用的符号，
# 链静态库时链接器有充分理由把那个 .obj 整个丢掉，成品导不出任何东西。
set(CAPEOPEN_BACKEND_SOURCES "")
set(CAPEOPEN_BACKEND_DEFS "")
set(CAPEOPEN_BACKEND_LIBS "")
set(CAPEOPEN_BACKEND_INCLUDES "")

if(WITH_CAPEOPEN_COM)
    list(APPEND CAPEOPEN_BACKEND_SOURCES
        backend/com/CapeVariantMarshal.cpp
        backend/com/CapeMINLPModelCom.cpp
        backend/com/CapeRegisterComBackend.cpp
    )
    list(APPEND CAPEOPEN_BACKEND_DEFS CAPEOPEN_WITH_COM)
    list(APPEND CAPEOPEN_BACKEND_LIBS ole32 oleaut32 uuid)

    target_sources(capeopen_core PRIVATE ${CAPEOPEN_BACKEND_SOURCES})
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

    set(IDL_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/idlgen")
    file(MAKE_DIRECTORY ${IDL_GEN_DIR})

    # 官方模块路径的 IDL（重建，见 tools/gen_capeopen_minlp_idl.py 与
    # docs/xOptMINLPco_design.md §6）。MINLP 通路的消费端与生产端都用它。
    # 仓库根的 SqpSolver.idl 不再参与本通路（它属于旧 solver 侧代码）。
    # tao_idl 需要显式指定预处理器(cl.exe)；用 CMAKE_CXX_COMPILER。
    set(CO_IDL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../CAPEOPEN100_Minlp.idl")
    add_custom_command(
        OUTPUT ${IDL_GEN_DIR}/CAPEOPEN100_MinlpC.cpp ${IDL_GEN_DIR}/CAPEOPEN100_MinlpS.cpp
               ${IDL_GEN_DIR}/CAPEOPEN100_MinlpC.h  ${IDL_GEN_DIR}/CAPEOPEN100_MinlpS.h
        COMMAND "${TAO_IDL_EXE}" -Yp,${CMAKE_CXX_COMPILER} -I "${TAO_INCLUDE_DIR}" -o "${IDL_GEN_DIR}" "${CO_IDL_FILE}"
        DEPENDS "${CO_IDL_FILE}"
        COMMENT "tao_idl: generating CAPEOPEN100 (official module path) stubs"
        VERBATIM)

    # Unit Operation / Collection / Parameter 子集（重建，见
    # tools/gen_capeopen_unit_idl.py）与我们自己的扩展。两者都 #include 上面
    # 那份 Minlp IDL 复用 Common::Types/Error/Identification，所以 tao_idl 的
    # include 路径要带上仓库根。
    set(CO_UNIT_IDL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../CAPEOPEN100_Unit.idl")
    set(CO_EXT_IDL_FILE  "${CMAKE_CURRENT_SOURCE_DIR}/../XOPTCO_Ext.idl")
    add_custom_command(
        OUTPUT ${IDL_GEN_DIR}/CAPEOPEN100_UnitC.cpp ${IDL_GEN_DIR}/CAPEOPEN100_UnitS.cpp
               ${IDL_GEN_DIR}/CAPEOPEN100_UnitC.h  ${IDL_GEN_DIR}/CAPEOPEN100_UnitS.h
        COMMAND "${TAO_IDL_EXE}" -Yp,${CMAKE_CXX_COMPILER} -I "${TAO_INCLUDE_DIR}"
                -I "${CMAKE_CURRENT_SOURCE_DIR}/.." -o "${IDL_GEN_DIR}" "${CO_UNIT_IDL_FILE}"
        DEPENDS "${CO_UNIT_IDL_FILE}" "${CO_IDL_FILE}"
        COMMENT "tao_idl: generating CAPEOPEN100 Unit/Collection/Parameter stubs"
        VERBATIM)
    add_custom_command(
        OUTPUT ${IDL_GEN_DIR}/XOPTCO_ExtC.cpp ${IDL_GEN_DIR}/XOPTCO_ExtS.cpp
               ${IDL_GEN_DIR}/XOPTCO_ExtC.h  ${IDL_GEN_DIR}/XOPTCO_ExtS.h
        COMMAND "${TAO_IDL_EXE}" -Yp,${CMAKE_CXX_COMPILER} -I "${TAO_INCLUDE_DIR}"
                -I "${CMAKE_CURRENT_SOURCE_DIR}/.." -o "${IDL_GEN_DIR}" "${CO_EXT_IDL_FILE}"
        DEPENDS "${CO_EXT_IDL_FILE}" "${CO_UNIT_IDL_FILE}" "${CO_IDL_FILE}"
        COMMENT "tao_idl: generating XOPTCO extension stubs (ours, not CAPE-OPEN)"
        VERBATIM)

    # 一个静态库装全部三份 stub：它们本来就互相 #include，拆库只会制造
    # "谁先谁后"的链接顺序问题，而它们的生命周期完全一致。
    add_library(capeopen100_minlp STATIC
        ${IDL_GEN_DIR}/CAPEOPEN100_MinlpC.cpp
        ${IDL_GEN_DIR}/CAPEOPEN100_MinlpS.cpp
        ${IDL_GEN_DIR}/CAPEOPEN100_UnitC.cpp
        ${IDL_GEN_DIR}/CAPEOPEN100_UnitS.cpp
        ${IDL_GEN_DIR}/XOPTCO_ExtC.cpp
        ${IDL_GEN_DIR}/XOPTCO_ExtS.cpp
    )
    target_include_directories(capeopen100_minlp PUBLIC ${IDL_GEN_DIR} ${TAO_INCLUDE_DIR})
    target_compile_definitions(capeopen100_minlp PUBLIC
        WIN32 ACE_AS_STATIC_LIBS=1 TAO_AS_STATIC_LIBS=1)
    target_link_directories(capeopen100_minlp PUBLIC ${TAO_LIB_DIR})
    target_link_libraries(capeopen100_minlp PUBLIC
        TAO_PortableServers TAO_AnyTypeCodes TAOs ACEs
        ws2_32 mswsock advapi32 user32 iphlpapi)

    # MINLP 通路已迁到官方模块路径的 stub（capeopen100_minlp）。SqpSolver.idl
    # 仍为仓库根的旧 solver 侧代码保留，但不再参与本通路：用自造模块的 stub
    # 去 _narrow 一个真实 CO 组件的引用必然返回 nil。
    list(APPEND CAPEOPEN_BACKEND_SOURCES
        backend/corba/CapeMINLPModelCorba.cpp
        backend/corba/CapeRegisterCorbaBackend.cpp
        backend/corba/CapeUnitCorba.cpp
    )
    list(APPEND CAPEOPEN_BACKEND_DEFS
        WIN32 ACE_AS_STATIC_LIBS=1 TAO_AS_STATIC_LIBS=1 CAPEOPEN_WITH_CORBA)
    list(APPEND CAPEOPEN_BACKEND_LIBS capeopen100_minlp
        TAO_PortableServers TAO_AnyTypeCodes TAOs ACEs
        ws2_32 mswsock advapi32 user32 iphlpapi)
    list(APPEND CAPEOPEN_BACKEND_INCLUDES ${IDL_GEN_DIR} ${TAO_INCLUDE_DIR})

    add_library(capeopen_corba STATIC
        backend/corba/CapeMINLPModelCorba.cpp
        backend/corba/CapeRegisterCorbaBackend.cpp
        backend/corba/CapeUnitCorba.cpp
    )
    target_link_libraries(capeopen_corba PUBLIC capeopen100_minlp)
    target_include_directories(capeopen_corba PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${XOPT_INCLUDE_DIR} ${IDL_GEN_DIR} ${TAO_INCLUDE_DIR})
    target_compile_definitions(capeopen_corba PUBLIC
        WIN32 ACE_AS_STATIC_LIBS=1 TAO_AS_STATIC_LIBS=1 CAPEOPEN_WITH_CORBA)
    target_link_directories(capeopen_corba PUBLIC ${TAO_LIB_DIR})
    target_link_libraries(capeopen_corba PUBLIC capeopen_core
        TAO_PortableServers TAO_AnyTypeCodes TAOs ACEs
        ws2_32 mswsock advapi32 user32 iphlpapi)
endif()

# ***************************************************************
#  消费端 DLL —— xRto 侧真正加载的那一个（design §3 的架构图）
#  -------------------------------------------------------------
#  产物 xRtoCapeOpen.dll，只导出一个 C 符号 xOptModel_createModel。
#  在 xRto 的 UnitModel.json 里按 ModelType="BlackBox" 注册，
#  ModelPath 指向本 DLL；连接目标经环境变量 XRTO_CAPEOPEN_TARGET 给，
#  例如 "corba:corbaname::localhost:2809#xOpt/Splitter" 或 "corba:IOR:0100..."。
#
#  target 名不叫 xRtoCapeOpen：仓库根的 project() 就叫这个，而 VS 生成器按
#  target 名产工程文件，大小写不敏感的文件系统上同名会绕成 MSB4006 循环依赖
#  （xOptMINLPco 那边刚踩过）。OUTPUT_NAME 保证产物名不变。
# ***************************************************************
option(WITH_CAPEOPEN_MODEL_DLL "Build the consumer-side black-box model DLL (xRtoCapeOpen.dll)" ON)
if(WITH_CAPEOPEN_MODEL_DLL)
    add_library(xrtocapeopen_dll SHARED
        ${CAPEOPEN_CORE_SOURCES}
        ${CAPEOPEN_BACKEND_SOURCES}
    )
    set_target_properties(xrtocapeopen_dll PROPERTIES
        OUTPUT_NAME "xRtoCapeOpen" PREFIX ""
        ARCHIVE_OUTPUT_NAME "xRtoCapeOpen_import")
    target_include_directories(xrtocapeopen_dll PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${XOPT_INCLUDE_DIR}
        ${CAPEOPEN_BACKEND_INCLUDES}
    )
    if(CAPEOPEN_BACKEND_DEFS)
        target_compile_definitions(xrtocapeopen_dll PRIVATE ${CAPEOPEN_BACKEND_DEFS})
    endif()
    if(CAPEOPEN_BACKEND_LIBS)
        target_link_libraries(xrtocapeopen_dll PRIVATE ${CAPEOPEN_BACKEND_LIBS})
    endif()
    if(WITH_CAPEOPEN_CORBA)
        target_link_directories(xrtocapeopen_dll PRIVATE ${TAO_LIB_DIR})
    endif()
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

        # 重建 IDL 的 Repository ID 断言。RID 由 module 嵌套隐式推出，改错了
        # 编译照过、测试照绿，只在对接真实 PME 时才炸——故显式钉死。
        add_executable(test_capeopen100_rid
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen100_rid.cpp
        )
        target_link_libraries(test_capeopen100_rid PRIVATE capeopen100_minlp GTest::gtest)
        add_test(NAME CapeOpen100RidTest COMMAND test_capeopen100_rid)

        # Unit/Collection/Parameter 与我们自己的扩展，同样把 RID 钉死。
        add_executable(test_capeopen100_unit_rid
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_capeopen100_unit_rid.cpp
        )
        target_link_libraries(test_capeopen100_unit_rid PRIVATE capeopen100_minlp GTest::gtest)
        add_test(NAME CapeOpen100UnitRidTest COMMAND test_capeopen100_unit_rid)
    endif()
endif()
