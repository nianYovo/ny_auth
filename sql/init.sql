-- =========================================================
-- ny_auth 数据库初始化脚本（新版）
-- 目标：
-- 1. 支持基础 RBAC：用户 -> 角色 -> 权限
-- 2. 支持资源 owner 快捷规则
-- 3. 支持策略版本号
-- 4. 支持记录每次权限决策的日志、管理端审计日志和策略快照
-- =========================================================

-- 如果数据库不存在，就创建数据库 ny_auth
CREATE DATABASE IF NOT EXISTS ny_auth DEFAULT CHARSET utf8mb4;

-- 切换到 ny_auth 数据库
USE ny_auth;

-- =========================================================
-- 为了方便反复调试，这里先按“从依赖最弱到最强”的顺序删表
-- 注意：这会删除旧数据
-- =========================================================
DROP TABLE IF EXISTS ny_decision_logs;
DROP TABLE IF EXISTS ny_snapshot_publish_logs;
DROP TABLE IF EXISTS ny_policy_snapshots;
DROP TABLE IF EXISTS ny_audit_logs;
DROP TABLE IF EXISTS ny_console_users;
DROP TABLE IF EXISTS ny_resources;
DROP TABLE IF EXISTS ny_user_roles;
DROP TABLE IF EXISTS ny_role_permissions;
DROP TABLE IF EXISTS ny_permissions;
DROP TABLE IF EXISTS ny_roles;
DROP TABLE IF EXISTS ny_policy_versions;
DROP TABLE IF EXISTS ny_apps;

-- =========================================================
-- 1) 应用表 ny_apps
-- 作用：
-- 记录有哪些业务系统接入了 ny_auth
-- 例如：doc_center、course_hub、group_bot
-- =========================================================
CREATE TABLE ny_apps (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_name VARCHAR(64) NOT NULL,

    app_code VARCHAR(64) NOT NULL UNIQUE,

    app_secret VARCHAR(128) NOT NULL,

    description VARCHAR(255) DEFAULT '',

    -- 状态：1表示启用，0表示禁用
    status TINYINT NOT NULL DEFAULT 1,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- =========================================================
-- 2) 策略版本表 ny_policy_versions
-- 作用：
-- 记录每个应用当前生效的策略版本
-- 后续缓存 key 会带上这个版本号，避免粗暴清缓存
-- =========================================================
CREATE TABLE ny_policy_versions (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    -- 当前生效的版本号
    current_version INT NOT NULL DEFAULT 1,

    published_by VARCHAR(64) DEFAULT '',

    publish_note VARCHAR(255) DEFAULT '',

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uk_app_policy_version(app_id)
);

-- =========================================================
-- 3) 角色表 ny_roles
-- 作用：
-- 记录某个应用下有哪些角色
-- 例如：admin、editor、viewer
-- =========================================================
CREATE TABLE ny_roles (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    role_name VARCHAR(64) NOT NULL,

    role_key VARCHAR(64) NOT NULL,

    description VARCHAR(255) DEFAULT '',

    is_default TINYINT NOT NULL DEFAULT 0,

    status TINYINT NOT NULL DEFAULT 1,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uk_app_role(app_id,role_key),

    KEY idx_role_app_id(app_id)
);

-- =========================================================
-- 4) 权限表 ny_permissions
-- 作用：
-- 记录某个应用下有哪些权限
-- 1. resource_type：这个权限作用于哪类资源
-- 2. owner_shortcut_enabled：是否允许资源 owner 快捷通过
-- =========================================================
CREATE TABLE ny_permissions (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    perm_name VARCHAR(64) NOT NULL,

    perm_key VARCHAR(64) NOT NULL,

    resource_type VARCHAR(64) DEFAULT '',

    owner_shortcut_enabled TINYINT NOT NULL DEFAULT 0,

    description VARCHAR(255) DEFAULT '',

    status TINYINT NOT NULL DEFAULT 1,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uk_app_perm(app_id,perm_key),

    KEY idx_perm_app_source(app_id,resource_type)
);

-- =========================================================
-- 5) 角色-权限关联表 ny_role_permissions
-- 作用：
-- 记录某个角色拥有哪些权限
-- 例如：editor -> document:edit
-- =========================================================
CREATE TABLE ny_role_permissions (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    role_id BIGINT NOT NULL,

    perm_id BIGINT NOT NULL,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    UNIQUE KEY uk_role_perm(role_id,perm_id),

    KEY idx_role_perm_perm_id(perm_id)
);

