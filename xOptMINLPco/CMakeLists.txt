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
    XOptModelDesc.cpp   # C-ABI 黑箱模型的部署描述（参数/组分表/固定变量）
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
    #
    # target 名刻意**不叫** xOptMINLPco：它与静态核心库 xoptminlpco 只差大小写，
    # 而 Visual Studio 生成器按 target 名产出 <名字>.vcxproj——在大小写不敏感的
    # 文件系统上两个工程文件是同一个，MSBuild 解析工程引用时就绕成环，报
    # `MSB4006 ... ResolveProjectReferences ... 循环依赖`，整个解决方案建不动
    # （Ninja 不产工程文件，所以一直没暴露）。这和下面那条导入库改名是同一个
    # 撞车的两个层面：那条撞的是 .lib，这条撞的是 .vcxproj。
    #
    # 对外产物不变：OUTPUT_NAME 仍是 xOptMINLPco，DLL 依旧是 xOptMINLPco.dll，
    # 注册表 InprocServer32、xOptMINLPco.def、手写的 xOptMINLPco.vcxproj 都不受影响。
    add_library(xoptminlpco_dll SHARED
        xOptMINLPcoServer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/xOptMINLPco.def
    )
    target_link_libraries(xoptminlpco_dll PRIVATE xoptminlpco_com)
    # DLL 名为 xOptMINLPco.dll；导入库改名避免与静态库 xoptminlpco.lib 大小写冲突(LNK1149)。
    set_target_properties(xoptminlpco_dll PROPERTIES
        OUTPUT_NAME "xOptMINLPco" PREFIX ""
        ARCHIVE_OUTPUT_NAME "xOptMINLPco_import")
endif()

# ---- N4：CORBA 前端 ----
if(WITH_XOPTMINLPCO_CORBA)
    # UnitServant 与 MINLPServant 同库：两者共用同一个 XOptMINLPAdapter，
    # 也共用同一批 IDL stub，拆开只会多一条链接顺序约束。
    add_library(xoptminlpco_corba STATIC MINLPServant.cpp UnitServant.cpp)
    # capeopen_corba PUBLIC 传递 TAO include/defines/libs + capeopen100_minlp
    # 由 tao_idl 从 CAPEOPEN100_Minlp.idl 生成的 stub 头
    target_link_libraries(xoptminlpco_corba PUBLIC xoptminlpco capeopen_corba)

    # 独立进程 CORBA server：CORBA 没有 COM 那样的进程内激活，外部 ORB 只能
    # 经 IIOP 连到一个真在跑 ORB 的进程，所以这里必须是 exe 而不是 DLL。
    add_executable(xOptMINLPcoCorbaServer MINLPCorbaServer.cpp)
    # --name 走 Naming Service（issue #6）：需要 orbsvcs 的 CosNaming stub。
    # 头文件在 <triplet>/include/orbsvcs/，故 include 根仍是 TAO_INCLUDE_DIR。
    target_link_libraries(xOptMINLPcoCorbaServer PRIVATE xoptminlpco_corba TAO_CosNamings)
endif()

