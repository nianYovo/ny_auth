# ny_auth

`ny_auth` 是一个基于 C++17、brpc、Protobuf 和 MySQL 的权限鉴权服务。它提供中心化鉴权、RBAC 授权、资源 owner 快捷规则、管理端策略维护、策略发布、策略快照，以及 Agent / Sidecar 本地快照判权能力。

仓库内置 `doc_center` 测试数据，适合用来完整验证“管理端维护策略 -> 发布策略快照 -> 中心化鉴权 -> Agent 本地判权”的链路。

## 功能概览

- 中心化鉴权：通过 `AuthService.Check` 判断用户是否拥有某个权限。
- RBAC 模型：支持用户、角色、权限、角色权限绑定、用户角色绑定。
- owner 快捷规则：资源 owner 可在指定权限开启时直接放行。
- 策略版本：发布策略后递增 `policy_version`，缓存 key 自动切换到新版本。
- 发布回滚：策略版本递增后如果快照构建失败，会尝试回滚到发布前版本。
- 决策解释：响应中返回命中角色、命中权限、判权来源、拒绝码和 trace 文本。
- 管理端接口：支持登录、创建角色、创建权限、绑定权限、授权用户、设置资源 owner、发布策略、模拟鉴权和审计日志查询。
- 策略快照：发布策略时生成只读快照，供 Agent / Sidecar 拉取和激活。
- 本地判权：Agent / Sidecar 可基于已激活快照执行本地权限判断。
- 本地缓存：运行时权限缓存和管理端 session 缓存支持 TTL 和最大条目数。

## 技术栈

- C++17
- CMake 3.10+
- brpc
- Protobuf
- MySQL 8.0 / MySQL Connector C++
- gflags
- OpenSSL、pthread、zlib、LevelDB 等 brpc 相关依赖

## 目录结构

```text
ny_auth/
├── CMakeLists.txt
├── compose.yaml
├── include/                 # 业务头文件
├── proto/                   # RPC 协议定义
│   ├── auth.proto
│   ├── admin.proto
│   └── agent.proto
├── sql/
│   └── init.sql             # 初始化表结构和测试数据
├── src/                     # 业务实现和生成的 protobuf C++ 文件
└── tests/
    └── local_cache_test.cpp
```

## 快速开始

### 1. 启动 MySQL

推荐使用 Docker Compose 启动本地 MySQL：

```bash
docker compose up -d mysql
```

默认端口映射为本机 `3307` -> 容器 `3306`。如果要换成本机其他端口：

```bash
MYSQL_PORT=3308 docker compose up -d mysql
```

确认数据库已启动并完成初始化：

```bash
docker compose ps
docker compose exec mysql mysql -uroot -p123456 -e "SHOW DATABASES;"
docker compose exec mysql mysql -uroot -p123456 ny_auth -e "SHOW TABLES;"
```

首次启动时，MySQL 会自动执行 `sql/init.sql`，创建 `ny_auth` 数据库、业务表和 `doc_center` 测试数据。

如果修改过 SQL 并需要重新初始化本地数据库：

```bash
docker compose down -v
docker compose up -d mysql
```

`docker compose down -v` 会删除本地 MySQL 数据卷，只适合开发和测试环境。

### 2. 安装依赖

Ubuntu / Debian 示例：

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  pkg-config \
  protobuf-compiler \
  libprotobuf-dev \
  libssl-dev \
  zlib1g-dev \
  libleveldb-dev \
  libgflags-dev \
  default-libmysqlclient-dev
```

还需要安装 brpc 和 MySQL Connector/C++，并确保下面命令或文件可用：

```bash
pkg-config --cflags --libs brpc
```

```text
mysql_driver.h
libmysqlcppconn
```

### 3. 编译

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j$(nproc)
```

编译产物：

```text
build/auth_server
build/local_cache_test
```

### 4. 运行测试

```bash
cd build
ctest --output-on-failure
```

也可以直接运行缓存测试：

```bash
./local_cache_test
```

### 5. 启动服务

如果使用默认 Docker Compose 配置，本机 MySQL 端口是 `3307`：