-- =========================================================
-- 6) 用户-角色关联表 ny_user_roles
-- 作用：
-- 记录某个用户被授予了哪些角色
-- 例如：u1001 -> editor
-- =========================================================
CREATE TABLE ny_user_roles (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    app_user_id VARCHAR(128) NOT NULL,

    role_id BIGINT NOT NULL,

    granted_by VARCHAR(64) DEFAULT '',

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uk_app_user_role(app_id,app_user_id,role_id),

    KEY idx_user_role_lookup(app_id,app_user_id),

    KEY idx_user_role_role_id(role_id)
);

-- =========================================================
-- 7) 资源表 ny_resources
-- 作用：
-- 记录系统中的资源，以及这个资源归谁所有
-- 这是 owner 快捷规则的数据基础
-- 例如：doc_001 这篇文档属于 u1001
-- =========================================================
CREATE TABLE ny_resources (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    resource_type VARCHAR(64) NOT NULL,

    resource_id VARCHAR(128) NOT NULL,

    resource_name VARCHAR(128) DEFAULT '',

    owner_user_id VARCHAR(128) NOT NULL,

    metadata_text TEXT,

    status TINYINT NOT NULL DEFAULT 1,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uk_app_resource(app_id,resource_type,resource_id),

    KEY idx_resource_lookup (app_id,resource_type,resource_id),

    KEY idx_resource_owner_lookup(app_id,owner_user_id)
);

-- =========================================================
-- 8) 决策日志表 ny_decision_logs
-- 作用：
-- 记录每一次权限判断的结果和解释
-- 这不是“管理员改配置”的审计日志，而是“每次鉴权本身”的日志
-- =========================================================
CREATE TABLE ny_decision_logs (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    app_code VARCHAR(64) NOT NULL,

    user_id VARCHAR(128) NOT NULL,

    perm_key VARCHAR(64) NOT NULL,

    resource_type VARCHAR(64) DEFAULT '',

    resource_id VARCHAR(128) DEFAULT '',

    allowed TINYINT NOT NULL DEFAULT 0,

    decision_source VARCHAR(32) NOT NULL DEFAULT 'DB',

    matched_roles VARCHAR(255) DEFAULT '',

    matched_permissions VARCHAR(255) DEFAULT '',

    owner_shortcut_used TINYINT NOT NULL DEFAULT 0,

    policy_version INT NOT NULL DEFAULT 1,

    deny_code VARCHAR(64) DEFAULT '',

    reason VARCHAR(255) DEFAULT '',

    trace_text TEXT,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    KEY idx_decision_app_time(app_id,created_at),
    KEY idx_decision_user_time(user_id,created_at),
    KEY idx_decision_perm_time(perm_key,created_at)
);

-- =========================================================
-- 9) 管理员账号表 ny_console_users
-- 作用：
-- 管理端登录使用，不是业务系统普通用户
-- =========================================================
CREATE TABLE ny_console_users (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    username VARCHAR(64) NOT NULL UNIQUE,

    password_hash VARCHAR(255) NOT NULL,

    display_name VARCHAR(64) DEFAULT '',

    status TINYINT NOT NULL DEFAULT 1,

    is_super_admin TINYINT NOT NULL DEFAULT 1,

    last_login_at DATETIME NULL,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- =========================================================
-- 10) 审计日志表 ny_audit_logs
-- 作用：
-- 记录管理员对权限策略做了什么修改
-- 注意：
-- 这和 ny_decision_logs 不一样
-- - ny_decision_logs 记录“每次鉴权”
-- - ny_audit_logs   记录“每次配置变更”
-- =========================================================
CREATE TABLE ny_audit_logs (

    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    operator_user_id BIGINT NOT NULL,

    operator_username VARCHAR(64) NOT NULL,

    operator_display_name VARCHAR(64) DEFAULT '',

    app_code VARCHAR(64) NOT NULL,

    action_type VARCHAR(64) NOT NULL,

    target_type VARCHAR(64) NOT NULL,

    target_key VARCHAR(128) NOT NULL,

    before_text TEXT,

    after_text TEXT,

    trace_text TEXT,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    KEY idx_audit_app_time (app_code, created_at),
    KEY idx_audit_operator_time (operator_username, created_at),
    KEY idx_audit_action_time (action_type, created_at)
);

