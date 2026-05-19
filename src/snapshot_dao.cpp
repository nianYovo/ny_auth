#include "snapshot_dao.h"

#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

// ======================================================
// 下面这些辅助函数服务于快照 JSON 序列化 / 反序列化。
// 当前实现覆盖项目快照结构所需的 JSON 子集，并处理基础转义。
// ======================================================

// ------------------------------------------------------
// JsonValue
// 作用：表示一个极简 JSON 值
// 我们当前只支持：
// - null
// - bool
// - number（用 int64_t 表示）
// - string
// - object
// - array
// ------------------------------------------------------
struct JsonValue {
    enum class Type {
        kNull,
        kBool,
        kNumber,
        kString,
        kObject,
        kArray
    };

    Type type = Type::kNull;

    bool bool_value = false;
    int64_t number_value = 0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::unordered_map<std::string, JsonValue> object_value;
};

// ------------------------------------------------------
// SkipWhitespace
// 作用：跳过 JSON 文本中的空白字符
// ------------------------------------------------------
void SkipWhitespace(const std::string& text, std::size_t& pos) {
    while (pos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

// ------------------------------------------------------
// EscapeJsonString
// 作用：把普通字符串转成可安全写进 JSON 的字符串内容
// 例如：
//   " -> \"
//   "\ -> \\"
//   换行 -> \n
// ------------------------------------------------------
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

// ------------------------------------------------------
// ParseJsonString
// 作用：从当前位置解析一个 JSON string
// 要求当前位置必须是双引号 "
// ------------------------------------------------------
std::string ParseJsonString(const std::string& text, std::size_t& pos) {
    if (pos >= text.size() || text[pos] != '\"') {
        throw std::runtime_error("expected '\"' at beginning of json string");
    }

    // 跳过起始双引号
    ++pos;

    std::ostringstream oss;

    while (pos < text.size()) {
        const char ch = text[pos++];

        // 遇到结束双引号，说明字符串结束
        if (ch == '\"') {
            return oss.str();
        }

        // 处理转义
        if (ch == '\\') {
            if (pos >= text.size()) {
                throw std::runtime_error("invalid escape sequence in json string");
            }

            const char esc = text[pos++];
            switch (esc) {
                case '\"':
                    oss << '\"';
                    break;
                case '\\':
                    oss << '\\';
                    break;
                case '/':
                    oss << '/';
                    break;
                case 'b':
                    oss << '\b';
                    break;
                case 'f':
                    oss << '\f';
                    break;
                case 'n':
                    oss << '\n';
                    break;
                case 'r':
                    oss << '\r';
                    break;
                case 't':
                    oss << '\t';
                    break;
                case 'u': {
                    if (pos + 4 > text.size()) {
                        throw std::runtime_error("invalid unicode escape in json string");
                    }

                    int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char hex = text[pos++];
                        code <<= 4;
                        if (hex >= '0' && hex <= '9') {
                            code += hex - '0';
                        } else if (hex >= 'a' && hex <= 'f') {
                            code += 10 + hex - 'a';
                        } else if (hex >= 'A' && hex <= 'F') {
                            code += 10 + hex - 'A';
                        } else {
                            throw std::runtime_error("invalid unicode escape in json string");
                        }
                    }

                    if (code <= 0x7F) {
                        oss << static_cast<char>(code);
                    } else {
                        throw std::runtime_error("non-ascii unicode escape is not supported");
                    }
                    break;
                }
                default:
                    throw std::runtime_error("unsupported escape in json string");
            }
            continue;
        }

        oss << ch;
    }

    throw std::runtime_error("unterminated json string");
}

// 前置声明：因为对象/数组/值的解析会相互调用
JsonValue ParseJsonValue(const std::string& text, std::size_t& pos);

// ------------------------------------------------------
// ParseJsonNumber
// 作用：解析一个整数 number
// 当前快照里我们只需要整数，所以实现成 int64_t 即可
// ------------------------------------------------------
JsonValue ParseJsonNumber(const std::string& text, std::size_t& pos) {
    const std::size_t start = pos;

    if (text[pos] == '-') {
        ++pos;
    }

    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
        throw std::runtime_error("invalid json number");
    }

    while (pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }

    JsonValue value;
    value.type = JsonValue::Type::kNumber;
    value.number_value = std::stoll(text.substr(start, pos - start));
    return value;
}

