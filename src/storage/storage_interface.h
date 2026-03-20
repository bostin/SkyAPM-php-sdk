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
 * SkyWalking PHP 扩展 - 存储接口抽象
 *
 * 本模块定义存储接口，支持多种存储后端：
 * - SQLite: 嵌入式数据库，支持 SQL 查询
 * - JSON: 文件存储，向后兼容
 */

#ifndef SKYWALKING_STORAGE_INTERFACE_H
#define SKYWALKING_STORAGE_INTERFACE_H

#include <string>
#include <vector>

// 前向声明
class Segment;

/**
 * 存储接口抽象类
 * 定义统一的存储 API，支持多种存储后端
 */
class StorageInterface {
public:
    virtual ~StorageInterface() = default;

    /**
     * 保存追踪段数据
     * @param segment 追踪段指针
     * @return 保存成功返回 true，失败返回 false
     */
    virtual bool saveSegment(const Segment* segment) = 0;

    /**
     * 根据 traceId 查询追踪数据
     * @param traceId 追踪 ID
     * @return JSON 格式的追踪数据
     */
    virtual std::string getTraceById(const std::string& traceId) = 0;

    /**
     * 获取最慢的 URL 列表（按平均响应时间排序）
     * @param limit 返回的最大数量
     * @return JSON 格式的统计数据
     */
    virtual std::string getTopSlowUrls(int limit) = 0;

    /**
     * 清理过期数据
     * @param retentionDays 保留天数（0 = 永久保留）
     * @return 清理成功返回 true，失败返回 false
     */
    virtual bool cleanup(int retentionDays) = 0;

    /**
     * 初始化存储后端
     * @return 初始化成功返回 true，失败返回 false
     */
    virtual bool initialize() = 0;

    /**
     * 关闭存储后端
     */
    virtual void shutdown() = 0;
};

#endif // SKYWALKING_STORAGE_INTERFACE_H