-- =========================================================
-- 11) 策略快照表 ny_policy_snapshots
--
-- 作用：
-- 每次发布策略后，把“当时的完整只读策略视图”固化成一份快照。
-- 后面本地 Agent 拉取的就是这里的数据。
-- =========================================================
CREATE TABLE ny_policy_snapshots (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    app_code VARCHAR(64) NOT NULL,

    policy_version INT NOT NULL,

    snapshot_json LONGTEXT NOT NULL,

    status TINYINT NOT NULL DEFAULT 1,

    published_by VARCHAR(64) DEFAULT '',

    publish_note VARCHAR(255) DEFAULT '',

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uk_snapshot_app_version(app_id, policy_version),

    KEY idx_snapshot_app_code_version(app_code, policy_version),
    KEY idx_snapshot_app_status(app_id, status)
);

-- =========================================================
-- 12) 快照发布日志表 ny_snapshot_publish_logs
--
-- 作用：
-- 记录“哪次快照是怎么生成和发布的”
--
-- 注意：
-- 这和 ny_audit_logs 不一样：
-- - ny_audit_logs            记录管理员做了什么配置操作
-- - ny_snapshot_publish_logs 记录快照构建/发布过程本身
-- =========================================================
CREATE TABLE ny_snapshot_publish_logs (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,

    app_id BIGINT NOT NULL,

    app_code VARCHAR(64) NOT NULL,

    policy_version INT NOT NULL,

    snapshot_id BIGINT NOT NULL,

    published_by VARCHAR(64) DEFAULT '',

    publish_result VARCHAR(32) NOT NULL DEFAULT 'SUCCESS',

    trace_text TEXT,

    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

    KEY idx_snapshot_publish_app_version(app_code, policy_version),
    KEY idx_snapshot_publish_result_time(publish_result, created_at)
);

-- =========================================================
-- 下面开始插入测试数据
-- 这部分很重要，因为没有测试数据，你后面没法调 Check 接口
-- =========================================================

-- ---------------------------------------------------------
-- 插入一个测试应用：文档协作平台
-- 这是我们的主测试 app
-- ---------------------------------------------------------
INSERT INTO ny_apps(app_name,app_code,app_secret,description,status)
VALUES('文档协作平台','doc_center','secret_doc_center_123','带owner规则和决策解释的文档平台',1);

SET @app_id = (SELECT id FROM ny_apps WHERE app_code = 'doc_center');

-- ---------------------------------------------------------
-- 初始化策略版本
-- 一开始先从第 1 版开始
-- ---------------------------------------------------------
INSERT INTO ny_policy_versions(app_id,current_version,published_by,publish_note)
VALUES(@app_id, 1, 'system','初始化策略版本');

-- ---------------------------------------------------------
-- 插入角色
-- admin  ：管理员
-- editor ：编辑者
-- viewer ：只读用户
-- ---------------------------------------------------------
INSERT INTO ny_roles(app_id,role_name,role_key,description,is_default,status)
VALUES
(@app_id, '管理员', 'admin', '拥有系统高权限', 0, 1),
(@app_id, '编辑者', 'editor', '可读写文档', 0, 1),
(@app_id, '浏览者', 'viewer', '只能查看文档', 1, 1);

-- 取出角色 id
SET @role_admin = (
    SELECT id FROM ny_roles WHERE app_id = @app_id AND role_key = 'admin'
);
SET @role_editor = (
    SELECT id FROM ny_roles WHERE app_id = @app_id AND role_key = 'editor'
);
SET @role_viewer = (
    SELECT id FROM ny_roles WHERE app_id = @app_id AND role_key = 'viewer'
);


-- ---------------------------------------------------------
-- 插入角色
-- admin  ：管理员
-- editor ：编辑者
-- viewer ：只读用户
-- ---------------------------------------------------------
INSERT INTO ny_permissions(
    app_id,
    perm_name,
    perm_key,
    resource_type,
    owner_shortcut_enabled,
    description,
    status
)
VALUES
(@app_id, '查看文档', 'document:read', 'document', 1, '允许读取文档, owner 可快捷通过', 1),
(@app_id, '编辑文档', 'document:edit', 'document', 1, '允许编辑文档, owner 可快捷通过', 1),
(@app_id, '发布文档', 'document:publish', 'document', 0, '允许发布文档, 但owner 不能直接快捷通过', 1),
(@app_id, '删除文档', 'document:delete', 'document', 0, '允许删除文档, 必须依赖角色权限', 1),
(@app_id, '授予角色', 'admin:grant_role', 'system', 0, '管理类权限，绝不能通过 owner 快捷放行', 1);

