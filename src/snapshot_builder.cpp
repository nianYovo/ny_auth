#include "snapshot_builder.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

namespace {

// ======================================================
// 当前时间转字符串
// 例如：2026-04-15 21:30:45
// ======================================================
std::string BuildCurrentDateTimeString() {
    const std::time_t now = std::time(nullptr);

    std::tm tm_value {};
#if defined(_WIN32)
    localtime_s(&tm_value, &now);
#else
    localtime_r(&now, &tm_value);
#endif

    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_value);

    return std::string(buffer);
}

// ======================================================
// JSON 字符串转义
// 作用：让字符串能安全写入 JSON
// ======================================================
std::string EscapeJsonString(const std::string& input) {
    std::ostringstream oss;

    for (unsigned char ch : input) {
        switch (ch) {
            case '\"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\b':
                oss << "\\b";
                break;
            case '\f':
                oss << "\\f";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                if (ch < 0x20) {
                    oss << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch)
                        << std::dec << std::setfill(' ');
                } else {
                    oss << static_cast<char>(ch);
                }
                break;
        }
    }

    return oss.str();
}

// ======================================================
// BuildSnapshotJsonText
// 作用：把 PolicySnapshot 手工序列化成 JSON 字符串
//
// 说明：
// 1. 这里故意和 SnapshotDAO 的私有序列化能力保持同风格
// 2. 当前这样做是为了让 SnapshotBuilder 能把“实际落库 JSON”回填到结果中
// 3. 后面如果你想进一步重构，可以把这块抽成共享工具类
// ======================================================
std::string BuildSnapshotJsonText(const PolicySnapshot& snapshot) {
    std::ostringstream oss;

    oss << "{";

    // app_info
    oss << "\"app_info\":{"
        << "\"app_id\":" << snapshot.app_info.app_id << ","
        << "\"app_code\":\"" << EscapeJsonString(snapshot.app_info.app_code) << "\","
        << "\"enabled\":" << (snapshot.app_info.enabled ? "true" : "false") << ","
        << "\"policy_version\":" << snapshot.app_info.policy_version
        << "},";

    // meta
    oss << "\"meta\":{"
        << "\"snapshot_id\":" << snapshot.meta.snapshot_id << ","
        << "\"published_by\":\"" << EscapeJsonString(snapshot.meta.published_by) << "\","
        << "\"publish_note\":\"" << EscapeJsonString(snapshot.meta.publish_note) << "\","
        << "\"created_at\":\"" << EscapeJsonString(snapshot.meta.created_at) << "\""
        << "},";

    // roles
    oss << "\"roles\":[";
    for (std::size_t i = 0; i < snapshot.roles.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }

        const auto& role = snapshot.roles[i];
        oss << "{"
            << "\"role_id\":" << role.role_id << ","
            << "\"role_key\":\"" << EscapeJsonString(role.role_key) << "\","
            << "\"role_name\":\"" << EscapeJsonString(role.role_name) << "\","
            << "\"description\":\"" << EscapeJsonString(role.description) << "\","
            << "\"is_default\":" << (role.is_default ? "true" : "false") << ","
            << "\"enabled\":" << (role.enabled ? "true" : "false")
            << "}";
    }
    oss << "],";

    // permissions
    oss << "\"permissions\":[";
    for (std::size_t i = 0; i < snapshot.permissions.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }

        const auto& perm = snapshot.permissions[i];
        oss << "{"
            << "\"permission_id\":" << perm.permission_id << ","
            << "\"perm_key\":\"" << EscapeJsonString(perm.perm_key) << "\","
            << "\"perm_name\":\"" << EscapeJsonString(perm.perm_name) << "\","
            << "\"resource_type\":\"" << EscapeJsonString(perm.resource_type) << "\","
            << "\"owner_shortcut_enabled\":"
            << (perm.owner_shortcut_enabled ? "true" : "false") << ","
            << "\"description\":\"" << EscapeJsonString(perm.description) << "\","
            << "\"enabled\":" << (perm.enabled ? "true" : "false")
            << "}";
    }
    oss << "],";

    // role_permission_bindings
    oss << "\"role_permission_bindings\":[";
    for (std::size_t i = 0; i < snapshot.role_permission_bindings.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }

        const auto& item = snapshot.role_permission_bindings[i];
        oss << "{"
            << "\"role_key\":\"" << EscapeJsonString(item.role_key) << "\","
            << "\"perm_key\":\"" << EscapeJsonString(item.perm_key) << "\""
            << "}";
    }
    oss << "],";

    // user_role_bindings
    oss << "\"user_role_bindings\":[";
    for (std::size_t i = 0; i < snapshot.user_role_bindings.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }

        const auto& item = snapshot.user_role_bindings[i];
        oss << "{"
            << "\"user_id\":\"" << EscapeJsonString(item.user_id) << "\","
            << "\"role_key\":\"" << EscapeJsonString(item.role_key) << "\","
            << "\"granted_by\":\"" << EscapeJsonString(item.granted_by) << "\""
            << "}";
    }
    oss << "],";

    // resource_owners
    oss << "\"resource_owners\":[";
    for (std::size_t i = 0; i < snapshot.resource_owners.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }

        const auto& item = snapshot.resource_owners[i];
        oss << "{"
            << "\"resource_type\":\"" << EscapeJsonString(item.resource_type) << "\","
            << "\"resource_id\":\"" << EscapeJsonString(item.resource_id) << "\","
            << "\"resource_name\":\"" << EscapeJsonString(item.resource_name) << "\","
            << "\"owner_user_id\":\"" << EscapeJsonString(item.owner_user_id) << "\","
            << "\"enabled\":" << (item.enabled ? "true" : "false") << ","
            << "\"metadata_text\":\"" << EscapeJsonString(item.metadata_text) << "\""
            << "}";
    }
    oss << "]";

    oss << "}";

    return oss.str();
}

