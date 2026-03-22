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

#include "sqlite_storage.h"
#include "segment.h"
#include "sky_log.h"
#include "php_skywalking.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/file.h>
#include <chrono>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <thread>

// 进程级标志：记录是否已打印过初始化日志（跨 shutdown/initialize 周期）
static std::atomic<pid_t> s_init_logged_pid(0);

SqliteStorage::SqliteStorage(const std::string& dbPath, bool walMode, int maxSizeMB)
    : dbPath_(dbPath)
    , walMode_(walMode)
    , maxSizeMB_(maxSizeMB)
    , db_(nullptr)
    , stmtInsert_(nullptr)
    , stmtSelectByTraceId_(nullptr)
    , stmtSelectTopSlow_(nullptr)
    , stmtDeleteOld_(nullptr)
    , stmtCount_(nullptr)
    , stmtMinMaxTime_(nullptr)
    , totalTraces_(0)
    , lastCleanupTime_(0)
    , lastCompactionTime_(0)
    , initialized_(false) {
}

SqliteStorage::~SqliteStorage() {
    shutdown();
}

bool SqliteStorage::initialize() {
    std::lock_guard<std::mutex> lock(dbMutex_);

    // 防止重复初始化
    if (initialized_) {
        return true;
    }

    // 打开或创建数据库
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to open database: " + dbPath_ +
                    ", error: " + sqlite3_errmsg(db_));
        }
        return false;
    }

    // 设置超时
    sqlite3_busy_timeout(db_, 5000);

    // 启用 WAL 模式（如果需要）
    if (walMode_) {
        char* errMsg = nullptr;
        rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK && errMsg) {
            if (SKYWALKING_G(log_enable)) {
                sky_log("SqliteStorage: failed to set WAL mode: " + std::string(errMsg));
            }
            sqlite3_free(errMsg);
        }

        // 设置 WAL checkpoint 大小
        rc = sqlite3_exec(db_, "PRAGMA wal_autocheckpoint=1000;", nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK && errMsg) {
            sqlite3_free(errMsg);
        }
    }

    // 设置同步模式为 NORMAL
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // 设置缓存大小（-64000 表示 64MB）
    sqlite3_exec(db_, "PRAGMA cache_size=-64000;", nullptr, nullptr, nullptr);

    // 初始化表
    if (!initTables()) {
        return false;
    }

    // 准备语句
    if (!prepareStatements()) {
        return false;
    }

    // 获取当前记录数
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM traces", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalTraces_ = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // 使用进程级标志确保整个进程内只打印一次初始化日志
    // 防止 shutdown() 后重新 initialize() 导致重复日志
    pid_t current_pid = getpid();
    pid_t expected_pid = 0;
    if (s_init_logged_pid.compare_exchange_strong(expected_pid, current_pid)) {
        // 只有成功获取标志的进程才打印日志
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: initialized, database=" + dbPath_ +
                    ", traces=" + std::to_string(totalTraces_.load()));
        }
    }
    initialized_ = true;

    return true;
}

void SqliteStorage::shutdown() {
    std::lock_guard<std::mutex> lock(dbMutex_);

    finalizeStatements();

    if (db_) {
        // 执行最终的 checkpoint
        sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
        sqlite3_close(db_);
        db_ = nullptr;
    }

    // 重置初始化标志，允许重新初始化
    initialized_ = false;
}

bool SqliteStorage::initTables() {
    const char* createTableSQL =
        "CREATE TABLE IF NOT EXISTS traces ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    trace_id TEXT NOT NULL UNIQUE,"
        "    trace_segment_id TEXT NOT NULL,"
        "    service TEXT NOT NULL,"
        "    service_instance TEXT NOT NULL,"
        "    start_time INTEGER NOT NULL,"
        "    end_time INTEGER NOT NULL,"
        "    duration_ms INTEGER NOT NULL,"
        "    status_code INTEGER DEFAULT 200,"
        "    is_error INTEGER DEFAULT 0,"
        "    url TEXT,"
        "    full_json TEXT NOT NULL,"
        "    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)"
        ");";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, createTableSQL, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable) && errMsg) {
            sky_log("SqliteStorage: failed to create table: " + std::string(errMsg));
        }
        sqlite3_free(errMsg);
        return false;
    }

    // 创建索引
    const char* indexes[] = {
        "CREATE INDEX IF NOT EXISTS idx_trace_id ON traces(trace_id);",
        "CREATE INDEX IF NOT EXISTS idx_start_time ON traces(start_time DESC);",
        "CREATE INDEX IF NOT EXISTS idx_duration ON traces(duration_ms DESC);",
        "CREATE INDEX IF NOT EXISTS idx_url ON traces(url);",
        "CREATE INDEX IF NOT EXISTS idx_service ON traces(service);"
    };

    for (const char* idxSql : indexes) {
        rc = sqlite3_exec(db_, idxSql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK && errMsg) {
            sqlite3_free(errMsg);
        }
    }

    return true;
}

