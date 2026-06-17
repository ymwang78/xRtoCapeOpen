# FindACE.cmake
#
# 查找 ACE（Adaptive Communication Environment）库
#
# 该模块定义以下变量：
#   ACE_FOUND       - 如果找到了 ACE 则为 TRUE
#   ACE_INCLUDE_DIR - ACE 头文件所在目录
#   ACE_LIBRARY     - ACE 库文件
#   ACE_LIBRARIES   - 通常等同于 ACE_LIBRARY，方便后续链接时使用
#
# 可选：可以通过设置环境变量 ACE_ROOT 来帮助搜索

find_path(ACE_INCLUDE_DIR
  NAMES ace/ACE.h
  HINTS ENV ACE_ROOT
  PATH_SUFFIXES include
  DOC "目录：ACE 的头文件所在目录"
)

find_library(ACE_LIBRARY
  NAMES ACE
  HINTS ENV ACE_ROOT
  PATH_SUFFIXES lib
  DOC "ACE 库文件"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ACE DEFAULT_MSG ACE_INCLUDE_DIR ACE_LIBRARY)

if(ACE_FOUND)
  set(ACE_INCLUDE_DIRS ${ACE_INCLUDE_DIR})
  set(ACE_LIBRARIES ${ACE_LIBRARY})
endif()

mark_as_advanced(ACE_INCLUDE_DIR ACE_INCLUDE_DIRS ACE_LIBRARY ACE_LIBRARIES)