// ======================================================
// 从形如：APP_NOT_FOUND: 应用不存在
// 提取错误码 APP_NOT_FOUND
// ======================================================
std::string ExtractErrorCode(const std::string& error_text) {
    const std::size_t pos = error_text.find(':');
    if (pos == std::string::npos) {
        return "INTERNAL_ERROR";
    }

    return error_text.substr(0, pos);
}

// ======================================================
// 从形如：APP_NOT_FOUND: 应用不存在
// 提取错误消息 应用不存在
// ======================================================
std::string ExtractErrorMessage(const std::string& error_text) {
    const std::size_t pos = error_text.find(':');
    if (pos == std::string::npos) {
        return error_text.empty() ? "系统内部错误" : error_text;
    }

    if (pos + 1 >= error_text.size()) {
        return "系统内部错误";
    }

    std::string message = error_text.substr(pos + 1);

    // 去掉开头可能的一个空格
    if (!message.empty() && message.front() == ' ') {
        message.erase(message.begin());
    }

    return message;
}

void BindPublishLog(sql::PreparedStatement* stmt,
                    const SnapshotPublishLogRecord& record) {
    stmt->setInt64(1, record.app_id);
    stmt->setString(2, record.app_code);
    stmt->setInt(3, record.policy_version);
    stmt->setInt64(4, record.snapshot_id);
    stmt->setString(5, record.published_by);
    stmt->setString(6, record.publish_result);
    stmt->setString(7, record.trace_text);
}

}  // namespace

// ======================================================
// 构造函数
// 作用：保存数据库连接参数和 SnapshotDAO
// ======================================================
SnapshotBuilder::SnapshotBuilder(const std::string& host,
                                 int port,
                                 const std::string& user,
                                 const std::string& password,
                                 const std::string& database,
                                 std::shared_ptr<SnapshotDAO> snapshot_dao)
    : host_(host),
      port_(port),
      user_(user),
      password_(password),
      database_(database),
      snapshot_dao_(std::move(snapshot_dao)) {}

// ======================================================
// BuildSnapshot
// 作用：只构建快照，不落库
//
// 流程：
// 1. 校验 app_code
// 2. 加载 app 信息
// 3. 检查 app 状态
// 4. 加载角色 / 权限 / 绑定 / owner
// 5. 组装成 PolicySnapshot
// ======================================================
std::optional<PolicySnapshot> SnapshotBuilder::BuildSnapshot(
    const std::string& app_code,
    const std::string& published_by,
    const std::string& publish_note) {
    // 清理旧错误
    setLastError("");

    if (app_code.empty()) {
        setLastError("INVALID_ARGUMENT: app_code 不能为空");
        return std::nullopt;
    }

    auto app_info_opt = loadAppInfo(app_code);
    if (!app_info_opt.has_value()) {
        if (getLastError().empty()) {
            setLastError("APP_NOT_FOUND: 应用不存在");
        }
        return std::nullopt;
    }

    const SnapshotAppInfo app_info = app_info_opt.value();

    if (!app_info.enabled) {
        setLastError("APP_DISABLED: 应用已被禁用");
        return std::nullopt;
    }

    PolicySnapshot snapshot;
    snapshot.app_info = app_info;
    snapshot.meta = buildSnapshotMeta(published_by, publish_note);

    // 加载角色
    snapshot.roles = loadRoles(app_info.app_id);
    if (!getLastError().empty()) {
        return std::nullopt;
    }

    // 加载权限
    snapshot.permissions = loadPermissions(app_info.app_id);
    if (!getLastError().empty()) {
        return std::nullopt;
    }

    // 加载角色-权限绑定
    snapshot.role_permission_bindings = loadRolePermissionBindings(app_info.app_id);
    if (!getLastError().empty()) {
        return std::nullopt;
    }

    // 加载用户-角色绑定
    snapshot.user_role_bindings = loadUserRoleBindings(app_info.app_id);
    if (!getLastError().empty()) {
        return std::nullopt;
    }

    // 加载资源 owner
    snapshot.resource_owners = loadResourceOwners(app_info.app_id);
    if (!getLastError().empty()) {
        return std::nullopt;
    }

    return snapshot;
}

