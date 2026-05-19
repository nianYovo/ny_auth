#include "admin_dao.h"

#include <memory>
#include <sstream>

// ======================================================
// 构造函数
// 作用：把数据库连接参数保存到成员变量中
// ======================================================
AdminDAO::AdminDAO(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& database) : host_(host), port_(port), user_(user), password_(password), database_(database) {}

// ======================================================
// createConnection
// 作用：创建一个新的 MySQL 数据库连接
// 当前仍然采用“每次查询新建连接”的简单模式
// ======================================================
sql::Connection* AdminDAO::createConnection() {
    
    sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

    std::ostringstream oss;
    oss << "tcp://" << host_ << ":" << port_;
    
    sql::Connection* conn =driver->connect(oss.str(), user_, password_);

    conn->setSchema(database_);

    return conn;
}

// ======================================================
// setLastError
// 作用：线程安全地记录最近一次数据库错误
// ======================================================
void AdminDAO::setLastError(const std::string& error) {

    std::lock_guard<std::mutex> lock(error_mutex_);

    last_error_ = error;
}

// ======================================================
// getConsoleUserByUsername
// 作用：根据用户名查询管理员账号
// ======================================================
std::optional<ConsoleUserInfo> AdminDAO::getConsoleUserByUsername(const std::string& username) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(

            conn->prepareStatement(

                "SELECT id, username, password_hash, display_name, status, is_super_admin "
                "FROM ny_console_users "
                "WHERE username = ? "
                "LIMIT 1 "
            )
        );

        stmt->setString(1, username);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        if(!rs->next()) {

            return std::nullopt;
        }

        ConsoleUserInfo info;

        info.id = rs->getInt64("id");
        info.username = rs->getString("username");
        info.password_hash = rs->getString("password_hash");
        info.display_name = rs->getString("display_name");
        info.enabled = (rs->getInt("status") == 1);
        info.is_super_admin = (rs->getInt("is_super_admin") == 1);

        return info;
    } catch (const sql::SQLException& e) {
        
        setLastError(e.what());

        return std::nullopt;
    }
}

// ======================================================
// updateConsoleUserLastLoginAt
// 作用：更新管理员最近登录时间
// ======================================================
bool AdminDAO::updateConsoleUserLastLoginAt(int64_t user_id) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "UPDATE ny_console_users "
                "SET last_login_at = CURRENT_TIMESTAMP "
                "WHERE id = ? "
            )
        );

        stmt->setInt64(1, user_id);

        return stmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        
        setLastError(e.what());
        
        return false;
    }
}

// ======================================================
// roleExists
// 作用：判断某个角色在指定 app 下是否存在
// ======================================================
bool AdminDAO::roleExists(const std::string& app_code, const std::string& role_key) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT 1 "
                "FROM ny_roles r "
                "JOIN ny_apps a ON a.id = r.app_id "
                "WHERE a.app_code = ? "
                "  AND r.role_key = ? "
                "LIMIT 1 "
            )
        );

        stmt->setString(1, app_code);
        stmt->setString(2, role_key);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        return rs->next();
    } catch (const sql::SQLException& e) {

        setLastError(e.what());

        return false;
    }
}

