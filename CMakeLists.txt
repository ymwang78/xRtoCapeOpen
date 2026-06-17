cmake_minimum_required(VERSION 3.10)
project(RtoCapeOpen)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(IDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/SqpSolver.idl)
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 创建生成目录
file(MAKE_DIRECTORY ${GENERATED_DIR})

# 添加自定义命令，调用 tao_idl 生成服务器端文件
# 这里假设生成的文件包含 SqpSolverS.cpp 和 SqpSolverS.h，如有需要可根据实际情况调整
add_custom_command(
    OUTPUT ${GENERATED_DIR}/SqpSolverS.cpp ${GENERATED_DIR}/SqpSolverS.h
    COMMAND tao_idl -o ${GENERATED_DIR} ${IDL_FILE}
    DEPENDS ${IDL_FILE}
    COMMENT "Generating server stubs from SqpSolver.idl"
)
cmake_policy(SET CMP0074 NEW)

# 将生成的源文件添加到源文件列表中
set(GENERATED_SOURCES
    ${GENERATED_DIR}/SqpSolverS.cpp
)

# 你自己的源文件
set(MY_SOURCES SqpcSovlerImpl.cpp)

# 合并所有源文件
set(SOURCES ${MY_SOURCES} ${GENERATED_SOURCES})

# 将生成目录添加到 include 路径中，这样 SqpSolverS.h 能够被正确引用
include_directories(${GENERATED_DIR})

# 添加共享库 target (在 Linux 下生成 libRtoCapeOpen.so, 你可以修改目标名称以生成 RtoCapeOpen.so)
add_library(RtoCapeOpen SHARED ${SOURCES})

message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")

# 查找 TAO 和 ACE（前提是它们已安装并能被 find_package 找到）
find_package(ACE REQUIRED)
message(STATUS "ACE_FOUND: ${ACE_FOUND}")
message(STATUS "ACE_INCLUDE_DIR: ${ACE_INCLUDE_DIR}")
message(STATUS "ACE_INCLUDE_DIRS: ${ACE_INCLUDE_DIRS}")
message(STATUS "ACE_LIBRARY: ${ACE_LIBRARY}")
message(STATUS "ACE_LIBRARIES: ${ACE_LIBRARIES}")

find_package(TAO REQUIRED)

message(STATUS "TAO_FOUND: ${TAO_FOUND}")
message(STATUS "TAO_INCLUDE_DIR: ${TAO_INCLUDE_DIR}")
message(STATUS "TAO_INCLUDE_DIRS: ${TAO_INCLUDE_DIRS}")
message(STATUS "TAO_LIBRARY: ${TAO_LIBRARY}")
message(STATUS "TAO_LIBRARIES: ${TAO_LIBRARIES}")

# 添加 TAO 和 ACE 的 include 目录
target_include_directories(RtoCapeOpen PRIVATE ${TAO_INCLUDE_DIRS} ${ACE_INCLUDE_DIRS})

# 链接 TAO 和 ACE 库
target_link_libraries(RtoCapeOpen ${TAO_LIBRARIES} ${ACE_LIBRARIES})