// ======================================================
// BuildAndStoreSnapshot
// 作用：构建并保存快照，同时写快照发布日志
//
// 流程：
// 1. BuildSnapshot
// 2. 调 SnapshotDAO 保存
// 3. 更新 snapshot_id
// 4. 再次保存一遍，把 snapshot_id 写进 JSON
// 5. 写发布日志
// ======================================================
SnapshotBuildResult SnapshotBuilder::BuildAndStoreSnapshot(
    const std::string& app_code,
    const std::string& published_by,
    const std::string& publish_note) {
    SnapshotBuildResult result;

    if (!snapshot_dao_) {
        result.status.success = false;
        result.status.message = "系统内部错误：SnapshotDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";
        setLastError("INTERNAL_ERROR: SnapshotDAO 未初始化");
        return result;
    }

    if (app_code.empty()) {
        result.status.success = false;
        result.status.message = "参数不完整：app_code 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";
        setLastError("INVALID_ARGUMENT: app_code 不能为空");
        return result;
    }

    // 1) 先构建快照
    auto snapshot_opt = BuildSnapshot(app_code, published_by, publish_note);
    if (!snapshot_opt.has_value()) {
        const std::string error_text = getLastError();
        result.status.success = false;
        result.status.error_code = ExtractErrorCode(error_text);
        result.status.message = ExtractErrorMessage(error_text);

        // 构建失败，也尽量写一条失败发布日志
        SnapshotPublishLogRecord failed_log;
        failed_log.app_id = 0;
        failed_log.app_code = app_code;
        failed_log.policy_version = 0;
        failed_log.snapshot_id = 0;
        failed_log.published_by = published_by;
        failed_log.publish_result = "FAILED";
        failed_log.trace_text = error_text.empty()
            ? "snapshot build failed"
            : error_text;
        snapshot_dao_->insertSnapshotPublishLog(failed_log);

        return result;
    }

    PolicySnapshot snapshot = snapshot_opt.value();

    // 2) 先生成 JSON 并第一次保存
    // 说明：
    // 第一次保存时 snapshot.meta.snapshot_id 还是 0
    // 保存成功后，我们再把真实 snapshot_id 回填进快照里，
    // 然后再保存一次，让 JSON 内容也带上正确 snapshot_id
    std::string snapshot_json = BuildSnapshotJsonText(snapshot);

    int64_t snapshot_id = snapshot_dao_->saveSnapshot(snapshot, snapshot_json);
    if (snapshot_id == 0) {
        result.status.success = false;
        result.status.message = "保存快照失败";
        result.status.error_code = "INTERNAL_ERROR";

        SnapshotPublishLogRecord failed_log;
        failed_log.app_id = snapshot.app_info.app_id;
        failed_log.app_code = snapshot.app_info.app_code;
        failed_log.policy_version = snapshot.app_info.policy_version;
        failed_log.snapshot_id = 0;
        failed_log.published_by = published_by;
        failed_log.publish_result = "FAILED";
        failed_log.trace_text = "saveSnapshot failed";
        snapshot_dao_->insertSnapshotPublishLog(failed_log);

        return result;
    }

    // 3) 回填 snapshot_id，再重新生成 JSON 并覆盖保存
    snapshot.meta.snapshot_id = snapshot_id;
    snapshot_json = BuildSnapshotJsonText(snapshot);

    const int64_t final_snapshot_id = snapshot_dao_->saveSnapshot(snapshot, snapshot_json);
    if (final_snapshot_id == 0) {
        result.status.success = false;
        result.status.message = "保存快照失败（回填 snapshot_id 阶段）";
        result.status.error_code = "INTERNAL_ERROR";

        SnapshotPublishLogRecord failed_log;
        failed_log.app_id = snapshot.app_info.app_id;
        failed_log.app_code = snapshot.app_info.app_code;
        failed_log.policy_version = snapshot.app_info.policy_version;
        failed_log.snapshot_id = snapshot_id;
        failed_log.published_by = published_by;
        failed_log.publish_result = "FAILED";
        failed_log.trace_text = "saveSnapshot failed when updating snapshot_id into json";
        snapshot_dao_->insertSnapshotPublishLog(failed_log);

        return result;
    }

    // 4) 写成功发布日志
    SnapshotPublishLogRecord success_log;
    success_log.app_id = snapshot.app_info.app_id;
    success_log.app_code = snapshot.app_info.app_code;
    success_log.policy_version = snapshot.app_info.policy_version;
    success_log.snapshot_id = final_snapshot_id;
    success_log.published_by = published_by;
    success_log.publish_result = "SUCCESS";
    success_log.trace_text = buildSnapshotTraceText(snapshot);

    snapshot_dao_->insertSnapshotPublishLog(success_log);

    // 5) 回填结果
    result.status.success = true;
    result.status.message = "构建并保存快照成功";
    result.status.error_code = "OK";
    result.snapshot = snapshot;
    result.snapshot_id = final_snapshot_id;
    result.snapshot_json = snapshot_json;

    return result;
}