```bash
./build/auth_server \
  --port=8001 \
  --db_host=127.0.0.1 \
  --db_port=3307 \
  --db_user=root \
  --db_password=123456 \
  --db_name=ny_auth \
  --cache_ttl=60 \
  --cache_max_entries=100000 \
  --admin_session_ttl=3600 \
  --admin_session_cache_max_entries=10000
```

如果 MySQL 就在本机 `3306`，可以省略 `--db_port` 或显式传 `--db_port=3306`。

## 启动参数

| 参数 | 默认值 | 说明 |
|---|---:|---|
| `--port` | `8001` | brpc 服务监听端口 |
| `--db_host` | `127.0.0.1` | MySQL 地址 |
| `--db_port` | `3306` | MySQL 端口 |
| `--db_user` | `root` | MySQL 用户名 |
| `--db_password` | `123456` | MySQL 密码 |
| `--db_name` | `ny_auth` | MySQL 数据库 |
| `--cache_ttl` | `60` | 权限缓存 TTL，单位秒 |
| `--cache_max_entries` | `100000` | 权限缓存最大条目数，`0` 表示不限制 |
| `--admin_session_ttl` | `3600` | 管理端登录态 TTL，单位秒 |
| `--admin_session_cache_max_entries` | `10000` | 管理端登录态缓存最大条目数，`0` 表示不限制 |
| `--agent_bootstrap_app_code` | 空 | 启动时自动加载指定 app 的最新快照 |

## 初始化数据

`sql/init.sql` 默认写入一个测试应用：

| 项 | 值 |
|---|---|
| app_code | `doc_center` |
| app_secret | `secret_doc_center_123` |
| 管理员账号 | `admin` |
| 管理员密码 | `admin123` |

管理员密码在数据库中以 `pbkdf2_sha256$iterations$salt$hash` 形式保存。代码仍兼容历史明文密码和旧版 `sha256$salt$hash`，方便旧开发库平滑运行。

测试用户和角色：

| 用户 | 角色 | 说明 |
|---|---|---|
| `u9000` | `admin` | 管理员角色 |
| `u1001` | `editor` | 可读、编辑、发布文档 |
| `u1002` | `viewer` | 只能读取文档 |
| `u3001` | 无 | 用于验证 owner 快捷规则 |

测试资源：

| resource_id | owner |
|---|---|
| `doc_001` | `u1001` |
| `doc_002` | `u1002` |
| `doc_003` | `u3001` |
| `doc_004` | `u1002` |

## 接口调用

brpc 开启 HTTP / JSON 访问后，可以按下面的形式调用：

```text
http://127.0.0.1:8001/<package.Service>/<Method>
```

### 中心化鉴权

接口：

```protobuf
service AuthService {
  rpc Check(CheckRequest) returns (CheckResponse);
}
```

owner 快捷规则放行：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.auth.AuthService/Check' \
  -H 'Content-Type: application/json' \
  -d '{
    "app_code": "doc_center",
    "user_id": "u1001",
    "perm_key": "document:edit",
    "resource_type": "document",
    "resource_id": "doc_001",
    "request_id": "req_owner_edit_001"
  }'
```

RBAC 放行：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.auth.AuthService/Check' \
  -H 'Content-Type: application/json' \
  -d '{
    "app_code": "doc_center",
    "user_id": "u1002",
    "perm_key": "document:read",
    "resource_type": "document",
    "resource_id": "doc_001",
    "request_id": "req_viewer_read_001"
  }'
```

权限不足：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.auth.AuthService/Check' \
  -H 'Content-Type: application/json' \
  -d '{
    "app_code": "doc_center",
    "user_id": "u1002",
    "perm_key": "document:delete",
    "resource_type": "document",
    "resource_id": "doc_002",
    "request_id": "req_viewer_delete_001"
  }'
```

### 管理端登录

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.admin.AdminService/Login' \
  -H 'Content-Type: application/json' \
  -d '{
    "username": "admin",
    "password": "admin123"
  }'
```

响应中的 `session.token` 是后续管理接口的 `operator_token`。

### 发布策略并生成快照

先把登录响应里的 token 放进环境变量：

```bash
TOKEN='把 Login 返回的 session.token 放这里'
```