// ------------------------------------------------------
// ParseJsonArray
// 作用：解析 JSON 数组
// 当前位置要求是 [
// ------------------------------------------------------
JsonValue ParseJsonArray(const std::string& text, std::size_t& pos) {
    if (pos >= text.size() || text[pos] != '[') {
        throw std::runtime_error("expected '['");
    }

    JsonValue value;
    value.type = JsonValue::Type::kArray;

    // 跳过 [
    ++pos;

    SkipWhitespace(text, pos);

    // 空数组：[]
    if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return value;
    }

    while (pos < text.size()) {
        SkipWhitespace(text, pos);

        JsonValue item = ParseJsonValue(text, pos);
        value.array_value.push_back(std::move(item));

        SkipWhitespace(text, pos);

        if (pos >= text.size()) {
            throw std::runtime_error("unterminated json array");
        }

        if (text[pos] == ',') {
            ++pos;
            continue;
        }

        if (text[pos] == ']') {
            ++pos;
            return value;
        }

        throw std::runtime_error("expected ',' or ']' in json array");
    }

    throw std::runtime_error("unterminated json array");
}

// ------------------------------------------------------
// ParseJsonObject
// 作用：解析 JSON 对象
// 当前位置要求是 {
// ------------------------------------------------------
JsonValue ParseJsonObject(const std::string& text, std::size_t& pos) {
    if (pos >= text.size() || text[pos] != '{') {
        throw std::runtime_error("expected '{'");
    }

    JsonValue value;
    value.type = JsonValue::Type::kObject;

    // 跳过 {
    ++pos;

    SkipWhitespace(text, pos);

    // 空对象：{}
    if (pos < text.size() && text[pos] == '}') {
        ++pos;
        return value;
    }

    while (pos < text.size()) {
        SkipWhitespace(text, pos);

        if (pos >= text.size() || text[pos] != '\"') {
            throw std::runtime_error("expected object key string");
        }

        const std::string key = ParseJsonString(text, pos);

        SkipWhitespace(text, pos);

        if (pos >= text.size() || text[pos] != ':') {
            throw std::runtime_error("expected ':' after object key");
        }

        // 跳过 :
        ++pos;

        SkipWhitespace(text, pos);

        JsonValue field_value = ParseJsonValue(text, pos);
        value.object_value[key] = std::move(field_value);

        SkipWhitespace(text, pos);

        if (pos >= text.size()) {
            throw std::runtime_error("unterminated json object");
        }

        if (text[pos] == ',') {
            ++pos;
            continue;
        }

        if (text[pos] == '}') {
            ++pos;
            return value;
        }

        throw std::runtime_error("expected ',' or '}' in json object");
    }

    throw std::runtime_error("unterminated json object");
}

// ------------------------------------------------------
// ParseJsonValue
// 作用：根据当前位置字符，分派到不同类型解析函数
// ------------------------------------------------------
JsonValue ParseJsonValue(const std::string& text, std::size_t& pos) {
    SkipWhitespace(text, pos);

    if (pos >= text.size()) {
        throw std::runtime_error("unexpected end of json");
    }

    const char ch = text[pos];

    // string
    if (ch == '\"') {
        JsonValue value;
        value.type = JsonValue::Type::kString;
        value.string_value = ParseJsonString(text, pos);
        return value;
    }

    // object
    if (ch == '{') {
        return ParseJsonObject(text, pos);
    }

    // array
    if (ch == '[') {
        return ParseJsonArray(text, pos);
    }

    // true
    if (text.compare(pos, 4, "true") == 0) {
        pos += 4;
        JsonValue value;
        value.type = JsonValue::Type::kBool;
        value.bool_value = true;
        return value;
    }

    // false
    if (text.compare(pos, 5, "false") == 0) {
        pos += 5;
        JsonValue value;
        value.type = JsonValue::Type::kBool;
        value.bool_value = false;
        return value;
    }

    // null
    if (text.compare(pos, 4, "null") == 0) {
        pos += 4;
        JsonValue value;
        value.type = JsonValue::Type::kNull;
        return value;
    }

    // number
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
        return ParseJsonNumber(text, pos);
    }

    throw std::runtime_error("unsupported json value");
}