// ======================================================
// PublishPolicyTransaction
// 作用：在同一个数据库事务中完成策略版本发布和快照保存
//
// 流程：
// 1. 锁定 app 和策略版本
// 2. 计算新版本号并更新 ny_policy_versions
// 3. 基于同一事务读取完整策略数据
// 4. 写入/覆盖 ny_policy_snapshots
// 5. 回填 snapshot_id 到 JSON
// 6. 写 ny_snapshot_publish_logs
// 7. commit；任何失败 rollback
// ======================================================
SnapshotBuildResult SnapshotBuilder::PublishPolicyTransaction(
    const std::string& app_code,
    const std::string& published_by,
    const std::string& publish_note) {
    SnapshotBuildResult result;
    setLastError("");

    if (app_code.empty()) {
        result.status.success = false;
        result.status.message = "参数不完整：app_code 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";
        setLastError("INVALID_ARGUMENT: app_code 不能为空");
        return result;
    }

    std::unique_ptr<sql::Connection> conn;
    int64_t app_id = 0;
    int new_version = 0;

    try {
        conn.reset(createConnection());
        conn->setAutoCommit(false);

        std::unique_ptr<sql::PreparedStatement> app_stmt(
            conn->prepareStatement(
                "SELECT a.id, a.app_code, a.status, "
                "       COALESCE(pv.current_version, 1) AS current_version "
                "FROM ny_apps a "
                "LEFT JOIN ny_policy_versions pv ON pv.app_id = a.id "
                "WHERE a.app_code = ? "
                "LIMIT 1 "
                "FOR UPDATE"
            )
        );
        app_stmt->setString(1, app_code);
        std::unique_ptr<sql::ResultSet> app_rs(app_stmt->executeQuery());
        if (!app_rs->next()) {
            throw std::runtime_error("APP_NOT_FOUND: 应用不存在");
        }

        SnapshotAppInfo app_info;
        app_info.app_id = app_rs->getInt64("id");
        app_info.app_code = app_rs->getString("app_code");
        app_info.enabled = (app_rs->getInt("status") == 1);
        app_info.policy_version = app_rs->getInt("current_version") + 1;

        if (!app_info.enabled) {
            throw std::runtime_error("APP_DISABLED: 应用已被禁用");
        }

        app_id = app_info.app_id;
        new_version = app_info.policy_version;

        std::unique_ptr<sql::PreparedStatement> version_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_policy_versions ("
                "  app_id, current_version, published_by, publish_note "
                ") VALUES (?, ?, ?, ?) "
                "ON DUPLICATE KEY UPDATE "
                "  current_version = VALUES(current_version), "
                "  published_by = VALUES(published_by), "
                "  publish_note = VALUES(publish_note), "
                "  updated_at = CURRENT_TIMESTAMP"
            )
        );
        version_stmt->setInt64(1, app_id);
        version_stmt->setInt(2, new_version);
        version_stmt->setString(3, published_by);
        version_stmt->setString(4, publish_note);
        if (version_stmt->executeUpdate() <= 0) {
            throw std::runtime_error("INTERNAL_ERROR: 更新策略版本失败");
        }

        PolicySnapshot snapshot;
        snapshot.app_info = app_info;
        snapshot.meta = buildSnapshotMeta(published_by, publish_note);

        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "SELECT id, role_key, role_name, description, is_default, status "
                    "FROM ny_roles "
                    "WHERE app_id = ? "
                    "ORDER BY id ASC"
                )
            );
            stmt->setInt64(1, app_id);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                SnapshotRole role;
                role.role_id = rs->getInt64("id");
                role.role_key = rs->getString("role_key");
                role.role_name = rs->getString("role_name");
                role.description = rs->getString("description");
                role.is_default = (rs->getInt("is_default") == 1);
                role.enabled = (rs->getInt("status") == 1);
                snapshot.roles.push_back(role);
            }
        }

        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "SELECT id, perm_key, perm_name, resource_type, "
                    "       owner_shortcut_enabled, description, status "
                    "FROM ny_permissions "
                    "WHERE app_id = ? "
                    "ORDER BY id ASC"
                )
            );
            stmt->setInt64(1, app_id);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                SnapshotPermission perm;
                perm.permission_id = rs->getInt64("id");
                perm.perm_key = rs->getString("perm_key");
                perm.perm_name = rs->getString("perm_name");
                perm.resource_type = rs->getString("resource_type");
                perm.owner_shortcut_enabled = (rs->getInt("owner_shortcut_enabled") == 1);
                perm.description = rs->getString("description");
                perm.enabled = (rs->getInt("status") == 1);
                snapshot.permissions.push_back(perm);
            }
        }

        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "SELECT r.role_key, p.perm_key "
                    "FROM ny_role_permissions rp "
                    "JOIN ny_roles r ON r.id = rp.role_id "
                    "JOIN ny_permissions p ON p.id = rp.perm_id "
                    "WHERE r.app_id = ? "
                    "  AND p.app_id = ? "
                    "ORDER BY r.id ASC, p.id ASC"
                )
            );
            stmt->setInt64(1, app_id);
            stmt->setInt64(2, app_id);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                SnapshotRolePermissionBinding item;
                item.role_key = rs->getString("role_key");
                item.perm_key = rs->getString("perm_key");
                snapshot.role_permission_bindings.push_back(item);
            }
        }

        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "SELECT ur.app_user_id, r.role_key, ur.granted_by "
                    "FROM ny_user_roles ur "
                    "JOIN ny_roles r ON r.id = ur.role_id "
                    "WHERE ur.app_id = ? "
                    "ORDER BY ur.id ASC"
                )
            );
            stmt->setInt64(1, app_id);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                SnapshotUserRoleBinding item;
                item.user_id = rs->getString("app_user_id");
                item.role_key = rs->getString("role_key");
                item.granted_by = rs->getString("granted_by");
                snapshot.user_role_bindings.push_back(item);
            }
        }

        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "SELECT resource_type, resource_id, resource_name, "
                    "       owner_user_id, metadata_text, status "
                    "FROM ny_resources "
                    "WHERE app_id = ? "
                    "ORDER BY id ASC"
                )
            );
            stmt->setInt64(1, app_id);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                SnapshotResourceOwner item;
                item.resource_type = rs->getString("resource_type");
                item.resource_id = rs->getString("resource_id");
                item.resource_name = rs->getString("resource_name");
                item.owner_user_id = rs->getString("owner_user_id");
                item.metadata_text = rs->getString("metadata_text");
                item.enabled = (rs->getInt("status") == 1);
                snapshot.resource_owners.push_back(item);
            }
        }

        std::string snapshot_json = BuildSnapshotJsonText(snapshot);

        std::unique_ptr<sql::PreparedStatement> snapshot_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_policy_snapshots ("
                "  app_id, app_code, policy_version, snapshot_json, status, "
                "  published_by, publish_note "
                ") VALUES (?, ?, ?, ?, 1, ?, ?) "
                "ON DUPLICATE KEY UPDATE "
                "  snapshot_json = VALUES(snapshot_json), "
                "  status = VALUES(status), "
                "  published_by = VALUES(published_by), "
                "  publish_note = VALUES(publish_note), "
                "  updated_at = CURRENT_TIMESTAMP"
            )
        );
        snapshot_stmt->setInt64(1, snapshot.app_info.app_id);
        snapshot_stmt->setString(2, snapshot.app_info.app_code);
        snapshot_stmt->setInt(3, snapshot.app_info.policy_version);
        snapshot_stmt->setString(4, snapshot_json);
        snapshot_stmt->setString(5, snapshot.meta.published_by);
        snapshot_stmt->setString(6, snapshot.meta.publish_note);
        if (snapshot_stmt->executeUpdate() <= 0) {
            throw std::runtime_error("INTERNAL_ERROR: 保存策略快照失败");
        }

        std::unique_ptr<sql::PreparedStatement> snapshot_id_stmt(
            conn->prepareStatement(
                "SELECT id "
                "FROM ny_policy_snapshots "
                "WHERE app_id = ? "
                "  AND policy_version = ? "
                "LIMIT 1"
            )
        );
        snapshot_id_stmt->setInt64(1, snapshot.app_info.app_id);
        snapshot_id_stmt->setInt(2, snapshot.app_info.policy_version);
        std::unique_ptr<sql::ResultSet> snapshot_id_rs(snapshot_id_stmt->executeQuery());
        if (!snapshot_id_rs->next()) {
            throw std::runtime_error("INTERNAL_ERROR: 查询策略快照 id 失败");
        }

        snapshot.meta.snapshot_id = snapshot_id_rs->getInt64("id");
        snapshot_json = BuildSnapshotJsonText(snapshot);

        std::unique_ptr<sql::PreparedStatement> snapshot_update_stmt(
            conn->prepareStatement(
                "UPDATE ny_policy_snapshots "
                "SET snapshot_json = ?, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = ?"
            )
        );
        snapshot_update_stmt->setString(1, snapshot_json);
        snapshot_update_stmt->setInt64(2, snapshot.meta.snapshot_id);
        if (snapshot_update_stmt->executeUpdate() <= 0) {
            throw std::runtime_error("INTERNAL_ERROR: 回填策略快照 id 失败");
        }

        SnapshotPublishLogRecord success_log;
        success_log.app_id = snapshot.app_info.app_id;
        success_log.app_code = snapshot.app_info.app_code;
        success_log.policy_version = snapshot.app_info.policy_version;
        success_log.snapshot_id = snapshot.meta.snapshot_id;
        success_log.published_by = published_by;
        success_log.publish_result = "SUCCESS";
        success_log.trace_text = buildSnapshotTraceText(snapshot);

        std::unique_ptr<sql::PreparedStatement> log_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_snapshot_publish_logs ("
                "  app_id, app_code, policy_version, snapshot_id, "
                "  published_by, publish_result, trace_text "
                ") VALUES (?, ?, ?, ?, ?, ?, ?)"
            )
        );
        BindPublishLog(log_stmt.get(), success_log);
        if (log_stmt->executeUpdate() <= 0) {
            throw std::runtime_error("INTERNAL_ERROR: 写入快照发布日志失败");
        }

        conn->commit();
        conn->setAutoCommit(true);

        result.status.success = true;
        result.status.message = "发布策略并保存快照成功";
        result.status.error_code = "OK";
        result.snapshot = snapshot;
        result.snapshot_id = snapshot.meta.snapshot_id;
        result.snapshot_json = snapshot_json;
        return result;
    } catch (const std::exception& e) {
        if (conn) {
            try {
                conn->rollback();
                conn->setAutoCommit(true);
            } catch (...) {
            }
        }

        const std::string error_text = e.what();
        result.status.success = false;
        result.status.error_code = ExtractErrorCode(error_text);
        result.status.message = ExtractErrorMessage(error_text);
        if (result.status.error_code.empty()) {
            result.status.error_code = "INTERNAL_ERROR";
        }
        if (result.status.message.empty()) {
            result.status.message = "发布策略失败";
        }
        setLastError(error_text);

        if (snapshot_dao_) {
            SnapshotPublishLogRecord failed_log;
            failed_log.app_id = app_id;
            failed_log.app_code = app_code;
            failed_log.policy_version = new_version;
            failed_log.snapshot_id = 0;
            failed_log.published_by = published_by;
            failed_log.publish_result = "FAILED";
            failed_log.trace_text = error_text.empty() ? "transactional publish failed" : error_text;
            snapshot_dao_->insertSnapshotPublishLog(failed_log);
        }

        return result;
    }
}

