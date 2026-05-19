# 指定 CMake 最低版本
cmake_minimum_required(VERSION 3.10)

# 定义项目名称
project(ny_auth)

# 指定使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启常用编译警告
add_compile_options(-Wall -Wextra -Wpedantic)

# =========================================================
# 查找依赖
# =========================================================

# 线程库
find_package(Threads REQUIRED)

# Protobuf
find_package(Protobuf REQUIRED)

# pkg-config
find_package(PkgConfig REQUIRED)

# 通过 pkg-config 查找 brpc
# 前提：系统里已经正确安装 brpc，并且有 brpc.pc
pkg_check_modules(BRPC REQUIRED brpc)

# 查找 MySQL Connector/C++
find_path(MYSQL_INCLUDE_DIR mysql_driver.h)
find_library(MYSQL_LIBRARY mysqlcppconn)

# 查找 gflags
find_library(GFLAGS_LIBRARY gflags)

# 如果没找到 mysqlcppconn，则直接报错
if (NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "mysqlcppconn not found")
endif()

# 如果没找到 gflags，则直接报错
if (NOT GFLAGS_LIBRARY)
    message(FATAL_ERROR "gflags library not found")
endif()

# =========================================================
# proto 生成源文件
#
# protoc --cpp_out=src --proto_path=proto proto/auth.proto
# protoc --cpp_out=src --proto_path=proto proto/admin.proto
# protoc --cpp_out=src --proto_path=proto proto/agent.proto
# =========================================================
set(PROTO_SRCS
    src/auth.pb.cc
    src/admin.pb.cc
    src/agent.pb.cc
)

set(PROTO_HDRS
    src/auth.pb.h
    src/admin.pb.h
    src/agent.pb.h
)

# =========================================================
# 可执行程序：auth_server
#
# 现在它同时包含：
# - 中心鉴权服务
# - 管理端服务
# - Agent / Sidecar 服务
# =========================================================
add_executable(auth_server
    # main
    src/server_main.cpp

    # 中心鉴权
    src/permission_dao.cpp
    src/decision_engine.cpp
    src/auth_service_impl.cpp

    # 管理端
    src/admin_dao.cpp
    src/simulation_engine.cpp
    src/admin_manager.cpp
    src/admin_service_impl.cpp

    # Agent / Sidecar
    src/snapshot_dao.cpp
    src/snapshot_builder.cpp
    src/local_snapshot_engine.cpp
    src/agent_service_impl.cpp

    # protobuf 生成代码
    ${PROTO_SRCS}
)

# =========================================================
# 指定头文件搜索路径
# =========================================================
target_include_directories(auth_server PRIVATE
    include
    src
    ${PROTOBUF_INCLUDE_DIRS}
    ${BRPC_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIR}
)

# =========================================================
# 链接依赖库
# =========================================================
target_link_libraries(auth_server
    ${PROTOBUF_LIBRARIES}
    ${BRPC_LIBRARIES}
    ${MYSQL_LIBRARY}
    ${GFLAGS_LIBRARY}
    Threads::Threads
    pthread
    dl
    z
    ssl
    crypto
    leveldb
)

# =========================================================
# 打印调试信息
# =========================================================
message(STATUS "Project name: ${PROJECT_NAME}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Protobuf includes: ${PROTOBUF_INCLUDE_DIRS}")
message(STATUS "BRPC includes: ${BRPC_INCLUDE_DIRS}")
message(STATUS "MySQL include dir: ${MYSQL_INCLUDE_DIR}")
message(STATUS "MySQL library: ${MYSQL_LIBRARY}")
message(STATUS "gflags library: ${GFLAGS_LIBRARY}")
