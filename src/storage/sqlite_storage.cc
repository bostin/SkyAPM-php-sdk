/*
 * Copyright 2021 SkyAPM
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * SkyWalking PHP 扩展 - SQLite 存储实现
 */

#ifdef HAVE_SQLITE3

#include "sqlite_storage.h"
#include "segment.h"
#include "span.h"
#include "sky_log.h"
#include "php_skywalking.h"
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

SQLiteStorage::SQLiteStorage(const std::string& dbPath)
    : db_(nullptr), dbPath_(dbPath), initialized_(false) {
}

SQLiteStorage::~SQLiteStorage() {
    shutdown();
}

bool SQLiteStorage::initialize() {
    if (initialized_) {
        return true;
    }

    // 确保数据库目录存在
    size_t lastSlash = dbPath_.find_last_of('/');
    if (lastSlash != std::string::npos) {
        std::string dbDir = dbPath_.substr(0, lastSlash);
        mkdir(dbDir.c_str(), 0755);
    }

    // 打开数据库连接
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SQLiteStorage: failed to open database: " + dbPath_);
        }
        db_ = nullptr;
        return false;
    }

    // 配置 SQLite 优化选项
    // WAL 模式提高并发性能
    execute("PRAGMA journal_mode=WAL;");
    // 设置忙超时为 5 秒
    execute("PRAGMA busy_timeout=5000;");
    // 同步模式设置为 NORMAL（性能与安全的平衡）
    execute("PRAGMA synchronous=NORMAL;");
    // 缓存大小设置为 10MB
    execute("PRAGMA cache_size=-10000;");

    // 初始化表结构
    if (!initializeSchema()) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    initialized_ = true;

    if (SKYWALKING_G(log_enable)) {
        sky_log("SQLiteStorage: initialized successfully: " + dbPath_);
    }

    return true;
}

bool SQLiteStorage::initializeSchema() {
    const char* createSegmentsTable = R"(
        CREATE TABLE IF NOT EXISTS trace_segments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            trace_id TEXT NOT NULL,
            service TEXT NOT NULL,
            service_instance TEXT NOT NULL,
            start_time INTEGER NOT NULL,
            end_time INTEGER NOT NULL,
            created_at INTEGER DEFAULT (strftime('%s', 'now') * 1000)
        );
    )";

    const char* createSpansTable = R"(
        CREATE TABLE IF NOT EXISTS spans (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            segment_id INTEGER NOT NULL,
            trace_id TEXT NOT NULL,
            span_id INTEGER NOT NULL,
            parent_span_id INTEGER,
            operation_name TEXT NOT NULL,
            start_time INTEGER NOT NULL,
            end_time INTEGER NOT NULL,
            duration INTEGER NOT NULL,
            span_type INTEGER,
            span_layer INTEGER,
            component_id INTEGER,
            is_error BOOLEAN DEFAULT 0,
            peer TEXT,
            FOREIGN KEY (segment_id) REFERENCES trace_segments(id)
        );
    )";

    const char* createTagsTable = R"(
        CREATE TABLE IF NOT EXISTS span_tags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            span_id INTEGER NOT NULL,
            key TEXT NOT NULL,
            value TEXT,
            FOREIGN KEY (span_id) REFERENCES spans(id)
        );
    )";

    // 创建索引以提高查询性能
    const char* createIndexes = R"(
        CREATE INDEX IF NOT EXISTS idx_trace_id ON spans(trace_id);
        CREATE INDEX IF NOT EXISTS idx_operation_name ON spans(operation_name);
        CREATE INDEX IF NOT EXISTS idx_duration ON spans(duration);
        CREATE INDEX IF NOT EXISTS idx_start_time ON spans(start_time);
        CREATE INDEX IF NOT EXISTS idx_segment_created_at ON trace_segments(created_at);
    )";

    if (!execute(createSegmentsTable) ||
        !execute(createSpansTable) ||
        !execute(createTagsTable) ||
        !execute(createIndexes)) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SQLiteStorage: failed to initialize database schema");
        }
        return false;
    }

    return true;
}

bool SQLiteStorage::execute(const std::string& sql) {
    if (!db_) {
        return false;
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SQLiteStorage: SQL error: " + std::string(errMsg ? errMsg : "unknown"));
        }
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return false;
    }

    return true;
}

bool SQLiteStorage::beginTransaction() {
    return execute("BEGIN TRANSACTION;");
}

bool SQLiteStorage::commitTransaction() {
    return execute("COMMIT;");
}

void SQLiteStorage::rollbackTransaction() {
    execute("ROLLBACK;");
}