bool SqliteStorage::prepareStatements() {
    const char* insertSQL =
        "INSERT OR REPLACE INTO traces "
        "(trace_id, trace_segment_id, service, service_instance, "
        "start_time, end_time, duration_ms, status_code, is_error, url, full_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(db_, insertSQL, -1, &stmtInsert_, nullptr);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to prepare insert statement");
        }
        return false;
    }

    const char* selectSQL = "SELECT full_json FROM traces WHERE trace_id = ?;";
    rc = sqlite3_prepare_v2(db_, selectSQL, -1, &stmtSelectByTraceId_, nullptr);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to prepare select statement");
        }
        return false;
    }

    const char* topSlowSQL =
        "SELECT url, AVG(duration_ms) as avg_duration, COUNT(*) as count "
        "FROM traces WHERE url IS NOT NULL AND url != '' "
        "GROUP BY url ORDER BY avg_duration DESC LIMIT ?;";

    rc = sqlite3_prepare_v2(db_, topSlowSQL, -1, &stmtSelectTopSlow_, nullptr);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to prepare top slow URL statement");
        }
        return false;
    }

    const char* deleteOldSQL = "DELETE FROM traces WHERE start_time < ?;";
    rc = sqlite3_prepare_v2(db_, deleteOldSQL, -1, &stmtDeleteOld_, nullptr);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to prepare delete statement");
        }
        return false;
    }

    const char* countSQL = "SELECT COUNT(*) FROM traces;";
    rc = sqlite3_prepare_v2(db_, countSQL, -1, &stmtCount_, nullptr);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to prepare count statement");
        }
        return false;
    }

    const char* minMaxSQL = "SELECT MIN(start_time), MAX(start_time) FROM traces;";
    rc = sqlite3_prepare_v2(db_, minMaxSQL, -1, &stmtMinMaxTime_, nullptr);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to prepare minmax statement");
        }
        return false;
    }

    return true;
}

void SqliteStorage::finalizeStatements() {
    if (stmtInsert_) {
        sqlite3_finalize(stmtInsert_);
        stmtInsert_ = nullptr;
    }
    if (stmtSelectByTraceId_) {
        sqlite3_finalize(stmtSelectByTraceId_);
        stmtSelectByTraceId_ = nullptr;
    }
    if (stmtSelectTopSlow_) {
        sqlite3_finalize(stmtSelectTopSlow_);
        stmtSelectTopSlow_ = nullptr;
    }
    if (stmtDeleteOld_) {
        sqlite3_finalize(stmtDeleteOld_);
        stmtDeleteOld_ = nullptr;
    }
    if (stmtCount_) {
        sqlite3_finalize(stmtCount_);
        stmtCount_ = nullptr;
    }
    if (stmtMinMaxTime_) {
        sqlite3_finalize(stmtMinMaxTime_);
        stmtMinMaxTime_ = nullptr;
    }
}

bool SqliteStorage::saveSegment(Segment* segment) {
    if (!segment) {
        return false;
    }

    // 序列化 segment 为 JSON
    std::string jsonStr = segment->marshal();
    if (jsonStr.empty()) {
        return false;
    }

    // 提取元数据
    std::string traceId = segment->getTraceId();
    std::string traceSegmentId = segment->getTraceId();  // 使用 traceId 作为 segmentId
    std::string service = segment->getServiceId();
    std::string serviceInstance = segment->getServiceInstanceId();
    std::string url = extractUrl(segment);
    int statusCode = extractStatusCode(segment);
    bool isError = extractIsError(segment);
    int64_t startTime, endTime;
    getTimeInfo(segment, startTime, endTime);
    int64_t durationMs = endTime - startTime;

    std::lock_guard<std::mutex> lock(dbMutex_);

    if (!db_ || !stmtInsert_) {
        return false;
    }

    // 重置语句并绑定参数
    sqlite3_reset(stmtInsert_);
    sqlite3_clear_bindings(stmtInsert_);

    sqlite3_bind_text(stmtInsert_, 1, traceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsert_, 2, traceSegmentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsert_, 3, service.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsert_, 4, serviceInstance.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmtInsert_, 5, startTime);
    sqlite3_bind_int64(stmtInsert_, 6, endTime);
    sqlite3_bind_int64(stmtInsert_, 7, durationMs);
    sqlite3_bind_int(stmtInsert_, 8, statusCode);
    sqlite3_bind_int(stmtInsert_, 9, isError ? 1 : 0);

    if (url.empty()) {
        sqlite3_bind_null(stmtInsert_, 10);
    } else {
        sqlite3_bind_text(stmtInsert_, 10, url.c_str(), -1, SQLITE_TRANSIENT);
    }

    sqlite3_bind_text(stmtInsert_, 11, jsonStr.c_str(), -1, SQLITE_TRANSIENT);

    // 执行插入
    int rc = sqlite3_step(stmtInsert_);
    if (rc != SQLITE_DONE) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to insert trace: " + traceId +
                    ", error code: " + std::to_string(rc));
        }
        return false;
    }

    totalTraces_++;

    // 检查是否需要自动清理
    if (maxSizeMB_ > 0 && needsCompaction()) {
        checkpoint();
    }

    return true;
}