// ======================================================
// BuildIndexedSnapshot
// 作用：把原始快照转换成“本地快速判权友好”的索引快照
// ======================================================
IndexedPolicySnapshot SnapshotBuilder::BuildIndexedSnapshot(
    const PolicySnapshot& snapshot) const {
    IndexedPolicySnapshot indexed_snapshot;

    // 保留原始快照
    indexed_snapshot.snapshot = snapshot;

    // role_key -> role
    for (const auto& role : snapshot.roles) {
        indexed_snapshot.role_key_to_role[role.role_key] = role;
    }

    // perm_key -> permission
    for (const auto& perm : snapshot.permissions) {
        indexed_snapshot.perm_key_to_permission[perm.perm_key] = perm;
    }

    // role_key -> permissions
    for (const auto& item : snapshot.role_permission_bindings) {
        indexed_snapshot.role_to_permissions[item.role_key].insert(item.perm_key);
    }

    // user_id -> roles
    for (const auto& item : snapshot.user_role_bindings) {
        indexed_snapshot.user_to_roles[item.user_id].insert(item.role_key);
    }

    // resource key -> owner/detail
    for (const auto& item : snapshot.resource_owners) {
        const std::string resource_key =
            BuildSnapshotResourceKey(item.resource_type, item.resource_id);

        indexed_snapshot.resource_to_owner[resource_key] = item.owner_user_id;
        indexed_snapshot.resource_to_detail[resource_key] = item;
    }

    return indexed_snapshot;
}

