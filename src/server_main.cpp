// 提供 std::make_shared
#include <memory>

// 提供 std::unordered_set
#include <unordered_set>

// 提供日志输出
#include <butil/logging.h>

// 提供 gflags 命令行参数解析
#include <gflags/gflags.h>

// 提供 bRPC 服务端
#include <brpc/server.h>

// 引入缓存
#include "local_cache.h"

// ====================== 中心鉴权 ======================
// 引入运行时 DAO
#include "permission_dao.h"

// 引入决策引擎
#include "decision_engine.h"

// 引入运行时 RPC 服务
#include "auth_service_impl.h"

// ====================== 管理端 ======================
// 引入管理员会话结构
#include "session_types.h"

// 引入管理端 DAO
#include "admin_dao.h"

// 引入模拟引擎
#include "simulation_engine.h"

// 引入管理端业务逻辑层
#include "admin_manager.h"

// 引入管理端 RPC 服务
#include "admin_service_impl.h"

// ====================== Agent / Sidecar ======================
// 引入快照 DAO
#include "snapshot_dao.h"

// 引入快照构建器
#include "snapshot_builder.h"

// 引入本地快照判权引擎
#include "local_snapshot_engine.h"

// 引入 Agent / Sidecar RPC 服务
#include "agent_service_impl.h"

// ======================================================
// 命令行参数
// ======================================================

// 服务监听端口
DEFINE_int32(port, 8001, "ny_auth server port");

// 数据库主机
DEFINE_string(db_host, "127.0.0.1", "MySQL host");

// 数据库端口
DEFINE_int32(db_port, 3306, "MySQL port");

// 数据库用户名
DEFINE_string(db_user, "root", "MySQL user");

// 数据库密码
DEFINE_string(db_password, "123456", "MySQL password");

// 数据库名
DEFINE_string(db_name, "ny_auth", "MySQL database name");

// 运行时权限缓存 TTL（秒）
DEFINE_int32(cache_ttl, 60, "runtime permission cache ttl in seconds");

// 运行时权限缓存最大条目数，0 表示不限制
DEFINE_uint64(cache_max_entries, 100000, "runtime permission cache max entries, 0 means unlimited");

// 管理员登录 session TTL（秒）
DEFINE_int32(admin_session_ttl, 3600, "admin session ttl in seconds");

// 管理员 session 缓存最大条目数，0 表示不限制
DEFINE_uint64(admin_session_cache_max_entries, 10000, "admin session cache max entries, 0 means unlimited");

// 是否在启动时自动加载某个 app 的最新快照
// 为空表示不自动加载
DEFINE_string(agent_bootstrap_app_code, "",
              "app_code to auto-load latest snapshot into LocalSnapshotEngine on startup");