std::string SqliteStorage::getTraceById(const std::string& traceId) {
    std::lock_guard<std::mutex> lock(dbMutex_);

    if (!db_ || !stmtSelectByTraceId_) {
        return "{}";
    }

    sqlite3_reset(stmtSelectByTraceId_);
    sqlite3_clear_bindings(stmtSelectByTraceId_);
    sqlite3_bind_text(stmtSelectByTraceId_, 1, traceId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmtSelectByTraceId_) == SQLITE_ROW) {
        const unsigned char* json = sqlite3_column_text(stmtSelectByTraceId_, 0);
        if (json) {
            return std::string(reinterpret_cast<const char*>(json));
        }
    }

    return "{}";
}

std::string SqliteStorage::getTopSlowUrls(int limit) {
    std::lock_guard<std::mutex> lock(dbMutex_);

    if (!db_ || !stmtSelectTopSlow_) {
        return "[]";
    }

    sqlite3_reset(stmtSelectTopSlow_);
    sqlite3_clear_bindings(stmtSelectTopSlow_);
    sqlite3_bind_int(stmtSelectTopSlow_, 1, limit);

    std::ostringstream result;
    result << "[";

    bool first = true;
    while (sqlite3_step(stmtSelectTopSlow_) == SQLITE_ROW) {
        if (!first) {
            result << ",";
        }
        first = false;

        const unsigned char* url = sqlite3_column_text(stmtSelectTopSlow_, 0);
        double avgDuration = sqlite3_column_double(stmtSelectTopSlow_, 1);
        int count = sqlite3_column_int(stmtSelectTopSlow_, 2);

        result << "{\"url\":\"";
        if (url) {
            // 转义 JSON 字符串
            std::string urlStr(reinterpret_cast<const char*>(url));
            for (char c : urlStr) {
                if (c == '"') result << "\\\"";
                else if (c == '\\') result << "\\\\";
                else result << c;
            }
        }
        result << "\",\"avg_duration\":" << avgDuration
               << ",\"count\":" << count << "}";
    }

    result << "]";
    return result.str();
}

bool SqliteStorage::cleanup(int retentionDays) {
    if (retentionDays <= 0) {
        return true;
    }

    std::lock_guard<std::mutex> lock(dbMutex_);

    if (!db_) {
        return false;
    }

    // 计算过期时间阈值（毫秒）
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t threshold = now - (retentionDays * 24 * 60 * 60 * 1000LL);

    // 直接执行 DELETE 语句
    sqlite3_stmt* stmtDel;
    const char* deleteSQL = "DELETE FROM traces WHERE start_time < ?;";
    int rc = sqlite3_prepare_v2(db_, deleteSQL, -1, &stmtDel, nullptr);
    if (rc != SQLITE_OK) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: failed to prepare delete statement");
        }
        return false;
    }

    sqlite3_bind_int64(stmtDel, 1, threshold);
    rc = sqlite3_step(stmtDel);
    sqlite3_finalize(stmtDel);

    // 获取删除的行数（SQLite 不直接返回这个，我们需要重新查询）
    // 使用一个简单的方式：获取当前总数
    if (stmtCount_ && sqlite3_step(stmtCount_) == SQLITE_ROW) {
        int64_t newCount = sqlite3_column_int64(stmtCount_, 0);
        int64_t deleted = totalTraces_.load() - newCount;
        totalTraces_ = newCount;
        sqlite3_reset(stmtCount_);

        if (SKYWALKING_G(log_enable) && deleted > 0) {
            sky_log("SqliteStorage: cleanup completed, deleted " + std::to_string(deleted) +
                    " traces, retention_days=" + std::to_string(retentionDays));
        }
    }

    lastCleanupTime_ = now;

    return true;
}