// ======================================================
// getLastError
// 作用：返回最近一次构建错误
// ======================================================
std::string SnapshotBuilder::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

// ======================================================
// createConnection
// 作用：创建一个新的 MySQL 连接
// ======================================================
sql::Connection* SnapshotBuilder::createConnection() {
    sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

    std::ostringstream oss;
    oss << "tcp://" << host_ << ":" << port_;

    sql::Connection* conn = driver->connect(oss.str(), user_, password_);
    conn->setSchema(database_);

    return conn;
}

// ======================================================
// setLastError
// 作用：线程安全地记录最近一次错误
// ======================================================
void SnapshotBuilder::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

// ======================================================
// loadAppInfo
// 作用：加载 app 基础信息
// 数据来源：
// - ny_apps
// - ny_policy_versions
// ======================================================
std::optional<SnapshotAppInfo> SnapshotBuilder::loadAppInfo(
    const std::string& app_code) {
    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT a.id, a.app_code, a.status, "
                "       COALESCE(pv.current_version, 1) AS current_version "
                "FROM ny_apps a "
                "LEFT JOIN ny_policy_versions pv ON pv.app_id = a.id "
                "WHERE a.app_code = ? "
                "LIMIT 1"
            )
        );

        stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            return std::nullopt;
        }

        SnapshotAppInfo app_info;
        app_info.app_id = rs->getInt64("id");
        app_info.app_code = rs->getString("app_code");
        app_info.enabled = (rs->getInt("status") == 1);
        app_info.policy_version = rs->getInt("current_version");

        return app_info;
    } catch (const sql::SQLException& e) {
        setLastError(std::string("INTERNAL_ERROR: loadAppInfo failed: ") + e.what());
        return std::nullopt;
    }
}