// ------------------------------------------------------
// ParseRootJson
// 作用：解析整段 JSON 文本，并要求消费完整个字符串
// ------------------------------------------------------
JsonValue ParseRootJson(const std::string& text) {
    std::size_t pos = 0;
    JsonValue root = ParseJsonValue(text, pos);

    SkipWhitespace(text, pos);

    if (pos != text.size()) {
        throw std::runtime_error("unexpected trailing content in json");
    }

    return root;
}

// ------------------------------------------------------
// FindObjectField
// 作用：从 object 里找某个 key，对应不到返回 nullptr
// ------------------------------------------------------
const JsonValue* FindObjectField(const JsonValue& object_value,
                                 const std::string& key) {
    if (object_value.type != JsonValue::Type::kObject) {
        return nullptr;
    }

    const auto it = object_value.object_value.find(key);
    if (it == object_value.object_value.end()) {
        return nullptr;
    }

    return &(it->second);
}

// ------------------------------------------------------
// GetStringFieldOrDefault
// 作用：安全读取 object.key 对应的 string，取不到就返回默认值
// ------------------------------------------------------
std::string GetStringFieldOrDefault(const JsonValue& object_value,
                                    const std::string& key,
                                    const std::string& default_value = "") {
    const JsonValue* field = FindObjectField(object_value, key);
    if (field == nullptr || field->type != JsonValue::Type::kString) {
        return default_value;
    }
    return field->string_value;
}

// ------------------------------------------------------
// GetIntFieldOrDefault
// 作用：安全读取 object.key 对应的 int64，取不到就返回默认值
// ------------------------------------------------------
int64_t GetIntFieldOrDefault(const JsonValue& object_value,
                             const std::string& key,
                             int64_t default_value = 0) {
    const JsonValue* field = FindObjectField(object_value, key);
    if (field == nullptr || field->type != JsonValue::Type::kNumber) {
        return default_value;
    }
    return field->number_value;
}

// ------------------------------------------------------
// GetBoolFieldOrDefault
// 作用：安全读取 object.key 对应的 bool，取不到就返回默认值
// ------------------------------------------------------
bool GetBoolFieldOrDefault(const JsonValue& object_value,
                           const std::string& key,
                           bool default_value = false) {
    const JsonValue* field = FindObjectField(object_value, key);
    if (field == nullptr || field->type != JsonValue::Type::kBool) {
        return default_value;
    }
    return field->bool_value;
}

}  // namespace

// ======================================================
// 构造函数
// 作用：保存数据库连接参数
// ======================================================
SnapshotDAO::SnapshotDAO(const std::string& host,
                         int port,
                         const std::string& user,
                         const std::string& password,
                         const std::string& database)
    : host_(host),
      port_(port),
      user_(user),
      password_(password),
      database_(database) {}

// ======================================================
// createConnection
// 作用：创建一个新的 MySQL 连接
// 当前仍采用“每次操作新建连接”的简单模式
// ======================================================
sql::Connection* SnapshotDAO::createConnection() {
    // 获取 MySQL 驱动实例
    sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

    // 拼接地址，例如 tcp://127.0.0.1:3306
    std::ostringstream oss;
    oss << "tcp://" << host_ << ":" << port_;

    // 建立连接
    sql::Connection* conn = driver->connect(oss.str(), user_, password_);

    // 选择数据库
    conn->setSchema(database_);

    return conn;
}

