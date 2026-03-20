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
 * 本模块实现基于 SQLite 的追踪数据存储：
 * - 支持事务批量写入
 * - WAL 模式提高并发性能
 * - 自动数据清理
 */

#ifndef SKYWALKING_SQLITE_STORAGE_H
#define SKYWALKING_SQLITE_STORAGE_H

#include "storage_interface.h"
#include <string>

#ifdef HAVE_SQLITE3
#include <sqlite3.h>

/**
 * SQLite 存储实现类
 * 使用 SQLite 嵌入式数据库存储追踪数据
 */
class SQLiteStorage : public StorageInterface {
public:
    /**
     * 构造函数
     * @param dbPath 数据库文件路径
     */
    explicit SQLiteStorage(const std::string& dbPath);

    /**
     * 析构函数 - 自动关闭数据库连接
     */
    ~SQLiteStorage() override;

    /**
     * 保存追踪段数据
     * @param segment 追踪段指针
     * @return 保存成功返回 true，失败返回 false
     */
    bool saveSegment(const Segment* segment) override;

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

private:
    sqlite3* db_;                    // 数据库连接指针
    std::string dbPath_;             // 数据库文件路径
    bool initialized_;               // 是否已初始化

    /**
     * 初始化数据库表结构
     * @return 成功返回 true，失败返回 false
     */
    bool initializeSchema();

    /**
     * 执行 SQL 语句（无返回结果）
     * @param sql SQL 语句
     * @return 成功返回 true，失败返回 false
     */
    bool execute(const std::string& sql);

    /**
     * 开始事务
     * @return 成功返回 true，失败返回 false
     */
    bool beginTransaction();

    /**
     * 提交事务
     * @return 成功返回 true，失败返回 false
     */
    bool commitTransaction();

    /**
     * 回滚事务
     */
    void rollbackTransaction();
};

#endif // HAVE_SQLITE3

#endif // SKYWALKING_SQLITE_STORAGE_H