// ======================================================
// createRole
// 作用：在指定 app 下创建角色
// 返回：
//   成功 -> 新角色 id
//   失败 -> 0
// ======================================================
int64_t AdminDAO::createRole(const std::string& app_code, const std::string& role_key, const std::string& role_name, const std::string& description, bool is_default) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> find_app_stmt(
            conn->prepareStatement(
                "SELECT id FROM ny_apps WHERE app_code = ? LIMIT 1 "
            )
        );

        find_app_stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> app_rs(find_app_stmt->executeQuery());

        if(!app_rs->next()) {
            return 0;
        }

        int64_t app_id = app_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> insert_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_roles ("
                "   app_id, role_name, role_key, description, is_default, status "
                ") VALUES (?, ?, ?, ?, ?, 1)"
            )
        );

        insert_stmt->setInt64(1, app_id);
        insert_stmt->setString(2, role_name);
        insert_stmt->setString(3, role_key);
        insert_stmt->setString(4, description);
        insert_stmt->setInt(5, is_default ? 1 : 0);

        if(insert_stmt->executeUpdate() <= 0) {
            return 0;
        }

        std::unique_ptr<sql::PreparedStatement> query_stmt(
            conn->prepareStatement(
                "SELECT id "
                "FROM ny_roles "
                "WHERE app_id = ? AND role_key = ? "
                "LIMIT 1"
            )
        );

        query_stmt->setInt64(1, app_id);
        query_stmt->setString(2, role_key);

        std::unique_ptr<sql::ResultSet> role_rs(query_stmt->executeQuery());

        if(!role_rs->next()) {
            return 0;
        }

        return role_rs->getInt64("id");
    } catch (const sql::SQLException& e) {
        
        setLastError(e.what());

        return 0;
    }
}

// ======================================================
// permissionExists
// 作用：判断某个权限在指定 app 下是否存在
// ======================================================
bool AdminDAO::permissionExists(const std::string& app_code, const std::string& perm_key) {
    try {
        
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT 1 "
                "FROM ny_permissions p "
                "JOIN ny_apps a ON a.id = p.app_id "
                "WHERE a.app_code = ? "
                "  AND p.perm_key = ? "
                "LIMIT 1"
            )
        );

        stmt->setString(1, app_code);
        stmt->setString(2, perm_key);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        
        return rs->next();
    } catch (const sql::SQLException& e) {

        setLastError(e.what());

        return false;
    }
}

// ======================================================
// createPermission
// 作用：在指定 app 下创建权限
// 返回：
//   成功 -> 新权限 id
//   失败 -> 0
// ======================================================
int64_t AdminDAO::createPermission(const std::string& app_code, const std::string& perm_key, const std::string& perm_name, const std::string& resource_type, bool owner_shortcut_enabled, const std::string& description) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> find_app_stmt(
            conn->prepareStatement(
                "SELECT id FROM ny_apps WHERE app_code = ? LIMIT 1"
            )
        );

        find_app_stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> app_rs(find_app_stmt->executeQuery());
        if(!app_rs->next()){
            return 0;
        }

        int64_t app_id = app_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> insert_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_permissions ("
                "  app_id, perm_name, perm_key, resource_type, "
                "  owner_shortcut_enabled, description, status "
                ") VALUES (?, ?, ?, ?, ?, ?, 1)"
            )
        );

        insert_stmt->setInt64(1, app_id);
        insert_stmt->setString(2, perm_name);
        insert_stmt->setString(3, perm_key);
        insert_stmt->setString(4, resource_type);
        insert_stmt->setInt(5, owner_shortcut_enabled ? 1 : 0);
        insert_stmt->setString(6,  description);

        if(insert_stmt->executeUpdate() <= 0) {
            return 0;
        }

        std::unique_ptr<sql::PreparedStatement> query_stmt(
            conn->prepareStatement(
                "SELECT id "
                "FROM ny_permissions "
                "WHERE app_id = ? AND perm_key = ? "
                "LIMIT 1"
            )
        );

        query_stmt->setInt64(1, app_id);
        query_stmt->setString(2, perm_key);

        std::unique_ptr<sql::ResultSet> perm_rs(query_stmt->executeQuery());
        
        if(!perm_rs->next()) {
            return 0;
        }

        return perm_rs->getInt64("id");
    } catch (const sql::SQLException& e) {

        setLastError(e.what());

        return 0;
    }
}

