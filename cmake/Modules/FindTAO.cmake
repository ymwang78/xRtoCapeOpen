# FindTAO.cmake
#
# 查找 TAO（The ACE ORB）库
#
# 该模块定义以下变量：
#   TAO_FOUND       - 如果找到了 TAO 则为 TRUE
#   TAO_INCLUDE_DIR - TAO 头文件所在目录
#   TAO_LIBRARY     - TAO 库文件
#   TAO_LIBRARIES   - 通常等同于 TAO_LIBRARY，方便后续链接时使用
#
# 可选：可以通过设置环境变量 TAO_ROOT 来帮助搜索

find_path(TAO_INCLUDE_DIR
  NAMES tao/ORB_Core.h
  HINTS ENV TAO_ROOT
  PATH_SUFFIXES include
  DOC "目录：TAO 的头文件所在目录"
)

find_library(TAO_LIBRARY
  NAMES TAO
  HINTS ENV ACE_ROOT TAO_ROOT
  PATH_SUFFIXES lib tao
  DOC "TAO 库文件"
)

find_library(TAO_PORTABLESERVER_LIBRARY
  NAMES TAO_PortableServer
  HINTS ENV ACE_ROOT TAO_ROOT
  PATH_SUFFIXES lib tao
  DOC "TAO 库文件"
)

find_library(TAO_ANYTYPECODE_LIBRARY
  NAMES TAO_AnyTypeCode
  HINTS ENV ACE_ROOT TAO_ROOT
  PATH_SUFFIXES lib tao
  DOC "TAO 库文件"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TAO DEFAULT_MSG TAO_INCLUDE_DIR TAO_LIBRARY)

if(TAO_FOUND)
  set(TAO_INCLUDE_DIRS ${TAO_INCLUDE_DIR} ${TAO_INCLUDE_DIR}/orbsvcs)
  set(TAO_LIBRARIES ${TAO_LIBRARY} ${TAO_PORTABLESERVER_LIBRARY} ${TAO_ANYTYPECODE_LIBRARY})
endif()

mark_as_advanced(TAO_INCLUDE_DIR TAO_INCLUDE_DIRS TAO_LIBRARY TAO_LIBRARIES)