-- 取出权限 id
SET @perm_read = (
    SELECT id FROM ny_permissions WHERE app_id = @app_id AND perm_key = 'document:read'
);
SET @perm_edit = (
    SELECT id FROM ny_permissions WHERE app_id = @app_id AND perm_key = 'document:edit'
);
SET @perm_publish = (
    SELECT id FROM ny_permissions WHERE app_id = @app_id AND perm_key = 'document:publish'
);
SET @perm_delete = (
    SELECT id FROM ny_permissions WHERE app_id = @app_id AND perm_key = 'document:delete'
);
SET @perm_grant_role = (
    SELECT id FROM ny_permissions WHERE app_id = @app_id AND perm_key = 'admin:grant_role'
);

-- ---------------------------------------------------------
-- 插入角色
-- admin  ：管理员
-- editor ：编辑者
-- viewer ：只读用户
-- ---------------------------------------------------------
INSERT INTO ny_role_permissions(role_id, perm_id)
VALUES
(@role_admin, @perm_read),
(@role_admin, @perm_edit),
(@role_admin, @perm_publish),
(@role_admin, @perm_delete),
(@role_admin, @perm_grant_role),

(@role_editor, @perm_read),
(@role_editor, @perm_edit),
(@role_editor, @perm_publish),

(@role_viewer, @perm_read);

-- ---------------------------------------------------------
-- 给测试用户分配角色
-- u9000 是管理员
-- u1001 是编辑者
-- u1002 是浏览者
-- 注意：u3001 故意不分配任何角色
-- 因为我们要用它来演示“无角色但作为 owner 仍可通过部分权限”
-- ---------------------------------------------------------
INSERT INTO ny_user_roles(app_id, app_user_id, role_id, granted_by)
VALUES
(@app_id, 'u9000', @role_admin, 'system'),
(@app_id, 'u1001', @role_editor, 'system'),
(@app_id, 'u1002', @role_viewer, 'system');

-- ---------------------------------------------------------
-- 插入资源数据
-- 这些资源后面会用于演示 owner 快捷规则
-- ---------------------------------------------------------
INSERT INTO ny_resources(
    app_id,
    resource_type,
    resource_id,
    resource_name,
    owner_user_id,
    metadata_text,
    status
)
VALUES
(@app_id, 'document', 'doc_001', '新手指南', 'u1001', 'owner=u1001',1),
(@app_id, 'document', 'doc_002', '产品周报', 'u1002', 'owner=u1002',1),
(@app_id, 'document', 'doc_003', '个人草稿', 'u3001', 'owner=u3001',1),
(@app_id, 'document', 'doc_004', '发布公告', 'u1002', 'owner=u1002',1);

-- =========================================================
-- 插入一条样例决策日志
-- =========================================================
INSERT INTO ny_decision_logs(
    app_id,
    app_code,
    user_id,
    perm_key,
    resource_type,
    resource_id,
    allowed,
    decision_source,
    matched_roles,
    matched_permissions,
    owner_shortcut_used,
    policy_version,
    deny_code,
    reason,
    trace_text
)
VALUES
(
    @app_id,
    'doc_center',
    'u1001',
    'document:edit',
    'document',
    'doc_001',
    1,
    'SEED',
    'editor',
    'document:edit',
    0,
    1,
    '',
    '样例日志：编辑者角色允许编辑文档',
    'seed example'
);

-- =========================================================
-- 插入默认管理员账号
-- 默认账号：
--   username = admin
--   password = admin123
--   password_hash = pbkdf2_sha256$iterations$salt$hash
-- =========================================================
INSERT INTO ny_console_users (
    username,
    password_hash,
    display_name,
    status,
    is_super_admin
)
VALUES (
    'admin',
    'pbkdf2_sha256$120000$ny_auth_dev_salt_v1$46f774dced687d5268dae337c26e619f9948d910a5098a2fab3bc73794b4c1b2',
    '系统管理员',
    1,
    1
);

-- =========================================================
-- 插入一条样例审计日志
-- =========================================================
INSERT INTO ny_audit_logs (
    operator_user_id,
    operator_username,
    operator_display_name,
    app_code,
    action_type,
    target_type,
    target_key,
    before_text,
    after_text,
    trace_text
)
VALUES (
    1,
    'admin',
    '系统管理员',
    'doc_center',
    'INIT_AUDIT',
    'system',
    'bootstrap',
    '',
    'created ny_console_users and ny_audit_logs',
    'seed audit log for bootstrap'
);

-- 注意：
-- 不在 SQL 初始化阶段写入样例快照。
-- 快照 JSON 必须由 SnapshotBuilder 从真实策略表生成，否则 Agent 激活后会拿到
-- 缺少 roles / permissions / bindings / owners 的空壳快照。