// ======================================================
// rolePermissionBindingExists
// 作用：判断角色和权限是否已经绑定
// ======================================================
bool AdminDAO::rolePermissionBindingExists(const std::string& app_code, const std::string& role_key, const std::string& perm_key) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT 1 "
                "FROM ny_role_permissions rp "
                "JOIN ny_roles r ON r.id = rp.role_id "
                "JOIN ny_permissions p ON p.id = rp.perm_id "
                "JOIN ny_apps a ON a.id = r.app_id AND a.id = p.app_id "
                "WHERE a.app_code = ? "
                "  AND r.role_key = ? "
                "  AND p.perm_key = ? "
                "LIMIT 1 "
            )
        );

        stmt->setString(1, app_code);
        stmt->setString(2, role_key);
        stmt->setString(3, perm_key);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        return rs->next();
    } catch (const sql::SQLException& e) {
        
        setLastError(e.what());

        return false;
    }
}

// ======================================================
// bindPermissionToRole
// 作用：给角色绑定权限
// ======================================================
bool AdminDAO::bindPermissionToRole(const std::string& app_code, const std::string& role_key, const std::string& perm_key) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> role_stmt(
            conn->prepareStatement(
                "SELECT r.id "
                "FROM ny_roles r "
                "JOIN ny_apps a ON a.id = r.app_id "
                "WHERE a.app_code = ? "
                "  AND r.role_key = ? "
                "LIMIT 1 "
            )
        );

        role_stmt->setString(1, app_code);
        role_stmt->setString(2, role_key);

        std::unique_ptr<sql::ResultSet> role_rs(role_stmt->executeQuery());

        if(!role_rs->next()) {

            return false;
        }

        int64_t role_id = role_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> perm_stmt(
            conn->prepareStatement(
                "SELECT p.id "
                "FROM ny_permissions p "
                "JOIN ny_apps a ON a.id = p.app_id "
                "WHERE a.app_code = ? "
                "  AND p.perm_key = ? "
                "LIMIT 1 "
            )
        );

        perm_stmt->setString(1, app_code);
        perm_stmt->setString(2, perm_key);

        std::unique_ptr<sql::ResultSet> perm_rs(perm_stmt->executeQuery());

        if(!perm_rs->next()) {

            return false;
        }

        int64_t perm_id = perm_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> insert_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_role_permissions (role_id, perm_id) "
                "VALUES (?, ?)"
            )
        );

        insert_stmt->setInt64(1, role_id);
        insert_stmt->setInt64(2, perm_id);

        return insert_stmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {

        setLastError(e.what());

        return false;
    }
}

// ======================================================
// userRoleBindingExists
// 作用：判断用户是否已经拥有该角色
// ======================================================
bool AdminDAO::userRoleBindingExists(const std::string& app_code, const std::string& user_id, const std::string& role_key) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT 1 "
                "FROM ny_user_roles ur "
                "JOIN ny_roles r ON r.id = ur.role_id "
                "JOIN ny_apps a ON a.id = ur.app_id AND a.id = r.app_id "
                "WHERE a.app_code = ? "
                "  AND ur.app_user_id = ? "
                "  AND r.role_key = ? "
                "LIMIT 1 "
            )
        );

        stmt->setString(1, app_code);
        stmt->setString(2, user_id);
        stmt->setString(3, role_key);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        return rs->next();
    } catch (const sql::SQLException& e) {

        setLastError(e.what());

        return false;
    }
}

// ======================================================
// grantRoleToUser
// 作用：给用户授角色
// ======================================================
bool AdminDAO::grantRoleToUser(const std::string& app_code, const std::string& user_id, const std::string& role_key, const std::string& granted_by) {
    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> app_stmt(
            conn->prepareStatement(
                "SELECT id FROM ny_apps WHERE app_code = ? LIMIT 1"
            )
        );

        app_stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> app_rs(app_stmt->executeQuery());

        if(!app_rs->next()) {
            return false;
        }

        int64_t app_id = app_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> role_stmt(
            conn->prepareStatement(
                "SELECT r.id "
                "FROM ny_roles r "
                "WHERE r.app_id = ? "
                "  AND r.role_key = ? "
                "LIMIT 1"
            )
        );

        role_stmt->setInt64(1, app_id);
        role_stmt->setString(2, role_key);

        std::unique_ptr<sql::ResultSet> role_rs(role_stmt->executeQuery());

        if(!role_rs->next()) {
            return false;
        }

        int64_t role_id = role_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> insert_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_user_roles (app_id, app_user_id, role_id ,granted_by) "
                "VALUES (?, ?, ?, ?)"
            )
        );

        insert_stmt->setInt64(1, app_id);
        insert_stmt->setString(2, user_id);
        insert_stmt->setInt64(3, role_id);
        insert_stmt->setString(4, granted_by);

        return insert_stmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        
        setLastError(e.what());

        return false;
    }
}