// ======================================================
// loadRoles
// 作用：加载角色列表
// 数据来源：ny_roles
// ======================================================
std::vector<SnapshotRole> SnapshotBuilder::loadRoles(int64_t app_id) {
    std::vector<SnapshotRole> items;

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT id, role_key, role_name, description, is_default, status "
                "FROM ny_roles "
                "WHERE app_id = ? "
                "ORDER BY id ASC"
            )
        );

        stmt->setInt64(1, app_id);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            SnapshotRole role;
            role.role_id = rs->getInt64("id");
            role.role_key = rs->getString("role_key");
            role.role_name = rs->getString("role_name");
            role.description = rs->getString("description");
            role.is_default = (rs->getInt("is_default") == 1);
            role.enabled = (rs->getInt("status") == 1);

            items.push_back(role);
        }
    } catch (const sql::SQLException& e) {
        setLastError(std::string("INTERNAL_ERROR: loadRoles failed: ") + e.what());
        items.clear();
    }

    return items;
}

// ======================================================
// loadPermissions
// 作用：加载权限列表
// 数据来源：ny_permissions
// ======================================================
std::vector<SnapshotPermission> SnapshotBuilder::loadPermissions(int64_t app_id) {
    std::vector<SnapshotPermission> items;

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT id, perm_key, perm_name, resource_type, "
                "       owner_shortcut_enabled, description, status "
                "FROM ny_permissions "
                "WHERE app_id = ? "
                "ORDER BY id ASC"
            )
        );

        stmt->setInt64(1, app_id);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            SnapshotPermission perm;
            perm.permission_id = rs->getInt64("id");
            perm.perm_key = rs->getString("perm_key");
            perm.perm_name = rs->getString("perm_name");
            perm.resource_type = rs->getString("resource_type");
            perm.owner_shortcut_enabled =
                (rs->getInt("owner_shortcut_enabled") == 1);
            perm.description = rs->getString("description");
            perm.enabled = (rs->getInt("status") == 1);

            items.push_back(perm);
        }
    } catch (const sql::SQLException& e) {
        setLastError(std::string("INTERNAL_ERROR: loadPermissions failed: ") + e.what());
        items.clear();
    }

    return items;
}