// ======================================================
// setLastError
// 作用：线程安全地记录最近一次数据库错误
// ======================================================
void SnapshotDAO::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

// ======================================================
// saveSnapshot
// 作用：把一份快照写进 ny_policy_snapshots
//
// 规则：
// - (app_id, policy_version) 已存在 -> 覆盖更新
// - 不存在 -> 新插入
// - 最终返回这份快照在表中的 id
// ======================================================
int64_t SnapshotDAO::saveSnapshot(const PolicySnapshot& snapshot,
                                  const std::string& snapshot_json) {
    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        // 最终入库的 JSON 文本
        const std::string final_snapshot_json =
            snapshot_json.empty() ? serializeSnapshotToJson(snapshot) : snapshot_json;

        // 这里使用 INSERT ... ON DUPLICATE KEY UPDATE
        // 依赖表上的唯一键：(app_id, policy_version)
        std::unique_ptr<sql::PreparedStatement> stmt(
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

        stmt->setInt64(1, snapshot.app_info.app_id);
        stmt->setString(2, snapshot.app_info.app_code);
        stmt->setInt(3, snapshot.app_info.policy_version);
        stmt->setString(4, final_snapshot_json);
        stmt->setString(5, snapshot.meta.published_by);
        stmt->setString(6, snapshot.meta.publish_note);

        if (stmt->executeUpdate() <= 0) {
            return 0;
        }

        // 重新查回 id
        std::unique_ptr<sql::PreparedStatement> query_stmt(
            conn->prepareStatement(
                "SELECT id "
                "FROM ny_policy_snapshots "
                "WHERE app_id = ? "
                "  AND policy_version = ? "
                "LIMIT 1"
            )
        );

        query_stmt->setInt64(1, snapshot.app_info.app_id);
        query_stmt->setInt(2, snapshot.app_info.policy_version);

        std::unique_ptr<sql::ResultSet> rs(query_stmt->executeQuery());
        if (!rs->next()) {
            return 0;
        }

        return rs->getInt64("id");
    } catch (const sql::SQLException& e) {
        setLastError(e.what());
        return 0;
    } catch (const std::exception& e) {
        setLastError(e.what());
        return 0;
    }
}

// ======================================================
// getSnapshotByVersion
// 作用：按 app_code + policy_version 查询某一版快照
// ======================================================
std::optional<PolicySnapshot> SnapshotDAO::getSnapshotByVersion(
    const std::string& app_code,
    int policy_version) {
    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT id, app_code, policy_version, snapshot_json, "
                "       published_by, publish_note, "
                "       DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
                "FROM ny_policy_snapshots "
                "WHERE app_code = ? "
                "  AND policy_version = ? "
                "LIMIT 1"
            )
        );

        stmt->setString(1, app_code);
        stmt->setInt(2, policy_version);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            return std::nullopt;
        }

        return deserializeSnapshotFromJson(
            rs->getInt64("id"),
            rs->getString("app_code"),
            rs->getInt("policy_version"),
            rs->getString("snapshot_json"),
            rs->getString("published_by"),
            rs->getString("publish_note"),
            rs->getString("created_at")
        );
    } catch (const sql::SQLException& e) {
        setLastError(e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        setLastError(e.what());
        return std::nullopt;
    }
}

// ======================================================
// getLatestSnapshot
// 作用：查询某个 app 当前最新已发布快照
//
// 规则：
// - status = 1
// - policy_version 倒序
// - 取 1 条
// ======================================================
std::optional<PolicySnapshot> SnapshotDAO::getLatestSnapshot(
    const std::string& app_code) {
    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT id, app_code, policy_version, snapshot_json, "
                "       published_by, publish_note, "
                "       DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
                "FROM ny_policy_snapshots "
                "WHERE app_code = ? "
                "  AND status = 1 "
                "ORDER BY policy_version DESC "
                "LIMIT 1"
            )
        );

        stmt->setString(1, app_code);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            return std::nullopt;
        }

        return deserializeSnapshotFromJson(
            rs->getInt64("id"),
            rs->getString("app_code"),
            rs->getInt("policy_version"),
            rs->getString("snapshot_json"),
            rs->getString("published_by"),
            rs->getString("publish_note"),
            rs->getString("created_at")
        );
    } catch (const sql::SQLException& e) {
        setLastError(e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        setLastError(e.what());
        return std::nullopt;
    }
}