// ======================================================
// getResourceOwner
// 作用：查询某个资源当前 owner
// ======================================================
std::optional<std::string> AdminDAO::getResourceOwner(const std::string& app_code, const std::string& resource_type, const std::string& resource_id) {
    try {
        
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT r.owner_user_id "
                "FROM ny_resources r "
                "JOIN ny_apps a ON a.id = r.app_id "
                "WHERE a.app_code = ? "
                "  AND r.resource_type = ? "
                "  AND r.resource_id = ? "
                "LIMIT 1 "
            )
        );

        stmt->setString(1, app_code);
        stmt->setString(2, resource_type);
        stmt->setString(3, resource_id);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        if(!rs->next()){

            return std::nullopt;
        }

        return rs->getString("owner_user_id");
    } catch (const sql::SQLException& e) {

        setLastError(e.what());

        return std::nullopt;
    }
}

// ======================================================
// upsertResourceOwner
// 作用：设置资源 owner
// 规则：
//   - 资源存在 -> 更新 owner / resource_name / metadata_text / status
//   - 资源不存在 -> 新插入
// ======================================================
bool AdminDAO::upsertResourceOwner(const std::string& app_code, const std::string& resource_type, const std::string& resource_id, const std::string& resource_name, const std::string& owner_user_id, const std::string& metadata_text) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> app_stmt(
            conn->prepareStatement(
                "SELECT id FROM ny_apps WHERE app_code = ? LIMIT 1"
            )
        );

        app_stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> app_rs(app_stmt->executeQuery());

        if(!app_rs->next()) {

            return false;
        }

        int64_t app_id = app_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> find_stmt(
            conn->prepareStatement(
                "SELECT id "
                "FROM ny_resources "
                "WHERE app_id = ? "
                "  AND resource_type = ? "
                "  AND resource_id = ? "
                "LIMIT 1"
            )
        );

        find_stmt->setInt64(1, app_id);
        find_stmt->setString(2, resource_type);
        find_stmt->setString(3, resource_id);

        std::unique_ptr<sql::ResultSet> find_rs(find_stmt->executeQuery());

        if(find_rs->next()) {

            int64_t resource_pk = find_rs->getInt64("id");

            std::unique_ptr<sql::PreparedStatement> update_stmt(
                conn->prepareStatement(
                    "UPDATE ny_resources "
                    "SET owner_user_id = ?, "
                    "   resource_name = ?, "
                    "   metadata_text = ?, "
                    "   status = 1 "
                    "WHERE id = ? "
                )
            );

            update_stmt->setString(1, owner_user_id);
            update_stmt->setString(2, resource_name);
            update_stmt->setString(3, metadata_text);
            update_stmt->setInt64(4, resource_pk);

            return update_stmt->executeUpdate() > 0;
        }

        std::unique_ptr<sql::PreparedStatement> insert_stmt(
            conn->prepareStatement(
                "INSERT INTO ny_resources ("
                "  app_id, resource_type, resource_id, resource_name, "
                "  owner_user_id, metadata_text, status "
                ") VALUES (?, ?, ?, ?, ?, ?, 1)"
            )
        );

        insert_stmt->setInt64(1, app_id);
        insert_stmt->setString(2, resource_type);
        insert_stmt->setString(3, resource_id);
        insert_stmt->setString(4, resource_name);
        insert_stmt->setString(5, owner_user_id);
        insert_stmt->setString(6, metadata_text);

        return insert_stmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        
        setLastError(e.what());

        return false;
    }
}

