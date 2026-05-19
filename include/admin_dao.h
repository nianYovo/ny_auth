#ifndef NY_AUTH_ADMIN_DAO_H
#define NY_AUTH_ADMIN_DAO_H

#include <cstdint>

#include <mutex>

#include <optional>

#include <string>

#include <vector>

#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <mysql_driver.h>

// ======================================================
// ConsoleUserInfo
// 作用：表示管理端管理员账号信息
// 对应数据库表：ny_console_users
// ======================================================
struct ConsoleUserInfo {

    int64_t id = 0;

    std::string username;

    std::string password_hash;

    std::string display_name;

    bool enabled = false;

    bool is_super_admin = false;
};

// ======================================================
// AuditLogRecord
// 作用：表示一条管理操作审计日志
// 对应数据库表：ny_audit_logs
// ======================================================
struct AuditLogRecord {

    int64_t operator_user_id = 0;

    std::string operator_username;

    std::string operator_display_name;

    std::string app_code;

    std::string action_type;

    std::string target_type;

    std::string target_key;

    std::string before_text;

    std::string after_text;

    std::string trace_text;
};

// ======================================================
// AuditLogItem
// 作用：查询审计日志时的结果对象
// 比 AuditLogRecord 多了 id 和 created_at
// ======================================================
struct AuditLogItem {

    int64_t id = 0;

    int64_t operator_user_id = 0;

    std::string operator_username;

    std::string operator_display_name;

    std::string app_code;

    std::string action_type;

    std::string target_type;

    std::string target_key;

    std::string before_text;

    std::string after_text;

    std::string trace_text;

    std::string created_at;
};

// ======================================================
// AdminDAO
// 作用：管理端数据库访问层
// 它只负责执行数据库操作，不负责登录逻辑、token 校验或模拟逻辑
// ======================================================
class AdminDAO {
public:

    AdminDAO(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& database);

    
    std::optional<ConsoleUserInfo> getConsoleUserByUsername(const std::string& username);

    bool updateConsoleUserLastLoginAt(int64_t user_id);

    
    int64_t createRole(const std::string& app_code, const std::string& role_key, const std::string& role_name,const std::string& decription, bool is_default);

    bool roleExists(const std::string& app_code, const std::string& role_key);


    int64_t createPermission(const std::string& app_code, const std::string& perm_key, const std::string& perm_name, const std::string& resource_type, bool owner_shortcut_enabled, const std::string& description);

    bool permissionExists(const std::string& app_code, const std::string& perm_key);

    
    bool bindPermissionToRole(const std::string& app_code, const std::string& role_key, const std::string& perm_key);

    bool rolePermissionBindingExists(const std::string& app_code, const std::string& role_key, const std::string& perm_key);


    bool grantRoleToUser(const std::string& app_code, const std::string& user_id, const std::string& role_key, const std::string& granted_by);

    bool userRoleBindingExists(const std::string& app_code, const std::string& user_id, const std::string& role_key);


    bool upsertResourceOwner(const std::string& app_code, const std::string& resource_type, const std::string& resource_id, const std::string& resource_name, const std::string& owner_user_id, const std::string& metadata_text);

    std::optional<std::string> getResourceOwner(const std::string& app_code, const std::string& resource_type, const std::string& resource_id);


    int getCurrentPolicyVersion(const std::string& app_code);

    int publishPolicy(const std::string& app_code, const std::string& published_by, const std::string& publish_note);

    bool setPolicyVersion(const std::string& app_code, int policy_version, const std::string& published_by, const std::string& publish_note);

    bool insertAuditLog(const AuditLogRecord& record);

    std::vector<AuditLogItem> listAuditLogs(const std::string& app_code, const std::string& action_type, int limit);


    bool isConnected() const;

    std::string getLastError() const;

private:

    sql::Connection* createConnection();

    void setLastError(const std::string& error);

private:

    std::string host_;

    int port_;

    std::string user_;

    std::string password_;

    std::string database_;

    mutable std::mutex error_mutex_;

    std::string last_error_;
};

#endif