std::string SqliteStorage::getStats() {
    std::lock_guard<std::mutex> lock(dbMutex_);

    std::ostringstream result;
    result << "{\"traces_count\":" << totalTraces_.load()
           << ",\"db_size_bytes\":" << getDbFileSize();

    if (db_ && stmtMinMaxTime_) {
        sqlite3_reset(stmtMinMaxTime_);
        if (sqlite3_step(stmtMinMaxTime_) == SQLITE_ROW) {
            int64_t minTime = sqlite3_column_type(stmtMinMaxTime_, 0) == SQLITE_NULL ?
                              0 : sqlite3_column_int64(stmtMinMaxTime_, 0);
            int64_t maxTime = sqlite3_column_type(stmtMinMaxTime_, 1) == SQLITE_NULL ?
                              0 : sqlite3_column_int64(stmtMinMaxTime_, 1);
            result << ",\"oldest_trace\":" << minTime
                   << ",\"newest_trace\":" << maxTime;
        }
        sqlite3_reset(stmtMinMaxTime_);
    }

    result << "}";
    return result.str();
}

bool SqliteStorage::needsCompaction() {
    if (maxSizeMB_ <= 0) {
        return false;
    }

    int64_t size = getDbFileSize();
    int64_t maxSizeBytes = static_cast<int64_t>(maxSizeMB_) * 1024 * 1024;

    // 如果文件超过限制的 80%，触发清理
    return size > maxSizeBytes * 0.8;
}

bool SqliteStorage::compact() {
    std::lock_guard<std::mutex> lock(dbMutex_);

    if (!db_) {
        return false;
    }

    // 执行 VACUUM
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, "VACUUM;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK && errMsg) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("SqliteStorage: VACUUM failed: " + std::string(errMsg));
        }
        sqlite3_free(errMsg);
        return false;
    }

    lastCompactionTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (SKYWALKING_G(log_enable)) {
        sky_log("SqliteStorage: VACUUM completed, new size=" +
                std::to_string(getDbFileSize()));
    }

    return true;
}

bool SqliteStorage::checkpoint() {
    if (!db_) {
        return false;
    }

#ifndef SQLITE_CHECKPOINT_TRUNCATE
#define SQLITE_CHECKPOINT_TRUNCATE 3
#endif
    int rc = sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_TRUNCATE,
                                       nullptr, nullptr);

    if (rc != SQLITE_OK && SKYWALKING_G(log_enable)) {
        sky_log("SqliteStorage: checkpoint failed, error code=" + std::to_string(rc));
        return false;
    }

    return true;
}

std::string SqliteStorage::extractUrl(Segment* segment) {
    if (!segment) {
        return "";
    }

    const auto& spans = segment->getSpans();
    if (spans.empty()) {
        return "";
    }

    // 优先返回 Entry span 的 operation name（入口请求的 URL）
    // Entry span 代表 PHP 应用收到的原始请求，应该作为显示的 URL
    for (const auto* span : spans) {
        if (span && span->getSpanType() == SkySpanType::Entry) {
            std::string operationName = span->getOperationName();
            if (!operationName.empty() && operationName != "/") {
                return operationName;
            }
        }
    }

    // 如果没有找到合适的 Entry span，返回第一个 span 的 operation name
    // 这通常是最外层的 span，可能包含有意义的路径信息
    return spans[0]->getOperationName();
}

int SqliteStorage::extractStatusCode(Segment* segment) {
    if (!segment) {
        return 200;
    }

    const auto& spans = segment->getSpans();
    for (const auto* span : spans) {
        if (span) {
            const auto& tags = span->getTags();
            for (const auto* tag : tags) {
                if (tag && tag->getKey() == "status_code") {
                    try {
                        return std::stoi(tag->getValue());
                    } catch (...) {
                        return 200;
                    }
                }
            }
        }
    }

    return 200;
}

bool SqliteStorage::extractIsError(Segment* segment) {
    if (!segment) {
        return false;
    }

    const auto& spans = segment->getSpans();
    for (const auto* span : spans) {
        if (span && span->getIsError()) {
            return true;
        }
    }

    return false;
}

void SqliteStorage::getTimeInfo(Segment* segment, int64_t& startTime, int64_t& endTime) {
    startTime = 0;
    endTime = 0;

    if (!segment) {
        return;
    }

    const auto& spans = segment->getSpans();
    if (spans.empty()) {
        return;
    }

    startTime = spans[0]->getStartTime();
    endTime = spans[0]->getEndTime();

    for (const auto* span : spans) {
        if (span) {
            if (span->getStartTime() < startTime) {
                startTime = span->getStartTime();
            }
            if (span->getEndTime() > endTime) {
                endTime = span->getEndTime();
            }
        }
    }
}

int64_t SqliteStorage::getDbFileSize() {
    struct stat st;
    if (stat(dbPath_.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
}