// ======================================================
// snapshotExists
// 作用：判断某个 app 的某个版本快照是否存在
// ======================================================
bool SnapshotDAO::snapshotExists(const std::string& app_code,
                                 int policy_version) {
    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT 1 "
                "FROM ny_policy_snapshots "
                "WHERE app_code = ? "
                "  AND policy_version = ? "
                "LIMIT 1"
            )
        );

        stmt->setString(1, app_code);
        stmt->setInt(2, policy_version);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        return rs->next();
    } catch (const sql::SQLException& e) {
        setLastError(e.what());
        return false;
    }
}

// ======================================================
// listSnapshotMetas
// 作用：查询某个 app 的快照元信息列表（按版本倒序）
// 注意：这里只查元信息，不拉 snapshot_json
// ======================================================
std::vector<SnapshotMeta> SnapshotDAO::listSnapshotMetas(
    const std::string& app_code,
    int limit) {
    std::vector<SnapshotMeta> items;

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        if (limit <= 0) {
            limit = 20;
        }

        std::string sql =
            "SELECT id, published_by, publish_note, "
            "       DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
            "FROM ny_policy_snapshots ";

        bool has_app_filter = !app_code.empty();
        if (has_app_filter) {
            sql += "WHERE app_code = ? ";
        }

        sql += "ORDER BY policy_version DESC LIMIT ?";

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(sql)
        );

        int param_index = 1;
        if (has_app_filter) {
            stmt->setString(param_index++, app_code);
        }
        stmt->setInt(param_index++, limit);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        while (rs->next()) {
            SnapshotMeta item;
            item.snapshot_id = rs->getInt64("id");
            item.published_by = rs->getString("published_by");
            item.publish_note = rs->getString("publish_note");
            item.created_at = rs->getString("created_at");

            items.push_back(item);
        }
    } catch (const sql::SQLException& e) {
        setLastError(e.what());
    }

    return items;
}

// ======================================================
// insertSnapshotPublishLog
// 作用：写入一条快照发布日志
// ======================================================
bool SnapshotDAO::insertSnapshotPublishLog(
    const SnapshotPublishLogRecord& record) {
    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "INSERT INTO ny_snapshot_publish_logs ("
                "  app_id, app_code, policy_version, snapshot_id, "
                "  published_by, publish_result, trace_text "
                ") VALUES (?, ?, ?, ?, ?, ?, ?)"
            )
        );

        stmt->setInt64(1, record.app_id);
        stmt->setString(2, record.app_code);
        stmt->setInt(3, record.policy_version);
        stmt->setInt64(4, record.snapshot_id);
        stmt->setString(5, record.published_by);
        stmt->setString(6, record.publish_result);
        stmt->setString(7, record.trace_text);

        return stmt->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        setLastError(e.what());
        return false;
    }
}