# ---- 单元测试 ----
option(XOPTMINLPCO_BUILD_TESTS "Build xOptMINLPco unit tests" ON)
if(XOPTMINLPCO_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    # 测试夹具：导出 createProblem/destroyProblem 的 xOptProblem DLL（mock）。
    # COM 激活冒烟(N3)与 CORBA 跨进程冒烟(N4)都要它经 XRTO_XOPT_PROBLEM_DLL 加载，
    # 故放在两个前端开关之外。
    if(WIN32)
        add_library(mock_xoptproblem SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../tests/mock_xoptproblem_dll.cpp)
        target_include_directories(mock_xoptproblem PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests ${XOPT_INCLUDE_DIR})
        set_target_properties(mock_xoptproblem PROPERTIES OUTPUT_NAME "mock_xoptproblem" PREFIX "")
    endif()

    add_executable(test_xoptminlpco_adapter
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_adapter.cpp
    )
    target_link_libraries(test_xoptminlpco_adapter PRIVATE xoptminlpco GTest::gtest)
    add_test(NAME XOptMINLPcoAdapterTest COMMAND test_xoptminlpco_adapter)

    # N5：C-ABI(xOptProblemT) 输入路径
    add_executable(test_xoptminlpco_capi
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_capi.cpp
    )
    target_link_libraries(test_xoptminlpco_capi PRIVATE xoptminlpco GTest::gtest)
    add_test(NAME XOptMINLPcoCapiTest COMMAND test_xoptminlpco_capi)

    # 测试夹具：导出 xOptModel_createModel 的 C-ABI 模型 DLL，且刻意要求完整的
    # 初始化握手（参数/slate/校验/generateEstimate）才肯 buildProblem。
    # 测试按绝对路径加载它，故不限 Windows。
    add_library(mock_xoptmodel SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../tests/mock_xoptmodel_dll.cpp)
    target_include_directories(mock_xoptmodel PRIVATE ${XOPT_INCLUDE_DIR})

    # 输出目录固定在一个自己的子目录里，两个原因：
    #   1. 路径要能作为 C 字符串字面量传给测试。$<TARGET_FILE:> 在 MSVC 下可能
    #      给出反斜杠路径，塞进 -D"...=\"C:\path\to\x.dll\"" 后 \p、\t 会被
    #      编译器当转义序列吃掉——\t 直接变成制表符。用普通 CMake 变量 +
    #      TO_CMAKE_PATH 拼，正斜杠是有保证的。
    #   2. 自动发现测试要往 DLL 同目录写 *_Model.json，独占一个目录不会碰到
    #      别的构建产物。
    set(MOCK_XOPTMODEL_DIR "${CMAKE_CURRENT_BINARY_DIR}/mockmodel")
    set_target_properties(mock_xoptmodel PROPERTIES PREFIX "" OUTPUT_NAME "mock_xoptmodel")
    # 多配置生成器会在 RUNTIME_OUTPUT_DIRECTORY 后追加 /$<CONFIG>，逐配置钉死才作数。
    foreach(cfg IN ITEMS "" DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        if(cfg STREQUAL "")
            set(prop_suffix "")
        else()
            set(prop_suffix "_${cfg}")
        endif()
        set_target_properties(mock_xoptmodel PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY${prop_suffix} "${MOCK_XOPTMODEL_DIR}"
            LIBRARY_OUTPUT_DIRECTORY${prop_suffix} "${MOCK_XOPTMODEL_DIR}")
    endforeach()
    file(TO_CMAKE_PATH "${MOCK_XOPTMODEL_DIR}" MOCK_XOPTMODEL_DIR_FWD)

    # 描述 JSON 解析 + initModel 端到端
    add_executable(test_xoptminlpco_modeldesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_modeldesc.cpp
    )
    target_link_libraries(test_xoptminlpco_modeldesc PRIVATE xoptminlpco GTest::gtest)
    target_compile_definitions(test_xoptminlpco_modeldesc PRIVATE
        MOCK_XOPTMODEL_DLL="${MOCK_XOPTMODEL_DIR_FWD}/mock_xoptmodel${CMAKE_SHARED_LIBRARY_SUFFIX}")
    add_dependencies(test_xoptminlpco_modeldesc mock_xoptmodel)
    add_test(NAME XOptMINLPcoModelDescTest COMMAND test_xoptminlpco_modeldesc)

    if(WITH_XOPTMINLPCO_COM)
        # 回环：mock → adapter → CoMINLP → capeopen_core CapeMINLPModelCom
        add_executable(test_xoptminlpco_com
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_com.cpp
        )
        target_link_libraries(test_xoptminlpco_com PRIVATE xoptminlpco_com GTest::gtest)
        add_test(NAME XOptMINLPcoComTest COMMAND test_xoptminlpco_com)

        # COM 侧索引基的非回环校验（issue #2）：手工构造 1-based VARIANT 直接驱动
        # CoMINLP。上面那条回环用例两端都是我们自己，一起偏也照绿。
        add_executable(test_xoptminlpco_com_onebased
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_com_onebased.cpp
        )
        target_link_libraries(test_xoptminlpco_com_onebased PRIVATE xoptminlpco_com GTest::gtest)
        target_include_directories(test_xoptminlpco_com_onebased PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
        add_test(NAME XOptMINLPcoComOneBasedTest COMMAND test_xoptminlpco_com_onebased)

        # N3：注册 + CoCreateInstance 激活冒烟（用注册的 xOptMINLPco.dll 加载 mock DLL）
        add_executable(test_xoptminlpco_register
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_register.cpp
        )
        target_link_libraries(test_xoptminlpco_register PRIVATE capeopen_core GTest::gtest)
        target_include_directories(test_xoptminlpco_register PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_dependencies(test_xoptminlpco_register xoptminlpco_dll mock_xoptproblem)
        add_test(NAME XOptMINLPcoRegisterTest COMMAND test_xoptminlpco_register)
    endif()

    if(WITH_XOPTMINLPCO_CORBA)
        # 回环：mock → adapter → MINLPServant → capeopen_core CapeMINLPModelCorba（collocated）
        add_executable(test_xoptminlpco_corba
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_corba.cpp
        )
        target_link_libraries(test_xoptminlpco_corba PRIVATE xoptminlpco_corba GTest::gtest)
        add_test(NAME XOptMINLPcoCorbaTest COMMAND test_xoptminlpco_corba)

        # 索引基的非回环校验：直接对着 CORBA stub 手工构造 1-based 入参、手工
        # 写死 1-based 期望值。上面那条回环用例两端都是我们自己，一起偏也照绿。
        # 接线信息回环：模型 -> adapter -> UnitServant -> (IOR 绕一圈) -> CapeUnitCorba
        add_executable(test_xoptminlpco_unitports
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_unitports.cpp
        )
        target_link_libraries(test_xoptminlpco_unitports
            PRIVATE xoptminlpco_corba capeopen_corba GTest::gtest)
        target_compile_definitions(test_xoptminlpco_unitports PRIVATE
            MOCK_XOPTMODEL_DLL="${MOCK_XOPTMODEL_DIR_FWD}/mock_xoptmodel${CMAKE_SHARED_LIBRARY_SUFFIX}")
        add_dependencies(test_xoptminlpco_unitports mock_xoptmodel)
        add_test(NAME XOptMINLPcoUnitPortsTest COMMAND test_xoptminlpco_unitports)

        add_executable(test_xoptminlpco_onebased
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_onebased.cpp
        )
        target_link_libraries(test_xoptminlpco_onebased PRIVATE xoptminlpco_corba GTest::gtest)
        target_include_directories(test_xoptminlpco_onebased PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
        add_test(NAME XOptMINLPcoOneBasedTest COMMAND test_xoptminlpco_onebased)

        # 真·跨进程：起 xOptMINLPcoCorbaServer.exe，读它发布的 IOR，经
        # corba: 连接串连回来。与上面的 collocated 用例互补——那条走注入构造，
        # 从不碰 string_to_object/_narrow，也就验不到 IIOP。
        add_executable(test_xoptminlpco_corba_ipc
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_corba_ipc.cpp
        )
        target_link_libraries(test_xoptminlpco_corba_ipc PRIVATE capeopen_corba GTest::gtest)
        add_dependencies(test_xoptminlpco_corba_ipc xOptMINLPcoCorbaServer mock_xoptproblem)
        add_test(NAME XOptMINLPcoCorbaIpcTest COMMAND test_xoptminlpco_corba_ipc)

        # Naming Service 端到端（issue #6）：起 tao_cosnaming → server --name →
        # 客户端经 corbaname: 解析名字连回来。与上面的 IOR 直连用例互补——
        # 「名字能被解析到」是 --name 唯一真正的产出，只有真跑一次 naming 才知道。
        add_executable(test_xoptminlpco_corba_naming
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_xoptminlpco_corba_naming.cpp
        )
        target_link_libraries(test_xoptminlpco_corba_naming PRIVATE capeopen_corba GTest::gtest)
        # naming service 可执行文件在 <triplet>/tools/ace/，测试要自己拉起它。
        target_compile_definitions(test_xoptminlpco_corba_naming PRIVATE
            XRTO_TAO_TOOLS_DIR="${TAO_TRIPLET_DIR}/tools/ace")
        add_dependencies(test_xoptminlpco_corba_naming xOptMINLPcoCorbaServer mock_xoptproblem)
        add_test(NAME XOptMINLPcoCorbaNamingTest COMMAND test_xoptminlpco_corba_naming)
    endif()
endif()
