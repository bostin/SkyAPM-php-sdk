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
 * SkyWalking PHP 扩展 - JSON 文件存储实现
 *
 * 本模块实现基于 JSON 文件的追踪数据存储（向后兼容）
 */

#ifndef SKYWALKING_JSON_STORAGE_H
#define SKYWALKING_JSON_STORAGE_H

#include "storage_interface.h"
#include <string>

/**
 * JSON 文件存储实现类
 * 将追踪数据以 JSON 文件形式存储到文件系统
 */
class JsonStorage : public StorageInterface {
public:
    /**
     * 构造函数
     * @param logFilePath 追踪文件存储路径
     */
    explicit JsonStorage(const std::string& logFilePath);

    /**
     * 析构函数
     */
    ~JsonStorage() override = default;

    /**
     * 保存追踪段数据
     * @param segment 追踪段指针
     * @return 保存成功返回 true，失败返回 false
     */
    bool saveSegment(Segment* segment) override;

    /**
     * 根据 traceId 查询追踪数据（JSON 文件模式不支持，返回空 JSON）
     * @param traceId 追踪 ID
     * @return 空的 JSON 对象
     */
    std::string getTraceById(const std::string& traceId) override;

    /**
     * 获取最慢的 URL 列表（JSON 文件模式不支持，返回空数组）
     * @param limit 返回的最大数量
     * @return 空的 JSON 数组
     */
    std::string getTopSlowUrls(int limit) override;

    /**
     * 清理过期数据（删除旧文件）
     * @param retentionDays 保留天数（0 = 永久保留）
     * @return 清理成功返回 true，失败返回 false
     */
    bool cleanup(int retentionDays) override;

    /**
     * 初始化存储后端（JSON 文件模式无需初始化）
     * @return 始终返回 true
     */
    bool initialize() override;

    /**
     * 关闭存储后端（JSON 文件模式无需关闭）
     */
    void shutdown() override;

private:
    std::string logFilePath_;  // 追踪文件存储路径

    /**
     * 写入追踪数据到文件
     * @param jsonStr JSON 格式的追踪数据
     * @return 成功返回 true，失败返回 false
     */
    bool writeTraceToFile(const std::string& jsonStr);
};

#endif // SKYWALKING_JSON_STORAGE_H