发布策略：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.admin.AdminService/PublishPolicy' \
  -H 'Content-Type: application/json' \
  -d "{
    \"operator_token\": \"${TOKEN}\",
    \"app_code\": \"doc_center\",
    \"publish_note\": \"manual test publish\"
  }"
```

成功后会递增 `ny_policy_versions.current_version`，并写入 `ny_policy_snapshots` 和 `ny_snapshot_publish_logs`。

如果策略版本已经递增但快照构建失败，服务会尝试把 `current_version` 回滚到发布前版本，并写入失败审计日志，避免出现“版本已发布但没有对应快照”的半成功状态。

### Agent 拉取和激活快照

拉取最新快照：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.agent.AgentService/PullLatestSnapshot' \
  -H 'Content-Type: application/json' \
  -d '{
    "app_code": "doc_center"
  }'
```

激活最新快照：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.agent.AgentService/ActivateLatestSnapshot' \
  -H 'Content-Type: application/json' \
  -d '{
    "app_code": "doc_center"
  }'
```

基于本地快照判权：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.agent.AgentService/LocalCheck' \
  -H 'Content-Type: application/json' \
  -d '{
    "app_code": "doc_center",
    "user_id": "u1001",
    "perm_key": "document:edit",
    "resource_type": "document",
    "resource_id": "doc_001",
    "request_id": "req_local_owner_edit_001"
  }'
```

查询本地快照状态：

```bash
curl -s -X POST 'http://127.0.0.1:8001/ny.agent.AgentService/GetLocalSnapshotStatus' \
  -H 'Content-Type: application/json' \
  -d '{}'
```

## 服务接口

| 服务 | 方法 | 说明 |
|---|---|---|
| `ny.auth.AuthService` | `Check` | 中心化鉴权 |
| `ny.admin.AdminService` | `Login` | 管理员登录 |
| `ny.admin.AdminService` | `CreateRole` | 创建角色 |
| `ny.admin.AdminService` | `CreatePermission` | 创建权限 |
| `ny.admin.AdminService` | `BindPermissionToRole` | 给角色绑定权限 |
| `ny.admin.AdminService` | `GrantRoleToUser` | 给用户授角色 |
| `ny.admin.AdminService` | `SetResourceOwner` | 设置资源 owner |
| `ny.admin.AdminService` | `PublishPolicy` | 发布策略并生成快照 |
| `ny.admin.AdminService` | `SimulateCheck` | 模拟鉴权 |
| `ny.admin.AdminService` | `ListAuditLogs` | 查询管理审计日志 |
| `ny.agent.AgentService` | `PullLatestSnapshot` | 拉取最新快照 |
| `ny.agent.AgentService` | `PullSnapshotByVersion` | 按版本拉取快照 |
| `ny.agent.AgentService` | `ActivateLatestSnapshot` | 激活最新快照 |
| `ny.agent.AgentService` | `ActivateSnapshotByVersion` | 激活指定版本快照 |
| `ny.agent.AgentService` | `GetLocalSnapshotStatus` | 查询本地快照状态 |
| `ny.agent.AgentService` | `LocalCheck` | 基于本地快照判权 |

## 判权流程

中心化鉴权流程：

1. 校验 `app_code`、`user_id`、`perm_key` 和资源参数。
2. 查询应用是否存在、是否启用，并读取当前策略版本。
3. 校验权限是否存在、是否启用。
4. 如果传了资源，校验资源是否存在、是否启用。
5. 尝试 owner 快捷规则。
6. owner 未放行时，从本地缓存或数据库加载用户权限集合。
7. 使用 RBAC 判断用户是否拥有目标权限。
8. 返回可解释 trace，并写入 `ny_decision_logs`。

缓存 key 格式：

```text
app_code:user_id:policy_version
```

策略发布后版本递增，新请求自然使用新缓存 key，不需要全量清空缓存。

本地快照判权流程：

1. Agent 激活某个 app 的策略快照。
2. `LocalSnapshotEngine` 在内存中建立角色、权限、绑定和资源 owner 索引。
3. `LocalCheck` 先验证快照状态和 app 是否匹配。
4. 再按权限定义、资源有效性、owner shortcut、RBAC 顺序判断。
5. 响应会带上 `snapshot_id`、`policy_version` 和本地判权 trace。

## 数据库表