bool SQLiteStorage::saveSegment(Segment* segment) {
    if (!initialized_ || !db_ || !segment) {
        return false;
    }

    // 开始事务
    if (!beginTransaction()) {
        return false;
    }

    try {
        // 计算时间范围
        long minStartTime = 0;
        long maxEndTime = 0;

        const auto& spans = segment->getSpans();
        if (!spans.empty()) {
            minStartTime = spans[0]->getStartTime();
            maxEndTime = spans[0]->getEndTime();

            for (const auto* span : spans) {
                if (span->getStartTime() < minStartTime) {
                    minStartTime = span->getStartTime();
                }
                if (span->getEndTime() > maxEndTime) {
                    maxEndTime = span->getEndTime();
                }
            }
        }

        // 插入 trace_segments 表
        std::ostringstream segmentSql;
        segmentSql << "INSERT INTO trace_segments (trace_id, service, service_instance, start_time, end_time) "
                   << "VALUES ('"
                   << segment->getTraceId() << "', '"
                   << segment->getServiceId() << "', '"
                   << segment->getServiceInstanceId() << "', "
                   << minStartTime << ", "
                   << maxEndTime << ");";

        if (!execute(segmentSql.str())) {
            rollbackTransaction();
            return false;
        }

        // 获取刚插入的 segment_id
        sqlite3_int64 segmentId = sqlite3_last_insert_rowid(db_);

        // 插入 spans 表
        for (const auto* span : spans) {
            std::ostringstream spanSql;
            spanSql << "INSERT INTO spans (segment_id, trace_id, span_id, parent_span_id, "
                     << "operation_name, start_time, end_time, duration, "
                     << "span_type, span_layer, component_id, is_error, peer) VALUES ("
                     << segmentId << ", '"
                     << segment->getTraceId() << "', "
                     << span->getSpanId() << ", ";

            // parent_span_id 可能为 -1（表示无父 span）
            if (span->getParentSpanId() >= 0) {
                spanSql << span->getParentSpanId() << ", ";
            } else {
                spanSql << "NULL, ";
            }

            // 转义 SQL 字符串中的单引号
            std::string opName = span->getOperationName();
            std::string escapedOpName;
            for (char c : opName) {
                if (c == '\'') {
                    escapedOpName += "''";
                } else {
                    escapedOpName += c;
                }
            }

            spanSql << "'" << escapedOpName << "', "
                    << span->getStartTime() << ", "
                    << span->getEndTime() << ", "
                    << (span->getEndTime() - span->getStartTime()) << ", "
                    << static_cast<int>(span->getSpanType()) << ", "
                    << static_cast<int>(span->getSpanLayer()) << ", "
                    << span->getComponentId() << ", "
                    << (span->getIsError() ? 1 : 0) << ", '"
                    << span->getPeer() << "');";

            if (!execute(spanSql.str())) {
                rollbackTransaction();
                return false;
            }

            // 获取刚插入的 span_id
            sqlite3_int64 spanId = sqlite3_last_insert_rowid(db_);

            // 插入 tags 表
            for (const auto* tag : span->getTags()) {
                std::ostringstream tagSql;
                tagSql << "INSERT INTO span_tags (span_id, key, value) VALUES ("
                       << spanId << ", '"
                       << tag->getKey() << "', '"
                       << tag->getValue() << "');";

                if (!execute(tagSql.str())) {
                    rollbackTransaction();
                    return false;
                }
            }
        }

        // 提交事务
        if (!commitTransaction()) {
            rollbackTransaction();
            return false;
        }

        return true;

    } catch (...) {
        rollbackTransaction();
        return false;
    }
}

std::string SQLiteStorage::getTraceById(const std::string& traceId) {
    if (!initialized_ || !db_) {
        return "{}";
    }

    std::ostringstream sql;
    sql << "SELECT span_id, parent_span_id, operation_name, start_time, end_time, duration, peer "
        << "FROM spans WHERE trace_id = '" << traceId << "' "
        << "ORDER BY start_time;";

    // TODO: 执行查询并返回 JSON 格式结果

    return "{}";
}

std::string SQLiteStorage::getTopSlowUrls(int limit) {
    if (!initialized_ || !db_) {
        return "[]";
    }

    std::ostringstream sql;
    sql << "SELECT operation_name, AVG(duration) as avg_duration, COUNT(*) as count "
        << "FROM spans "
        << "GROUP BY operation_name "
        << "ORDER BY avg_duration DESC "
        << "LIMIT " << limit << ";";

    // TODO: 执行查询并返回 JSON 格式结果

    return "[]";
}

bool SQLiteStorage::cleanup(int retentionDays) {
    if (!initialized_ || !db_ || retentionDays <= 0) {
        return true;  // retentionDays = 0 表示永久保留
    }

    // 计算过期时间阈值（毫秒）
    sqlite3_int64 threshold = retentionDays * 24 * 60 * 60 * 1000;

    std::ostringstream sql;
    sql << "DELETE FROM spans WHERE segment_id IN ("
        << "SELECT id FROM trace_segments WHERE created_at < "
        << "(strftime('%s', 'now') * 1000 - " << threshold << "));";

    if (!execute(sql.str())) {
        return false;
    }

    sql.str("");
    sql.clear();
    sql << "DELETE FROM trace_segments WHERE created_at < "
        << "(strftime('%s', 'now') * 1000 - " << threshold << ");";

    if (!execute(sql.str())) {
        return false;
    }

    // 清理数据库文件空间（VACUUM）
    execute("VACUUM;");

    if (SKYWALKING_G(log_enable)) {
        sky_log("SQLiteStorage: cleanup completed, retention days=" + std::to_string(retentionDays));
    }

    return true;
}

void SQLiteStorage::shutdown() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    initialized_ = false;
}

#endif // HAVE_SQLITE3