// ======================================================
// listSnapshotPublishLogs
// 作用：按条件查询快照发布日志
// 规则：
// - app_code 为空 -> 不按 app 过滤
// - publish_result 为空 -> 不按结果过滤
// - limit <= 0 -> 默认 20
// ======================================================
std::vector<SnapshotPublishLogItem> SnapshotDAO::listSnapshotPublishLogs(
    const std::string& app_code,
    const std::string& publish_result,
    int limit) {
    std::vector<SnapshotPublishLogItem> items;

    try {
        std::unique_ptr<sql::Connection> conn(createConnection());

        if (limit <= 0) {
            limit = 20;
        }

        std::string sql =
            "SELECT id, app_id, app_code, policy_version, snapshot_id, "
            "       published_by, publish_result, trace_text, "
            "       DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
            "FROM ny_snapshot_publish_logs "
            "WHERE 1 = 1 ";

        const bool has_app_filter = !app_code.empty();
        const bool has_result_filter = !publish_result.empty();

        if (has_app_filter) {
            sql += "AND app_code = ? ";
        }

        if (has_result_filter) {
            sql += "AND publish_result = ? ";
        }

        sql += "ORDER BY id DESC LIMIT ?";

        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(sql)
        );

        int param_index = 1;
        if (has_app_filter) {
            stmt->setString(param_index++, app_code);
        }
        if (has_result_filter) {
            stmt->setString(param_index++, publish_result);
        }
        stmt->setInt(param_index++, limit);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        while (rs->next()) {
            SnapshotPublishLogItem item;
            item.id = rs->getInt64("id");
            item.app_id = rs->getInt64("app_id");
            item.app_code = rs->getString("app_code");
            item.policy_version = rs->getInt("policy_version");
            item.snapshot_id = rs->getInt64("snapshot_id");
            item.published_by = rs->getString("published_by");
            item.publish_result = rs->getString("publish_result");
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
bool SnapshotDAO::isConnected() const {
    try {
        std::unique_ptr<sql::Connection> conn(
            const_cast<SnapshotDAO*>(this)->createConnection()
        );
        return conn != nullptr;
    } catch (...) {
        return false;
    }
}

// ======================================================
// getLastError
// 作用：返回最近一次数据库错误
// ======================================================
std::string SnapshotDAO::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

// ======================================================
// serializeSnapshotToJson
// 作用：把 PolicySnapshot 序列化成 JSON 字符串
//
// 说明：
// 1. 当前实现采用手工拼 JSON
// 2. 重点是先让“快照落库 -> 读取 -> 本地判权”链路跑通
// 3. 后面你可以替换成 nlohmann/json / RapidJSON 等
// ======================================================
std::string SnapshotDAO::serializeSnapshotToJson(
    const PolicySnapshot& snapshot) const {
    std::ostringstream oss;

    oss << "{";

    // --------------------------------------------------
    // app_info
    // --------------------------------------------------
    oss << "\"app_info\":{"
        << "\"app_id\":" << snapshot.app_info.app_id << ","
        << "\"app_code\":\"" << EscapeJsonString(snapshot.app_info.app_code) << "\","
        << "\"enabled\":" << (snapshot.app_info.enabled ? "true" : "false") << ","
        << "\"policy_version\":" << snapshot.app_info.policy_version
        << "},";

    // --------------------------------------------------
    // meta
    // --------------------------------------------------
    oss << "\"meta\":{"
        << "\"snapshot_id\":" << snapshot.meta.snapshot_id << ","
        << "\"published_by\":\"" << EscapeJsonString(snapshot.meta.published_by) << "\","
        << "\"publish_note\":\"" << EscapeJsonString(snapshot.meta.publish_note) << "\","
        << "\"created_at\":\"" << EscapeJsonString(snapshot.meta.created_at) << "\""
        << "},";

    // --------------------------------------------------
    // roles
    // --------------------------------------------------
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

    // --------------------------------------------------
    // permissions
    // --------------------------------------------------
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

    // --------------------------------------------------
    // role_permission_bindings
    // --------------------------------------------------
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

    // --------------------------------------------------
    // user_role_bindings
    // --------------------------------------------------
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

    // --------------------------------------------------
    // resource_owners
    // --------------------------------------------------
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
// deserializeSnapshotFromJson
// 作用：把数据库中的 JSON 文本反序列化成 PolicySnapshot
//
// 当前实现目标：
// 1. 能正确解析本类 serializeSnapshotToJson 生成出来的 JSON
// 2. 这是一个“最低可用版本”
// 3. 足够支撑后面 Agent 拉取和本地判权
// ======================================================
std::optional<PolicySnapshot> SnapshotDAO::deserializeSnapshotFromJson(
    int64_t snapshot_id,
    const std::string& app_code,
    int policy_version,
    const std::string& snapshot_json,
    const std::string& published_by,
    const std::string& publish_note,
    const std::string& created_at) const {
    try {
        const JsonValue root = ParseRootJson(snapshot_json);

        if (root.type != JsonValue::Type::kObject) {
            return std::nullopt;
        }

        PolicySnapshot snapshot;

        // --------------------------------------------------
        // 如果 JSON 里有 app_info，则优先读 JSON；
        // 如果缺失，则用函数参数回填最基础信息。
        // --------------------------------------------------
        const JsonValue* app_info_obj = FindObjectField(root, "app_info");
        if (app_info_obj != nullptr && app_info_obj->type == JsonValue::Type::kObject) {
            snapshot.app_info.app_id =
                GetIntFieldOrDefault(*app_info_obj, "app_id", 0);
            snapshot.app_info.app_code =
                GetStringFieldOrDefault(*app_info_obj, "app_code", app_code);
            snapshot.app_info.enabled =
                GetBoolFieldOrDefault(*app_info_obj, "enabled", true);
            snapshot.app_info.policy_version =
                static_cast<int>(GetIntFieldOrDefault(
                    *app_info_obj, "policy_version", policy_version));
        } else {
            snapshot.app_info.app_id = 0;
            snapshot.app_info.app_code = app_code;
            snapshot.app_info.enabled = true;
            snapshot.app_info.policy_version = policy_version;
        }

        // --------------------------------------------------
        // meta
        // --------------------------------------------------
        const JsonValue* meta_obj = FindObjectField(root, "meta");
        if (meta_obj != nullptr && meta_obj->type == JsonValue::Type::kObject) {
            snapshot.meta.snapshot_id =
                GetIntFieldOrDefault(*meta_obj, "snapshot_id", snapshot_id);
            snapshot.meta.published_by =
                GetStringFieldOrDefault(*meta_obj, "published_by", published_by);
            snapshot.meta.publish_note =
                GetStringFieldOrDefault(*meta_obj, "publish_note", publish_note);
            snapshot.meta.created_at =
                GetStringFieldOrDefault(*meta_obj, "created_at", created_at);
        } else {
            snapshot.meta.snapshot_id = snapshot_id;
            snapshot.meta.published_by = published_by;
            snapshot.meta.publish_note = publish_note;
            snapshot.meta.created_at = created_at;
        }

        // --------------------------------------------------
        // roles
        // --------------------------------------------------
        const JsonValue* roles_arr = FindObjectField(root, "roles");
        if (roles_arr != nullptr && roles_arr->type == JsonValue::Type::kArray) {
            for (const auto& item : roles_arr->array_value) {
                if (item.type != JsonValue::Type::kObject) {
                    continue;
                }

                SnapshotRole role;
                role.role_id = GetIntFieldOrDefault(item, "role_id", 0);
                role.role_key = GetStringFieldOrDefault(item, "role_key", "");
                role.role_name = GetStringFieldOrDefault(item, "role_name", "");
                role.description = GetStringFieldOrDefault(item, "description", "");
                role.is_default = GetBoolFieldOrDefault(item, "is_default", false);
                role.enabled = GetBoolFieldOrDefault(item, "enabled", true);

                snapshot.roles.push_back(role);
            }
        }

        // --------------------------------------------------
        // permissions
        // --------------------------------------------------
        const JsonValue* perms_arr = FindObjectField(root, "permissions");
        if (perms_arr != nullptr && perms_arr->type == JsonValue::Type::kArray) {
            for (const auto& item : perms_arr->array_value) {
                if (item.type != JsonValue::Type::kObject) {
                    continue;
                }

                SnapshotPermission perm;
                perm.permission_id =
                    GetIntFieldOrDefault(item, "permission_id", 0);
                perm.perm_key =
                    GetStringFieldOrDefault(item, "perm_key", "");
                perm.perm_name =
                    GetStringFieldOrDefault(item, "perm_name", "");
                perm.resource_type =
                    GetStringFieldOrDefault(item, "resource_type", "");
                perm.owner_shortcut_enabled =
                    GetBoolFieldOrDefault(item, "owner_shortcut_enabled", false);
                perm.description =
                    GetStringFieldOrDefault(item, "description", "");
                perm.enabled =
                    GetBoolFieldOrDefault(item, "enabled", true);

                snapshot.permissions.push_back(perm);
            }
        }

        // --------------------------------------------------
        // role_permission_bindings
        // --------------------------------------------------
        const JsonValue* rp_arr =
            FindObjectField(root, "role_permission_bindings");
        if (rp_arr != nullptr && rp_arr->type == JsonValue::Type::kArray) {
            for (const auto& item : rp_arr->array_value) {
                if (item.type != JsonValue::Type::kObject) {
                    continue;
                }

                SnapshotRolePermissionBinding binding;
                binding.role_key = GetStringFieldOrDefault(item, "role_key", "");
                binding.perm_key = GetStringFieldOrDefault(item, "perm_key", "");

                snapshot.role_permission_bindings.push_back(binding);
            }
        }

        // --------------------------------------------------
        // user_role_bindings
        // --------------------------------------------------
        const JsonValue* ur_arr =
            FindObjectField(root, "user_role_bindings");
        if (ur_arr != nullptr && ur_arr->type == JsonValue::Type::kArray) {
            for (const auto& item : ur_arr->array_value) {
                if (item.type != JsonValue::Type::kObject) {
                    continue;
                }

                SnapshotUserRoleBinding binding;
                binding.user_id = GetStringFieldOrDefault(item, "user_id", "");
                binding.role_key = GetStringFieldOrDefault(item, "role_key", "");
                binding.granted_by = GetStringFieldOrDefault(item, "granted_by", "");

                snapshot.user_role_bindings.push_back(binding);
            }
        }

        // --------------------------------------------------
        // resource_owners
        // --------------------------------------------------
        const JsonValue* owner_arr =
            FindObjectField(root, "resource_owners");
        if (owner_arr != nullptr && owner_arr->type == JsonValue::Type::kArray) {
            for (const auto& item : owner_arr->array_value) {
                if (item.type != JsonValue::Type::kObject) {
                    continue;
                }

                SnapshotResourceOwner owner;
                owner.resource_type =
                    GetStringFieldOrDefault(item, "resource_type", "");
                owner.resource_id =
                    GetStringFieldOrDefault(item, "resource_id", "");
                owner.resource_name =
                    GetStringFieldOrDefault(item, "resource_name", "");
                owner.owner_user_id =
                    GetStringFieldOrDefault(item, "owner_user_id", "");
                owner.enabled =
                    GetBoolFieldOrDefault(item, "enabled", true);
                owner.metadata_text =
                    GetStringFieldOrDefault(item, "metadata_text", "");

                snapshot.resource_owners.push_back(owner);
            }
        }

        // 最后用函数参数兜底，避免关键信息为空
        if (snapshot.meta.snapshot_id == 0) {
            snapshot.meta.snapshot_id = snapshot_id;
        }
        if (snapshot.app_info.app_code.empty()) {
            snapshot.app_info.app_code = app_code;
        }
        if (snapshot.app_info.policy_version <= 0) {
            snapshot.app_info.policy_version = policy_version;
        }
        if (snapshot.meta.published_by.empty()) {
            snapshot.meta.published_by = published_by;
        }
        if (snapshot.meta.publish_note.empty()) {
            snapshot.meta.publish_note = publish_note;
        }
        if (snapshot.meta.created_at.empty()) {
            snapshot.meta.created_at = created_at;
        }

        return snapshot;
    } catch (...) {
        return std::nullopt;
    }
}