// ======================================================
// main
// 程序入口
// ======================================================
int main(int argc, char* argv[]) {
    // 解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // --------------------------------------------------
    // 运行时权限缓存类型
    // value = 用户权限集合
    // --------------------------------------------------
    using UserPermSet = std::unordered_set<std::string>;

    // --------------------------------------------------
    // 管理员登录 session 缓存类型
    // value = AdminSessionInfo
    // --------------------------------------------------
    using AdminSessionCache = LocalCache<AdminSessionInfo>;

    // ==================================================
    // 1) 创建运行时权限缓存
    // ==================================================
    auto runtime_permission_cache =
        std::make_shared<LocalCache<UserPermSet>>(
            static_cast<std::size_t>(FLAGS_cache_max_entries)
        );

    // ==================================================
    // 2) 创建管理员 session 缓存
    // ==================================================
    auto admin_session_cache =
        std::make_shared<AdminSessionCache>(
            static_cast<std::size_t>(FLAGS_admin_session_cache_max_entries)
        );

    // ==================================================
    // 3) 创建运行时 DAO
    // ==================================================
    auto permission_dao = std::make_shared<PermissionDAO>(
        FLAGS_db_host,
        FLAGS_db_port,
        FLAGS_db_user,
        FLAGS_db_password,
        FLAGS_db_name
    );

    // ==================================================
    // 4) 创建管理端 DAO
    // ==================================================
    auto admin_dao = std::make_shared<AdminDAO>(
        FLAGS_db_host,
        FLAGS_db_port,
        FLAGS_db_user,
        FLAGS_db_password,
        FLAGS_db_name
    );

    // ==================================================
    // 5) 创建快照 DAO
    // ==================================================
    auto snapshot_dao = std::make_shared<SnapshotDAO>(
        FLAGS_db_host,
        FLAGS_db_port,
        FLAGS_db_user,
        FLAGS_db_password,
        FLAGS_db_name
    );

    // ==================================================
    // 6) 启动前检查数据库连通性
    //    三条链路都检查，尽早暴露问题
    // ==================================================
    if (!permission_dao->isConnected()) {
        LOG(ERROR) << "PermissionDAO 数据库连接失败"
                   << ", host=" << FLAGS_db_host
                   << ", port=" << FLAGS_db_port
                   << ", db=" << FLAGS_db_name
                   << ", last_error=" << permission_dao->getLastError();
        return -1;
    }

    if (!admin_dao->isConnected()) {
        LOG(ERROR) << "AdminDAO 数据库连接失败"
                   << ", host=" << FLAGS_db_host
                   << ", port=" << FLAGS_db_port
                   << ", db=" << FLAGS_db_name
                   << ", last_error=" << admin_dao->getLastError();
        return -1;
    }

    if (!snapshot_dao->isConnected()) {
        LOG(ERROR) << "SnapshotDAO 数据库连接失败"
                   << ", host=" << FLAGS_db_host
                   << ", port=" << FLAGS_db_port
                   << ", db=" << FLAGS_db_name
                   << ", last_error=" << snapshot_dao->getLastError();
        return -1;
    }

    // ==================================================
    // 7) 创建决策引擎
    // ==================================================
    auto decision_engine = std::make_shared<DecisionEngine>(
        runtime_permission_cache,
        permission_dao,
        FLAGS_cache_ttl
    );

    // ==================================================
    // 8) 创建 RPC 服务：AuthServiceImpl
    // ==================================================
    AuthServiceImpl auth_service(decision_engine);

    // ==================================================
    // 9) 创建模拟引擎
    // ==================================================
    auto simulation_engine = std::make_shared<SimulationEngine>(
        permission_dao,
        admin_dao
    );

    // ==================================================
    // 10) 创建快照构建器
    // ==================================================
    auto snapshot_builder = std::make_shared<SnapshotBuilder>(
        FLAGS_db_host,
        FLAGS_db_port,
        FLAGS_db_user,
        FLAGS_db_password,
        FLAGS_db_name,
        snapshot_dao
    );

    // ==================================================
    // 11) 创建管理逻辑层：AdminManager
    // ==================================================
    auto admin_manager = std::make_shared<AdminManager>(
        admin_dao,
        simulation_engine,
        admin_session_cache,
        FLAGS_admin_session_ttl,
        snapshot_builder
    );

    // ==================================================
    // 12) 创建 RPC 服务：AdminServiceImpl
    // ==================================================
    AdminServiceImpl admin_service(admin_manager);

    // ==================================================
    // 13) 创建本地快照判权引擎
    // ==================================================
    auto local_snapshot_engine = std::make_shared<LocalSnapshotEngine>();

    // ==================================================
    // 14) 可选的启动即加载快照
    //
    // 逻辑：
    // - 如果传了 --agent_bootstrap_app_code=xxx
    // - 则尝试读取该 app 最新快照
    // - 成功则建立索引并加载到 LocalSnapshotEngine
    // - 失败不退出进程，只打警告日志
    // ==================================================
    if (!FLAGS_agent_bootstrap_app_code.empty()) {
        auto bootstrap_snapshot_opt =
            snapshot_dao->getLatestSnapshot(FLAGS_agent_bootstrap_app_code);

        if (!bootstrap_snapshot_opt.has_value()) {
            LOG(WARNING) << "启动时自动加载快照失败：未找到最新快照"
                         << ", app_code=" << FLAGS_agent_bootstrap_app_code;
        } else {
            const IndexedPolicySnapshot indexed_snapshot =
                snapshot_builder->BuildIndexedSnapshot(bootstrap_snapshot_opt.value());

            if (!local_snapshot_engine->LoadSnapshot(indexed_snapshot)) {
                LOG(WARNING) << "启动时自动加载快照失败：LoadSnapshot 返回 false"
                             << ", app_code=" << FLAGS_agent_bootstrap_app_code;
            } else {
                LOG(INFO) << "启动时自动加载快照成功"
                          << ", app_code=" << local_snapshot_engine->CurrentAppCode()
                          << ", snapshot_id=" << local_snapshot_engine->CurrentSnapshotId()
                          << ", policy_version=" << local_snapshot_engine->CurrentPolicyVersion();
            }
        }
    }

    // ==================================================
    // 15) 创建 Agent / Sidecar RPC 服务
    // ==================================================
    AgentServiceImpl agent_service(
        snapshot_dao,
        snapshot_builder,
        local_snapshot_engine
    );

    // ==================================================
    // 16) 创建 brpc Server
    // ==================================================
    brpc::Server server;

    // --------------------------------------------------
    // 注册运行时鉴权服务
    // --------------------------------------------------
    if (server.AddService(&auth_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "AddService(AuthServiceImpl) 失败";
        return -1;
    }

    // --------------------------------------------------
    // 注册管理端服务
    // --------------------------------------------------
    if (server.AddService(&admin_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "AddService(AdminServiceImpl) 失败";
        return -1;
    }

    // --------------------------------------------------
    // 注册 Agent / Sidecar 服务
    // --------------------------------------------------
    if (server.AddService(&agent_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "AddService(AgentServiceImpl) 失败";
        return -1;
    }

    // ==================================================
    // 17) 配置并启动服务
    // ==================================================
    brpc::ServerOptions options;

    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "服务启动失败，port=" << FLAGS_port;
        return -1;
    }

    // ==================================================
    // 18) 打印启动成功日志
    // ==================================================
    LOG(INFO) << "ny_auth 启动成功"
              << ", port=" << FLAGS_port
              << ", db_host=" << FLAGS_db_host
              << ", db_port=" << FLAGS_db_port
              << ", db_name=" << FLAGS_db_name
              << ", runtime_cache_ttl=" << FLAGS_cache_ttl
              << ", runtime_cache_max_entries=" << FLAGS_cache_max_entries
              << ", admin_session_ttl=" << FLAGS_admin_session_ttl
              << ", admin_session_cache_max_entries=" << FLAGS_admin_session_cache_max_entries
              << ", agent_bootstrap_app_code=" << FLAGS_agent_bootstrap_app_code
              << ", local_snapshot_loaded=" << (local_snapshot_engine->HasSnapshot() ? "true" : "false")
              << ", services=[AuthService, AdminService, AgentService]";

    // 阻塞运行，直到收到退出信号
    server.RunUntilAskedToQuit();

    return 0;
}