// ======================================================
// loadRolePermissionBindings
// 作用：加载角色-权限绑定
// 数据来源：
// - ny_role_permissions
// - ny_roles
// - ny_permissions
// ======================================================
std::vector<SnapshotRolePermissionBinding>
SnapshotBuilder::loadRolePermissionBindings(int64_t app_id) {
    std::vector<SnapshotRolePermissionBinding> items;

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT r.role_key, p.perm_key "
                "FROM ny_role_permissions rp "
                "JOIN ny_roles r ON r.id = rp.role_id "
                "JOIN ny_permissions p ON p.id = rp.perm_id "
                "WHERE r.app_id = ? "
                "  AND p.app_id = ? "
                "ORDER BY r.id ASC, p.id ASC"
            )
        );

        stmt->setInt64(1, app_id);
        stmt->setInt64(2, app_id);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            SnapshotRolePermissionBinding item;
            item.role_key = rs->getString("role_key");
            item.perm_key = rs->getString("perm_key");

            items.push_back(item);
        }
    } catch (const sql::SQLException& e) {
        setLastError(
            std::string("INTERNAL_ERROR: loadRolePermissionBindings failed: ") + e.what());
        items.clear();
    }

    return items;
}

// ======================================================
// loadUserRoleBindings
// 作用：加载用户-角色绑定
// 数据来源：
// - ny_user_roles
// - ny_roles
// ======================================================
std::vector<SnapshotUserRoleBinding>
SnapshotBuilder::loadUserRoleBindings(int64_t app_id) {
    std::vector<SnapshotUserRoleBinding> items;

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT ur.app_user_id, r.role_key, ur.granted_by "
                "FROM ny_user_roles ur "
                "JOIN ny_roles r ON r.id = ur.role_id "
                "WHERE ur.app_id = ? "
                "ORDER BY ur.id ASC"
            )
        );

        stmt->setInt64(1, app_id);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            SnapshotUserRoleBinding item;
            item.user_id = rs->getString("app_user_id");
            item.role_key = rs->getString("role_key");
            item.granted_by = rs->getString("granted_by");

            items.push_back(item);
        }
    } catch (const sql::SQLException& e) {
        setLastError(
            std::string("INTERNAL_ERROR: loadUserRoleBindings failed: ") + e.what());
        items.clear();
    }

    return items;
}

// ======================================================
// loadResourceOwners
// 作用：加载资源 owner 列表
// 数据来源：ny_resources
// ======================================================
std::vector<SnapshotResourceOwner>
SnapshotBuilder::loadResourceOwners(int64_t app_id) {
    std::vector<SnapshotResourceOwner> items;

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT resource_type, resource_id, resource_name, "
                "       owner_user_id, metadata_text, status "
                "FROM ny_resources "
                "WHERE app_id = ? "
                "ORDER BY id ASC"
            )
        );

        stmt->setInt64(1, app_id);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            SnapshotResourceOwner item;
            item.resource_type = rs->getString("resource_type");
            item.resource_id = rs->getString("resource_id");
            item.resource_name = rs->getString("resource_name");
            item.owner_user_id = rs->getString("owner_user_id");
            item.metadata_text = rs->getString("metadata_text");
            item.enabled = (rs->getInt("status") == 1);

            items.push_back(item);
        }
    } catch (const sql::SQLException& e) {
        setLastError(
            std::string("INTERNAL_ERROR: loadResourceOwners failed: ") + e.what());
        items.clear();
    }

    return items;
}

// ======================================================
// buildSnapshotMeta
// 作用：生成快照元信息
//
// 当前阶段：
// - snapshot_id 先置 0
// - created_at 先用当前时间字符串
// 真正入库后，BuildAndStoreSnapshot 会回填 snapshot_id
// ======================================================
SnapshotMeta SnapshotBuilder::buildSnapshotMeta(
    const std::string& published_by,
    const std::string& publish_note) const {
    SnapshotMeta meta;
    meta.snapshot_id = 0;
    meta.published_by = published_by;
    meta.publish_note = publish_note;
    meta.created_at = BuildCurrentDateTimeString();
    return meta;
}

// ======================================================
// buildSnapshotTraceText
// 作用：生成一段可读的快照构建摘要
// 用于写入快照发布日志
// ======================================================
std::string SnapshotBuilder::buildSnapshotTraceText(
    const PolicySnapshot& snapshot) const {
    std::ostringstream oss;
    oss << "snapshot built"
        << ", app_code=" << snapshot.app_info.app_code
        << ", policy_version=" << snapshot.app_info.policy_version
        << ", roles=" << snapshot.roles.size()
        << ", permissions=" << snapshot.permissions.size()
        << ", role_permission_bindings=" << snapshot.role_permission_bindings.size()
        << ", user_role_bindings=" << snapshot.user_role_bindings.size()
        << ", resource_owners=" << snapshot.resource_owners.size()
        << ", published_by=" << snapshot.meta.published_by;

    return oss.str();
}

// ======================================================
// nowUnixSeconds
// 作用：获取当前 Unix 秒级时间戳
// ======================================================
int64_t SnapshotBuilder::nowUnixSeconds() const {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    );
    return static_cast<int64_t>(seconds.count());
}
