# ***************************************************************
#  xRtoCapeOpen —— 仓库根聚合器
#
#  这里不定义任何 target，只把构建入口收敛到 xOptMINLPco/：
#  它自己 add_subdirectory(../core)，于是 `cmake -S .` 与既有的
#  `cmake -S xOptMINLPco` 产出完全一致的构建树。
#
#  历史：本文件原先构建一个 SHARED target `RtoCapeOpen`（SqpSolver.idl
#  的求解器侧 servant，SqpcSovlerImpl.cpp）。该通路已废弃——MINLP 走
#  官方模块路径的 CAPEOPEN100_Minlp.idl（见 core/CMakeLists.txt 与
#  docs/xOptMINLPco_design.md §6），自造的 `IDL:SqpSolver/ICapeMINLP:1.0`
#  与规范不符，且 servant 各方法体本就是空壳。target 与其源文件、
#  仅它使用的 cmake/Modules/Find{ACE,TAO}.cmake 一并删除。
#  SqpSolver.idl 保留：docs 引它作接口对照。
#
#  注意：core 自己的单测（test_capeopen_problem / test_capeopen_corba /
#  test_capeopen100_rid）不在这里构建——xOptMINLPco 会 FORCE 掉
#  CAPEOPEN_CORE_BUILD_TESTS。要跑它们，单独 `cmake -S core`。
# ***************************************************************
cmake_minimum_required(VERSION 3.10)
project(xRtoCapeOpen CXX)

enable_testing()

add_subdirectory(xOptMINLPco)
