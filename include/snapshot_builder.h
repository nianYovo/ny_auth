#ifndef NY_AUTH_SNAPSHOT_BUILDER_H
#define NY_AUTH_SNAPSHOT_BUILDER_H

// 提供 int64_t
#include <cstdint>

// 提供 std::shared_ptr
#include <memory>

// 提供 std::mutex
#include <mutex>

// 提供 std::optional
#include <optional>

// 提供 std::string
#include <string>

// 提供 std::vector
#include <vector>

// MySQL Connector/C++ 头文件
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <mysql_driver.h>

// 引入快照数据结构
#include "snapshot_types.h"

// 引入快照 DAO
#include "snapshot_dao.h"

// ======================================================
// SnapshotBuildStatus
// 作用：统一表示“快照构建 / 保存”的状态
// ======================================================
struct SnapshotBuildStatus {
    // 是否成功
    bool success = false;

    // 可读提示
    std::string message;

    // 错误码（字符串形式）
    // 例如：
    // OK
    // INVALID_ARGUMENT
    // APP_NOT_FOUND
    // APP_DISABLED
    // INTERNAL_ERROR
    std::string error_code;
};

// ======================================================
// SnapshotBuildResult
// 作用：表示一次“构建并保存快照”的最终结果
// ======================================================
struct SnapshotBuildResult {
    // 通用状态
    SnapshotBuildStatus status;

    // 构建出来的结构化快照
    PolicySnapshot snapshot;

    // 保存后的快照主键 id
    int64_t snapshot_id = 0;

    // 保存时使用的 JSON 文本
    std::string snapshot_json;
};

// ======================================================
// SnapshotBuilder
// 作用：快照构建器
//
// 它负责：
// 1. 从真实数据库读取某个 app 当前的全量只读策略数据
// 2. 组装成一份 PolicySnapshot
// 3. 调用 SnapshotDAO 保存快照
// 4. 写入快照发布日志
//
// 注意：
// - 它不负责本地判权
// - 它不负责管理端登录
// - 它专注于“构建并发布快照”
// ======================================================
class SnapshotBuilder {
public:
    // 构造函数
    // snapshot_dao: 用于保存快照和写快照发布日志
    SnapshotBuilder(const std::string& host,
                    int port,
                    const std::string& user,
                    const std::string& password,
                    const std::string& database,
                    std::shared_ptr<SnapshotDAO> snapshot_dao);

    // --------------------------------------------------
    // 核心能力
    // --------------------------------------------------

    // 只构建，不落库
    // 用途：
    // - 调试构建逻辑
    // - 做发布前预览
    // - 做测试
    std::optional<PolicySnapshot> BuildSnapshot(const std::string& app_code,
                                                const std::string& published_by,
                                                const std::string& publish_note);

    // 构建并保存快照
    // 用途：
    // - 正式发布快照
    // - 构建完成后写入 ny_policy_snapshots
    // - 同时写入 ny_snapshot_publish_logs
    SnapshotBuildResult BuildAndStoreSnapshot(const std::string& app_code,
                                              const std::string& published_by,
                                              const std::string& publish_note);

    // 在同一个数据库事务中发布策略版本并保存快照
    SnapshotBuildResult PublishPolicyTransaction(const std::string& app_code,
                                                 const std::string& published_by,
                                                 const std::string& publish_note);

    // 把“原始结构化快照”建立成“索引快照”
    // 用途：
    // - 给 LocalSnapshotEngine 使用
    IndexedPolicySnapshot BuildIndexedSnapshot(const PolicySnapshot& snapshot) const;

    // 获取最近一次构建错误
    std::string getLastError() const;

private:
    // --------------------------------------------------
    // 数据库通用能力
    // --------------------------------------------------

    // 创建数据库连接
    sql::Connection* createConnection();

    // 线程安全地记录最近一次错误
    void setLastError(const std::string& error);

    // --------------------------------------------------
    // 快照源数据加载
    // 下面这些方法会从“真实数据库表”读取构建快照所需的数据
    // --------------------------------------------------

    // 加载 app 基础信息
    std::optional<SnapshotAppInfo> loadAppInfo(const std::string& app_code);

    // 加载角色列表
    std::vector<SnapshotRole> loadRoles(int64_t app_id);

    // 加载权限列表
    std::vector<SnapshotPermission> loadPermissions(int64_t app_id);

    // 加载角色-权限绑定
    std::vector<SnapshotRolePermissionBinding> loadRolePermissionBindings(int64_t app_id);

    // 加载用户-角色绑定
    std::vector<SnapshotUserRoleBinding> loadUserRoleBindings(int64_t app_id);

    // 加载资源 owner 列表
    std::vector<SnapshotResourceOwner> loadResourceOwners(int64_t app_id);

    // --------------------------------------------------
    // 构建辅助
    // --------------------------------------------------

    // 生成快照元信息
    SnapshotMeta buildSnapshotMeta(const std::string& published_by,
                                   const std::string& publish_note) const;

    // 把快照转成一段可读 trace，方便写发布日志
    std::string buildSnapshotTraceText(const PolicySnapshot& snapshot) const;

    // 获取当前 Unix 秒级时间戳
    int64_t nowUnixSeconds() const;

private:
    // MySQL 主机
    std::string host_;

    // MySQL 端口
    int port_;

    // 用户名
    std::string user_;

    // 密码
    std::string password_;

    // 数据库名
    std::string database_;

    // 快照 DAO
    std::shared_ptr<SnapshotDAO> snapshot_dao_;

    // 保护 last_error_
    mutable std::mutex error_mutex_;

    // 最近一次错误
    std::string last_error_;
};

#endif
