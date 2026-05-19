#include "admin_manager.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace {

std::string BytesToHex(const unsigned char* data, std::size_t size) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH] = {0};
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    return BytesToHex(digest, SHA256_DIGEST_LENGTH);
}

bool ConstantTimeEquals(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return diff == 0;
}

bool VerifyPassword(const std::string& password, const std::string& stored_hash) {
    const std::string pbkdf2_prefix = "pbkdf2_sha256$";
    if (stored_hash.compare(0, pbkdf2_prefix.size(), pbkdf2_prefix) == 0) {
        const std::size_t iter_start = pbkdf2_prefix.size();
        const std::size_t salt_sep = stored_hash.find('$', iter_start);
        if (salt_sep == std::string::npos || salt_sep + 1 >= stored_hash.size()) {
            return false;
        }

        const std::size_t hash_sep = stored_hash.find('$', salt_sep + 1);
        if (hash_sep == std::string::npos || hash_sep + 1 >= stored_hash.size()) {
            return false;
        }

        int iterations = 0;
        try {
            iterations = std::stoi(stored_hash.substr(iter_start, salt_sep - iter_start));
        } catch (...) {
            return false;
        }

        if (iterations < 10000) {
            return false;
        }

        const std::string salt = stored_hash.substr(salt_sep + 1, hash_sep - salt_sep - 1);
        const std::string expected_hash = stored_hash.substr(hash_sep + 1);

        unsigned char derived[32] = {0};
        if (PKCS5_PBKDF2_HMAC(
                password.c_str(),
                static_cast<int>(password.size()),
                reinterpret_cast<const unsigned char*>(salt.data()),
                static_cast<int>(salt.size()),
                iterations,
                EVP_sha256(),
                sizeof(derived),
                derived) != 1) {
            return false;
        }

        return ConstantTimeEquals(BytesToHex(derived, sizeof(derived)), expected_hash);
    }

    const std::string prefix = "sha256$";
    if (stored_hash.compare(0, prefix.size(), prefix) == 0) {
        const std::size_t salt_start = prefix.size();
        const std::size_t hash_sep = stored_hash.find('$', salt_start);
        if (hash_sep == std::string::npos || hash_sep + 1 >= stored_hash.size()) {
            return false;
        }

        const std::string salt = stored_hash.substr(salt_start, hash_sep - salt_start);
        const std::string expected_hash = stored_hash.substr(hash_sep + 1);
        const std::string actual_hash = Sha256Hex(salt + password);
        return ConstantTimeEquals(actual_hash, expected_hash);
    }

    // Backward compatibility for old local databases initialized with plaintext.
    return ConstantTimeEquals(password, stored_hash);
}

}  // namespace

// ======================================================
// 构造函数
// 作用：保存管理端 DAO、模拟引擎、会话缓存和 session TTL
// ======================================================
AdminManager::AdminManager(std::shared_ptr<AdminDAO> admin_dao, std::shared_ptr<SimulationEngine> simulation_engine, std::shared_ptr<SessionCache> session_cache, int session_ttl_seconds) : admin_dao_(std::move(admin_dao)), simulation_engine_(std::move(simulation_engine)), session_cache_(std::move(session_cache)), session_ttl_seconds_(session_ttl_seconds > 0 ? session_ttl_seconds : 3600) {}

AdminManager::AdminManager(std::shared_ptr<AdminDAO> admin_dao, std::shared_ptr<SimulationEngine> simulation_engine, std::shared_ptr<SessionCache> session_cache, int session_ttl_seconds, std::shared_ptr<SnapshotBuilder> snapshot_builder) : admin_dao_(std::move(admin_dao)), simulation_engine_(std::move(simulation_engine)), snapshot_builder_(std::move(snapshot_builder)), session_cache_(std::move(session_cache)), session_ttl_seconds_(session_ttl_seconds > 0 ? session_ttl_seconds : 3600) {}

