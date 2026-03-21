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
 *
 * 基于 SQLite 的追踪数据存储，提供 SQL 查询能力和更好的性能
 */

#ifndef SKYWALKING_SQLITE_STORAGE_H
#define SKYWALKING_SQLITE_STORAGE_H

#include "storage_interface.h"
#include <string>
#include <sqlite3.h>
#include <mutex>
#include <atomic>

/**
 * SQLite 存储实现类
 * 将追踪数据存储到 SQLite 数据库文件
 */
class SqliteStorage : public StorageInterface {
public:
    /**
     * 构造函数
     * @param dbPath SQLite 数据库文件路径
     * @param walMode 是否启用 WAL 模式（默认启用）
     * @param maxSizeMB 数据库文件最大大小（MB，0 = 不限制）
     */
    explicit SqliteStorage(const std::string& dbPath, bool walMode = true, int maxSizeMB = 0);

    /**
     * 析构函数
     */
    ~SqliteStorage() override;

    /**
     * 保存追踪段数据
     * @param segment 追踪段指针
     * @return 保存成功返回 true，失败返回 false
     */
    bool saveSegment(Segment* segment) override;

    /**
     * 根据 traceId 查询追踪数据
     * @param traceId 追踪 ID
     * @return JSON 格式的追踪数据
     */
    std::string getTraceById(const std::string& traceId) override;

    /**
     * 获取最慢的 URL 列表（按平均响应时间排序）
     * @param limit 返回的最大数量
     * @return JSON 格式的统计数据
     */
    std::string getTopSlowUrls(int limit) override;

    /**
     * 清理过期数据
     * @param retentionDays 保留天数（0 = 永久保留）
     * @return 清理成功返回 true，失败返回 false
     */
    bool cleanup(int retentionDays) override;

    /**
     * 初始化存储后端
     * @return 初始化成功返回 true，失败返回 false
     */
    bool initialize() override;

    /**
     * 关闭存储后端
     */
    void shutdown() override;

    /**
     * 获取数据库统计信息
     * @return JSON 格式的统计信息
     */
    std::string getStats();

    /**
     * 检查是否需要执行清理（基于 maxSizeMB 配置）
     * @return 需要清理返回 true
     */
    bool needsCompaction();

    /**
     * 执行数据库压缩（VACUUM）
     * @return 成功返回 true
     */
    bool compact();

private:
    std::string dbPath_;           // 数据库文件路径
    bool walMode_;                 // 是否启用 WAL 模式
    int maxSizeMB_;                // 最大文件大小（MB）
    sqlite3* db_;                  // 数据库连接
    std::mutex dbMutex_;           // 写锁

    // Prepared statements
    sqlite3_stmt* stmtInsert_;
    sqlite3_stmt* stmtSelectByTraceId_;
    sqlite3_stmt* stmtSelectTopSlow_;
    sqlite3_stmt* stmtDeleteOld_;
    sqlite3_stmt* stmtCount_;
    sqlite3_stmt* stmtMinMaxTime_;

    std::atomic<int64_t> totalTraces_;
    int64_t lastCleanupTime_;
    int64_t lastCompactionTime_;

    /**
     * 初始化数据库表
     * @return 成功返回 true
     */
    bool initTables();

    /**
     * 准备 SQL 语句
     */
    bool prepareStatements();

    /**
     * 释放 SQL 语句
     */
    void finalizeStatements();

    /**
     * 从 Segment 提取 URL
     */
    std::string extractUrl(Segment* segment);

    /**
     * 从 Segment 提取状态码
     */
    int extractStatusCode(Segment* segment);

    /**
     * 从 Segment 检查是否有错误
     */
    bool extractIsError(Segment* segment);

    /**
     * 从 Segment 获取时间信息
     */
    void getTimeInfo(Segment* segment, int64_t& startTime, int64_t& endTime);

    /**
     * 获取数据库文件大小（字节）
     */
    int64_t getDbFileSize();

    /**
     * 执行 WAL checkpoint
     */
    bool checkpoint();
};

#endif // SKYWALKING_SQLITE_STORAGE_H