// ======================================================
// getCurrentPolicyVersion
// 作用：查询当前策略版本号
// 如果查不到，默认返回 1
// ======================================================
int AdminDAO::getCurrentPolicyVersion(const std::string& app_code) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT pv.current_version "
                "FROM ny_policy_versions pv "
                "JOIN ny_apps a ON a.id = pv.app_id "
                "WHERE a.app_code = ? "
                "LIMIT 1 "
            )
        );

        stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        
        if(!rs->next()) {

            return 1;
        }

        return rs->getInt("current_version");
    } catch (const sql::SQLException& e) {

        setLastError(e.what());

        return 1;
    }
}

// ======================================================
// publishPolicy
// 作用：发布新策略，版本号 +1
// 返回：
//   成功 -> 新版本号
//   失败 -> 0
// ======================================================
int AdminDAO::publishPolicy(const std::string& app_code, const std::string& published_by, const std::string& publish_note) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> app_stmt(
            conn->prepareStatement(
                "SELECT id FROM ny_apps WHERE app_code = ? LIMIT 1"
            )
        );

        app_stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> app_rs(app_stmt->executeQuery());

        if(!app_rs->next()) {

            return 0;
        }

        int64_t app_id = app_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> query_stmt(
            conn->prepareStatement(
                "SELECT current_version "
                "FROM ny_policy_versions "
                "WHERE app_id = ? "
                "LIMIT 1 "
            )
        );

        query_stmt->setInt64(1, app_id);

        std::unique_ptr<sql::ResultSet> version_rs(query_stmt->executeQuery());

        if(version_rs->next()) {

            int new_version = version_rs->getInt("current_version") + 1;

            std::unique_ptr<sql::PreparedStatement> update_stmt(
                conn->prepareStatement(
                    "UPDATE ny_policy_versions "
                    "SET current_version = ?, "
                    "    published_by = ?, "
                    "    publish_note = ?, "
                    "    updated_at = CURRENT_TIMESTAMP "
                    "WHERE app_id = ? "
                )
            );

            update_stmt->setInt(1, new_version);
            update_stmt->setString(2, published_by);
            update_stmt->setString(3, publish_note);
            update_stmt->setInt64(4, app_id);

            if(update_stmt->executeUpdate() <= 0) {
                
                return 0;
            }

            return new_version;
        }
            std::unique_ptr<sql::PreparedStatement> insert_stmt(
                conn->prepareStatement(
                    "INSERT INTO ny_policy_versions ("
                    "  app_id, current_version, published_by, publish_note "
                    ") VALUES (?, 1, ?, ?)"
                )
            );

            insert_stmt->setInt64(1, app_id);
            insert_stmt->setString(2, published_by);
            insert_stmt->setString(3, publish_note);

            if(insert_stmt->executeUpdate() <= 0) {

                return 0;
            }

            return 1;
        } catch (const sql::SQLException& e) {

            setLastError(e.what());

            return 0;
    }
}

// ======================================================
// setPolicyVersion
// 作用：把策略版本恢复或设置到指定版本
// 用途：
// - 发布策略后快照构建失败时，回滚刚递增的版本号
// - 管理修复工具显式设置版本
// ======================================================
bool AdminDAO::setPolicyVersion(const std::string& app_code,
                                int policy_version,
                                const std::string& published_by,
                                const std::string& publish_note) {
    if (policy_version <= 0) {
        setLastError("policy_version must be positive");
        return false;
    }

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> app_stmt(
            conn->prepareStatement(
                "SELECT id FROM ny_apps WHERE app_code = ? LIMIT 1"
            )
        );

        app_stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> app_rs(app_stmt->executeQuery());
        if (!app_rs->next()) {
            return false;
        }

        const int64_t app_id = app_rs->getInt64("id");

        std::unique_ptr<sql::PreparedStatement> stmt(
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

        stmt->setInt64(1, app_id);
        stmt->setInt(2, policy_version);
        stmt->setString(3, published_by);
        stmt->setString(4, publish_note);

        return stmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        setLastError(e.what());
        return false;
    } catch (const std::exception& e) {
        setLastError(e.what());
        return false;
    }
}