// ======================================================
// Login
// 作用：管理员登录
//
// 当前登录逻辑：
// 1. 校验用户名/密码非空
// 2. 查管理员账号
// 3. 检查账号状态
// 4. 校验密码哈希
// 5. 生成 token
// 6. 写入 session cache
// 7. 更新最近登录时间
// ======================================================
ManagerLoginResult AdminManager::Login(const ManagerLoginRequest& request) {

    ManagerLoginResult result;

    if(!admin_dao_ || !session_cache_) {
        
        result.status.success = false;
        result.status.message = "系统内部错误：登录依赖未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    if(request.username.empty()) {
        
        result.status.success = false;
        result.status.message = "参数不完整：username 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    if(request.password.empty()) {
        
        result.status.success = false;
        result.status.message = "参数不完整：password 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    auto console_user_opt = admin_dao_->getConsoleUserByUsername(request.username);

    if(!console_user_opt.has_value()) {
        
        result.status.success = false;
        result.status.message = "用户名或密码错误";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const ConsoleUserInfo console_user = console_user_opt.value();

    if(!console_user.enabled) {
        
        result.status.success = false;
        result.status.message = "管理员账号已被禁用";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    if(!VerifyPassword(request.password, console_user.password_hash)) {

        result.status.success = false;
        result.status.message = "用户名或密码错误";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const std::string token = generateToken(console_user);

    const AdminSessionInfo session_info = buildSession(console_user, token);

    const std::string session_cache_key = BuildAdminSessionCacheKey(token);

    session_cache_->Put(session_cache_key, session_info, session_ttl_seconds_);

    admin_dao_->updateConsoleUserLastLoginAt(console_user.id);

            
    result.status.success = true;
    result.status.message = "登录成功";
    result.status.error_code = "OK";
    result.session = session_info;
    result.expires_in_seconds = session_ttl_seconds_;

    return result;
}

// ======================================================
// CreateRole
// 作用：创建角色
// 流程：
// 1. 校验 token
// 2. 校验管理权限
// 3. 校验参数
// 4. 判断角色是否已存在
// 5. 创建角色
// 6. 写审计日志
// ======================================================
ManagerCreateRoleResult AdminManager::CreateRole(const ManagerCreateRoleRequest& request) {

    ManagerCreateRoleResult result;

    if(!admin_dao_) {

        result.status.success = false;
        result.status.message = "系统内部错误：AdminDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    if(request.app_code.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：app_code 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    if(request.role_key.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：role_key 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    if(request.role_name.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：role_name 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    if(admin_dao_->roleExists(request.app_code, request.role_key)) {

        result.status.success = false;
        result.status.message = "角色已存在";
        result.status.error_code = "ALREADY_EXISTS";

        return result;
    }

    const int64_t role_id = admin_dao_->createRole(request.app_code, request.role_key, request.role_name, request.description, request.is_default);

    if(role_id == 0) {

        result.status.success = false;
        result.status.message = "创建角色失败";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    result.status.success = true;
    result.status.message = "创建角色成功";
    result.status.error_code = "OK";
    result.role_id = role_id;

    std::ostringstream after_text;
    after_text << "role_key = " <<request.role_key
               << ", role_name = " << request.role_name
               << ", is_default = " << (request.is_default ? 1 : 0)
               << ", description = " <<request.description;
    
    writeAuditLog(operator_identity, request.app_code, "CREATE_ROLE", "role", request.role_key, "", after_text.str(), "created role under app " + request.app_code);

    return result;
}

// ======================================================
// CreatePermission
// 作用：创建权限
// ======================================================
ManagerCreatePermissionResult AdminManager::CreatePermission(const ManagerCreatePermissionRequest& request) {

    ManagerCreatePermissionResult result;

    if(!admin_dao_) {

        result.status.success = false;
        result.status.message = "系统内部错误：AdminDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    if(request.app_code.empty() || request.perm_key.empty() || request.perm_name.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：app_code / perm_key / perm_name 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    if(admin_dao_->permissionExists(request.app_code, request.perm_key)) {

        result.status.success = false;
        result.status.message = "权限已存在";
        result.status.error_code = "ALREADY_EXISTS";

        return result;
    }

    const int64_t permission_id = admin_dao_->createPermission(request.app_code, request.perm_key, request.perm_name, request.resource_type, request.owner_shortcut_enabled, request.description);

    if(permission_id == 0) {

        result.status.success = false;
        result.status.message = "创建权限失败";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    result.status.success = true;
    result.status.message = "创建权限成功";
    result.status.error_code = "OK";
    result.permission_id = permission_id;

    std::ostringstream after_text;
    
    after_text << "perm_key = " << request.perm_key
               << ", perm_name = " << request.perm_name
               << ", resource_type = " << request.resource_type
               << ", owner_shortcut_enabled = " << (request.owner_shortcut_enabled ? 1 : 0)
               << ", description = " << request.description;

    writeAuditLog(operator_identity, request.app_code, "CREATE_PERMISSION", "permission", request.perm_key, "", after_text.str(), "created permission under app " + request.app_code);

    return result;
}

// ======================================================
// BindPermissionToRole
// 作用：给角色绑定权限
// ======================================================
ManagerBindPermissionToRoleResult AdminManager::BindPermissionToRole(const ManagerBindPermissionToRoleRequest& request) {
    
    ManagerBindPermissionToRoleResult result;

    if(!admin_dao_) {

        result.status.success = false;
        result.status.message = "系统内部错误：AdminDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    if(request.app_code.empty() || request.role_key.empty() || request.perm_key.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：app_code / role_key / perm_key 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    if(!admin_dao_->roleExists(request.app_code, request.role_key)) {

        result.status.success = false;
        result.status.message = "角色不存在";
        result.status.error_code = "NOT_FOUND";

        return result;
    }

    if(!admin_dao_->permissionExists(request.app_code, request.perm_key)) {

        result.status.success = false;
        result.status.message = "权限不存在";
        result.status.error_code = "NOT_FOUND";

        return result;
    }

    if(admin_dao_->rolePermissionBindingExists(request.app_code, request.role_key, request.perm_key)) {

        result.status.success = false;
        result.status.message = "角色与权限已绑定";
        result.status.error_code = "CONFLICT";

        return result;
    }

    if(!admin_dao_->bindPermissionToRole(request.app_code, request.role_key, request.perm_key)) {

        result.status.success = false;
        result.status.message = "绑定角色权限失败";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    result.status.success = true;
    result.status.message = "绑定角色权限成功";
    result.status.error_code = "OK";

    writeAuditLog(operator_identity, request.app_code, "BIND_PERMISSION_TO_ROLE", "role_permission", request.role_key + "->" + request.perm_key, "", "role_key = " + request.role_key + ", perm_key = " + request.perm_key, "bound permission to role");

    return result;
}

// ======================================================
// GrantRoleToUser
// 作用：给用户授角色
// ======================================================
ManagerGrantRoleToUserResult AdminManager::GrantRoleToUser(const ManagerGrantRoleToUserRequest& request) {
    
    ManagerGrantRoleToUserResult result;

    if(!admin_dao_) {

        result.status.success = false;
        result.status.message = "系统内部错误：AdminDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    if(request.app_code.empty() || request.user_id.empty() || request.role_key.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：app_code / user_id / role_key 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    if(!admin_dao_->roleExists(request.app_code, request.role_key)) {

        result.status.success = false;
        result.status.message = "角色不存在";
        result.status.error_code = "NOT_FOUND";

        return result;
    }

    if(admin_dao_->userRoleBindingExists(request.app_code, request.user_id, request.role_key)) {

        result.status.success = false;
        result.status.message = "用户已拥有该角色";
        result.status.error_code = "CONFLICT";

        return result;
    }

    if(!admin_dao_->grantRoleToUser(request.app_code, request.user_id, request.role_key, operator_identity.username)) {

        result.status.success = false;
        result.status.message = "授予角色失败";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    result.status.success = true;
    result.status.message = "授予角色成功";
    result.status.error_code = "OK";

    writeAuditLog(operator_identity, request.app_code, "GRANT_ROLE_TO_USER", "user_role", request.user_id, "", "user_id = " + request.user_id + ", role_key = " + request.role_key, "granted role to user");

    return result;
}

// ======================================================
// SetResourceOwner
// 作用：设置资源 owner
// ======================================================
ManagerSetResourceOwnerResult AdminManager::SetResourceOwner(const ManagerSetResourceOwnerRequest& request) {
    
    ManagerSetResourceOwnerResult result;

    if(!admin_dao_) {

        result.status.success = false;
        result.status.message = "系统内部错误：AdminDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    if(request.app_code.empty() || request.resource_type.empty() || request.resource_id.empty() || request.owner_user_id.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：app_code / resource_type / resource_id / owner_user_id 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    const auto before_owner_opt = admin_dao_->getResourceOwner(request.app_code, request.resource_type, request.resource_id);

    std::string before_text;

    if(before_owner_opt.has_value()) {

        before_text = "owner_user_id = " + before_owner_opt.value();
    }

    if(!admin_dao_->upsertResourceOwner(request.app_code, request.resource_type, request.resource_id, request.resource_name, request.owner_user_id, request.metadata_text)) {

        result.status.success = false;
        result.status.message = "设置资源 owner 失败";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    result.status.success = true;
    result.status.message = "设置资源 owner 成功";
    result.status.error_code = "OK";

    std::ostringstream after_text;

    after_text << "resource_type = " << request.resource_type
               << ", resource_id = " << request.resource_id
               << ", owner_user_id = " << request.owner_user_id
               << ", resource_name = " << request.resource_name
               << ", metadata_text = " <<request.metadata_text;

    writeAuditLog(operator_identity, request.app_code, "SET_RESOURCE_OWNER", "resource", request.resource_type + "/" + request.resource_id, before_text, after_text.str(), "upserted resource owner");

    return result;
}

// ======================================================
// PublishPolicy
// 作用：发布策略（版本号 +1）
// ======================================================
ManagerPublishPolicyResult AdminManager::PublishPolicy(const ManagerPublishPolicyRequest& request) {

    ManagerPublishPolicyResult result;

    if(!admin_dao_) {

        result.status.success = false;
        result.status.message = "系统内部错误：AdminDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    if(request.app_code.empty()) {

        result.status.success = false;
        result.status.message = "参数不完整：app_code 不能为空";
        result.status.error_code = "INVALID_ARGUMENT";

        return result;
    }

    const int old_version = admin_dao_->getCurrentPolicyVersion(request.app_code);

    const int new_version = admin_dao_->publishPolicy(request.app_code, operator_identity.username, request.publish_note);

    if(new_version <= 0) {

        result.status.success = false;
        result.status.message = "发布策略失败";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    if(snapshot_builder_) {
        const SnapshotBuildResult snapshot_result =
            snapshot_builder_->BuildAndStoreSnapshot(
                request.app_code,
                operator_identity.username,
                request.publish_note
            );

        if(!snapshot_result.status.success) {
            const bool rollback_ok = admin_dao_->setPolicyVersion(
                request.app_code,
                old_version,
                operator_identity.username,
                "rollback after snapshot failure: " + request.publish_note
            );

            result.status.success = false;
            result.status.message = rollback_ok
                ? "构建快照失败，策略版本已回滚：" + snapshot_result.status.message
                : "构建快照失败，且策略版本回滚失败：" + snapshot_result.status.message;
            result.status.error_code = snapshot_result.status.error_code.empty() ? "INTERNAL_ERROR" : snapshot_result.status.error_code;
            result.new_policy_version = rollback_ok ? old_version : new_version;

            std::ostringstream before_text;
            before_text << "policy_version = " << old_version;

            std::ostringstream after_text;
            after_text << "policy_version = " << new_version
                       << ", publish_note = " << request.publish_note
                       << ", snapshot_error = " << snapshot_result.status.message
                       << ", rollback_ok = " << (rollback_ok ? 1 : 0);

            writeAuditLog(operator_identity, request.app_code, "PUBLISH_POLICY_SNAPSHOT_FAILED", "policy", request.app_code, before_text.str(), after_text.str(), "published policy version but failed to build snapshot");

            return result;
        }
    }

    result.status.success = true;
    result.status.message = "发布策略成功";
    result.status.error_code = "OK";
    result.new_policy_version = new_version;

    std::ostringstream before_text;
    before_text << "policy_version = " << old_version;

    std::ostringstream after_text;
    after_text << "policy_version = " << new_version << ", publish_note = " <<request.publish_note;

    writeAuditLog(operator_identity, request.app_code, "PUBLISH_POLICY", "policy", request.app_code, before_text.str(), after_text.str(), "published new policy version");

    return result;
}

// ======================================================
// SimulateCheck
// 作用：执行模拟检查
// 说明：
// - status 表示“管理操作是否成功执行”
// - simulation_result 表示“模拟判断结果是什么”
//
// 如果 token 无效 / 参数明显不对 / 依赖未初始化，
// status 会失败。
// 如果模拟顺利执行，哪怕最后 allowed = false，
// status 仍然可以是 success = true。
// ======================================================
ManagerSimulateCheckResult AdminManager::SimulateCheck(const ManagerSimulateCheckRequest& request) {

    ManagerSimulateCheckResult result;

    if(!simulation_engine_) {

        result.status.success = false;
        result.status.message = "系统内部错误：SimulationEngine 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    result.simulation_result = simulation_engine_->Simulate(request.simulation_request);

    const std::string& deny_code = result.simulation_result.deny_code;

    if(deny_code == "INVALID_ARGUMENT" || deny_code == "APP_NOT_FOUND" || deny_code == "APP_DISABLED" || deny_code == "PERMISSION_NOT_FOUND" || deny_code == "RESOURCE_NOT_FOUND" || deny_code == "INTERNAL_ERROR" || deny_code == "NOT_FOUND") {

        result.status.success =false;
        result.status.message = result.simulation_result.reason.empty() ? "模拟检查失败" : result.simulation_result.reason;
        result.status.error_code = deny_code.empty() ? "INTERNAL_ERROR" : deny_code;

        return result;
    }

    result.status.success = true;
    result.status.message = "模拟检查完成";
    result.status.error_code = "OK";

    return result;
}

// ======================================================
// ListAuditLogs
// 作用：查询审计日志
// ======================================================
ManagerListAuditLogsResult AdminManager::ListAuditLogs(const ManagerListAuditLogsRequest& request) {

    ManagerListAuditLogsResult result;

    if(!admin_dao_){

        result.status.success = false;
        result.status.message = "系统内部错误：AdminDAO 未初始化";
        result.status.error_code = "INTERNAL_ERROR";

        return result;
    }

    auto operator_opt = authenticateOperator(request.operator_token);

    if(!operator_opt.has_value()) {

        result.status.success = false;
        result.status.message = "登录状态无效或已过期";
        result.status.error_code = "UNAUTHORIZED";

        return result;
    }

    const OperatorIdentity operator_identity = operator_opt.value();

    if(!authorizeOperator(operator_identity)) {

        result.status.success = false;
        result.status.message = "没有权限执行该管理操作";
        result.status.error_code = "FORBIDDEN";

        return result;
    }

    int limit = request.limit;
    if(limit <= 0) {

        limit =20;
    }

    result.items = admin_dao_->listAuditLogs(request.app_code, request.action_type, limit);

    result.status.success = true;
    result.status.message = "查询审计日志成功";
    result.status.error_code = "OK";

    return result;
}

// ======================================================
// authenticateOperator
// 作用：校验 operator_token，并返回操作者身份
//
// 当前认证逻辑：
// 1. token 不能为空
// 2. 必须有 session cache
// 3. 从缓存中查 AdminSessionInfo
// 4. 转成 OperatorIdentity
// ======================================================
std::optional<OperatorIdentity> AdminManager::authenticateOperator(const std::string& operator_token) {

    if(operator_token.empty()) {

        return std::nullopt;
    }

    if(!session_cache_) {

        return std::nullopt;
    }

    const std::string session_cache_key = BuildAdminSessionCacheKey(operator_token);

    AdminSessionInfo session_info;

    if(!session_cache_->Get(session_cache_key, session_info)) {

        return std::nullopt;
    }

    if(session_info.token != operator_token) {

        return std::nullopt;
    }

    return buildOperatorIdentity(session_info);
}

// ======================================================
// authorizeOperator
// 作用：校验操作者是否允许执行当前管理操作
//
// 当前授权策略：
// authenticated 且 is_super_admin = true 才允许
// ======================================================
bool AdminManager::authorizeOperator(const OperatorIdentity& operator_identity) const {

    if(!operator_identity.authenticated) {

        return false;
    }

    if(!operator_identity.is_super_admin) {

        return false;
    }

    return true;
}

// ======================================================
// generateToken
// 作用：生成管理员 token
// ======================================================
std::string AdminManager::generateToken(const ConsoleUserInfo& console_user) const {

    (void)console_user;

    unsigned char bytes[32] = {0};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        std::random_device rd;
        for (unsigned char& byte : bytes) {
            byte = static_cast<unsigned char>(rd() & 0xFF);
        }
    }

    return BytesToHex(bytes, sizeof(bytes));
}

// ======================================================
// buildSession
// 作用：把 ConsoleUserInfo + token 组装成 AdminSessionInfo
// ======================================================
AdminSessionInfo AdminManager::buildSession(const ConsoleUserInfo& console_user, const std::string& token) const {

    AdminSessionInfo session_info;

    session_info.user_id = console_user.id;
    session_info.username = console_user.username;
    session_info.display_name = console_user.display_name;
    session_info.is_super_admin = console_user.is_super_admin;
    session_info.token = token;
    session_info.login_at_unix_seconds = nowUnixSeconds();

    return session_info;
}

// ======================================================
// buildOperatorIdentity
// 作用：把缓存中的 session 转成本次请求的操作者身份
// ======================================================
OperatorIdentity AdminManager::buildOperatorIdentity(const AdminSessionInfo& session_info) const {

    OperatorIdentity identity;

    identity.authenticated = true;
    identity.user_id = session_info.user_id;
    identity.username = session_info.username;
    identity.display_name = session_info.display_name;
    identity.is_super_admin = session_info.is_super_admin;
    identity.token = session_info.token;

    return identity;
}

// ======================================================
// writeAuditLog
// 作用：写一条审计日志
//
// 注意：
// - 这是辅助方法
// - 写审计失败不应该影响主操作成功与否
// ======================================================
bool AdminManager::writeAuditLog(const OperatorIdentity& operator_identity, const std::string& app_code, const std::string& action_type, const std::string& target_type, const std::string& target_key, const std::string& before_text, const std::string& after_text, const std::string& trace_text) {

    if(!admin_dao_) {

        return false;
    }

    AuditLogRecord record;

    record.operator_user_id = operator_identity.user_id;
    record.operator_username = operator_identity.username;
    record.operator_display_name = operator_identity.display_name;
    record.app_code = app_code;
    record.action_type = action_type;
    record.target_type = target_type;
    record.target_key = target_key;
    record.before_text = before_text;
    record.after_text = after_text;
    record.trace_text = trace_text;

    return admin_dao_->insertAuditLog(record);
}

// ======================================================
// nowUnixSeconds
// 作用：获取当前 Unix 秒级时间戳
// ======================================================
int64_t AdminManager::nowUnixSeconds() const {

    const auto now = std::chrono::system_clock::now();

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());

    return static_cast<int64_t>(seconds.count());
}