基础策略表：

| 表名 | 说明 |
|---|---|
| `ny_apps` | 接入应用 |
| `ny_policy_versions` | 应用当前策略版本 |
| `ny_roles` | 角色 |
| `ny_permissions` | 权限 |
| `ny_role_permissions` | 角色-权限绑定 |
| `ny_user_roles` | 用户-角色绑定 |
| `ny_resources` | 资源 owner 数据 |
| `ny_decision_logs` | 每次鉴权的决策日志 |

管理端和快照表：

| 表名 | 说明 |
|---|---|
| `ny_console_users` | 管理员账号 |
| `ny_audit_logs` | 管理操作审计日志 |
| `ny_policy_snapshots` | 策略快照 |
| `ny_snapshot_publish_logs` | 快照发布日志 |

## 安全说明

- 管理员密码以 PBKDF2-SHA256 哈希存储，登录验证使用常量时间比较。
- 管理员 token 使用 OpenSSL `RAND_bytes` 生成 32 字节随机值。
- 管理端 session 存在内存缓存中，进程重启后会失效。
- 默认管理员账号和密码只适合开发环境，生产环境应强制修改或移除。
- 当前管理端授权策略是 `is_super_admin = true` 才允许执行管理操作。
- 建议生产环境使用 HTTPS / 网关鉴权保护 brpc HTTP JSON 入口。
- 建议生产环境改用 Argon2id 或 bcrypt，并加入登录失败限流和锁定策略。

## 缓存说明

运行时权限缓存：

- 缓存内容：某个 app、用户、策略版本下的权限集合。
- TTL 参数：`--cache_ttl`。
- 容量参数：`--cache_max_entries`。
- 淘汰策略：超过容量时移除最早写入的条目，并优先清理已过期条目。

管理端 session 缓存：

- TTL 参数：`--admin_session_ttl`。
- 容量参数：`--admin_session_cache_max_entries`。
- 进程重启后登录态全部失效。

## Protobuf 代码生成

修改 `proto/*.proto` 后执行：

```bash
protoc -I=proto --cpp_out=src proto/auth.proto
protoc -I=proto --cpp_out=src proto/admin.proto
protoc -I=proto --cpp_out=src proto/agent.proto
```

然后重新构建：

```bash
cmake --build build -j$(nproc)
```

## 开发检查清单

提交前建议执行：

```bash
docker compose config --quiet
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

如果不想污染仓库内 `build/` 目录，也可以使用临时构建目录：

```bash
cmake -S . -B /tmp/ny_auth_build
cmake --build /tmp/ny_auth_build -j$(nproc)
ctest --test-dir /tmp/ny_auth_build --output-on-failure
```

## 常见问题

### 服务启动时报数据库连接失败

如果使用 Docker Compose 默认配置，请确认服务启动参数包含：

```bash
--db_port=3307
```

也可以改用：

```bash
MYSQL_PORT=3306 docker compose up -d mysql
```

### 修改 SQL 后没有生效

Docker 初始化脚本只会在数据目录为空时执行。如果已经启动过 MySQL，需要删除本地数据卷后重新初始化：

```bash
docker compose down -v
docker compose up -d mysql
```

### `mysqlcppconn not found`

请确认已安装 MySQL Connector/C++，并且 CMake 能找到 `mysql_driver.h` 和 `libmysqlcppconn`。

### `pkg-config` 找不到 brpc

请确认 brpc 已正确安装，并且下面命令有输出：

```bash
pkg-config --cflags --libs brpc
```

### 登录失败

默认账号是 `admin`，默认密码是 `admin123`。如果你之前已经初始化过本地数据库，数据库中可能仍是旧数据；可以继续使用旧明文兼容逻辑，也可以重新初始化数据库。

## 当前限制和后续方向

- DAO 当前仍是按操作创建 MySQL 连接，高并发场景建议引入连接池。
- 快照 JSON 解析覆盖当前项目结构所需的 JSON 子集，后续可替换为成熟 JSON 库。
- SQL 初始化脚本适合开发环境，生产环境建议拆成正式 migration。
- 当前测试覆盖集中在本地缓存，建议继续补充鉴权、发布回滚、快照 round-trip 和集成测试。