// ======================================================
// insertAuditLog
// 作用：插入一条审计日志
// ======================================================
bool AdminDAO::insertAuditLog(const AuditLogRecord& record) {
    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "INSERT INTO ny_audit_logs ("
                "  operator_user_id, operator_username, operator_display_name, "
                "  app_code, action_type, target_type, target_key, "
                "  before_text, after_text, trace_text "
                ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            )
        );

        stmt->setInt64(1, record.operator_user_id);
        stmt->setString(2, record.operator_username);
        stmt->setString(3, record.operator_display_name);
        stmt->setString(4, record.app_code);
        stmt->setString(5, record.action_type);
        stmt->setString(6, record.target_type);
        stmt->setString(7, record.target_key);
        stmt->setString(8, record.before_text);
        stmt->setString(9, record.after_text);
        stmt->setString(10, record.trace_text);

        return stmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        
        setLastError(e.what());

        return false;
    }
}

// ======================================================
// listAuditLogs
// 作用：按条件查询审计日志
// 说明：
//   - app_code 为空 -> 不按 app 过滤
//   - action_type 为空 -> 不按 action_type 过滤
//   - limit <= 0 -> 默认 20
// ======================================================
std::vector<AuditLogItem> AdminDAO::listAuditLogs(const std::string& app_code, const std::string& action_type, int limit) {

    std::vector<AuditLogItem> items;

    try {

        std::unique_ptr<sql::Connection> conn(createConnection());

        if(limit <= 0) {
            limit = 20;
        }

        std::string sql = "SELECT id, operator_user_id, operator_username, operator_display_name, "
                          "       app_code, action_type, target_type, target_key, "
                          "       before_text, after_text, trace_text, "
                          "       DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
                          "FROM ny_audit_logs "
                          "WHERE 1 = 1 ";
                          
        bool has_app_filter = !app_code.empty();
        bool has_action_filter = !action_type.empty();

        if(has_app_filter) {

            sql += "AND app_code = ? ";
        }

        if(has_action_filter) {

            sql += "AND action_type = ? ";
        }

        sql += "ORDER BY id DESC LIMIT ?";

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(sql)
        );

        int param_index = 1;

        if(has_app_filter) {

            stmt->setString(param_index++, app_code);
        }

        if(has_action_filter) {

            stmt->setString(param_index++, action_type);
        }

        stmt->setInt(param_index++, limit);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        while (rs->next()) {

            AuditLogItem item;

            item.id = rs->getInt64("id");
            item.operator_user_id = rs->getInt64("operator_user_id");
            item.operator_username = rs->getString("operator_username");
            item.operator_display_name = rs->getString("operator_display_name");
            item.app_code = rs->getString("app_code");
            item.action_type = rs->getString("action_type");
            item.target_type = rs->getString("target_type");
            item.target_key = rs->getString("target_key");
            item.before_text = rs->getString("before_text");
            item.after_text = rs->getString("after_text");
            item.trace_text = rs->getString("trace_text");
            item.created_at = rs->getString("created_at");

            items.push_back(item);
        }
    } catch (const sql::SQLException& e) {

        setLastError(e.what());
    }

    return items;
}

// ======================================================
// isConnected
// 作用：检查数据库是否可连通
// ======================================================
bool AdminDAO::isConnected() const {
    try {
        std::unique_ptr<sql::Connection> conn(
            const_cast<AdminDAO*>(this)->createConnection()
        );

        return conn != nullptr;
    } catch (const sql::SQLException& e) {
        
        const_cast<AdminDAO*>(this)->setLastError(e.what());

        return false;
    } catch (const std::exception& e) {

        const_cast<AdminDAO*>(this)->setLastError(e.what());

        return false;
    }
}

// ======================================================
// getLastError
// 作用：获取最近一次数据库错误
// ======================================================
std::string AdminDAO::getLastError() const {

    std::lock_guard<std::mutex> lock(error_mutex_);

    return last_error_;
}
