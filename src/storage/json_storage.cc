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
 */

#include "json_storage.h"
#include "segment.h"
#include "sky_log.h"
#include "php_skywalking.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <chrono>

JsonStorage::JsonStorage(const std::string& logFilePath)
    : logFilePath_(logFilePath) {
}

bool JsonStorage::initialize() {
    // JSON 文件模式无需初始化
    return true;
}

void JsonStorage::shutdown() {
    // JSON 文件模式无需关闭
}

bool JsonStorage::saveSegment(Segment* segment) {
    if (!segment) {
        return false;
    }

    // 序列化 segment 为 JSON
    std::string jsonStr = segment->marshal();

    // 写入文件
    return writeTraceToFile(jsonStr);
}

std::string JsonStorage::getTraceById(const std::string& traceId) {
    // JSON 文件模式不支持查询功能
    return "{}";
}

std::string JsonStorage::getTopSlowUrls(int limit) {
    // JSON 文件模式不支持聚合查询
    return "[]";
}

bool JsonStorage::cleanup(int retentionDays) {
    if (retentionDays <= 0) {
        return true;  // 永久保留，不清理
    }

    // 计算过期时间阈值（毫秒）
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t threshold = now - (retentionDays * 24 * 60 * 60 * 1000);

    // 打开日志目录
    DIR* dir = opendir(logFilePath_.c_str());
    if (!dir) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("JsonStorage: failed to open log directory: " + logFilePath_);
        }
        return false;
    }

    struct dirent* entry;
    int deletedCount = 0;

    while ((entry = readdir(dir)) != nullptr) {
        // 跳过 "." 和 ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // 检查文件名格式：skywalking-{timestamp}-{traceid}-{pid}.json
        if (strncmp(entry->d_name, "skywalking-", 11) != 0) {
            continue;
        }

        // 提取时间戳
        const char* timestampStart = entry->d_name + 11;
        const char* timestampEnd = strchr(timestampStart, '-');
        if (!timestampEnd) {
            continue;
        }

        std::string timestampStr(timestampStart, timestampEnd - timestampStart);
        try {
            int64_t fileTimestamp = std::stoll(timestampStr);

            // 如果文件过期，删除它
            if (fileTimestamp < threshold) {
                std::string fullPath = logFilePath_ + "/" + entry->d_name;
                if (unlink(fullPath.c_str()) == 0) {
                    deletedCount++;
                }
            }
        } catch (...) {
            // 时间戳解析失败，跳过此文件
            continue;
        }
    }

    closedir(dir);

    if (SKYWALKING_G(log_enable) && deletedCount > 0) {
        sky_log("JsonStorage: cleanup completed, deleted " + std::to_string(deletedCount) +
                " files, retention days=" + std::to_string(retentionDays));
    }

    return true;
}

bool JsonStorage::writeTraceToFile(const std::string& jsonStr) {
    // 确保日志目录存在
    mkdir(logFilePath_.c_str(), 0755);

    // 从 JSON 中提取 traceId（简单解析）
    size_t traceIdPos = jsonStr.find("\"traceId\":\"");
    std::string traceIdShort = "unknown";
    if (traceIdPos != std::string::npos) {
        size_t start = traceIdPos + 11;  // 跳过 "traceId":"
        size_t end = jsonStr.find("\"", start);
        if (end != std::string::npos && end > start) {
            size_t len = std::min(end - start, size_t(8));
            traceIdShort = jsonStr.substr(start, len);
        }
    }

    // 生成文件名: skywalking-{timestamp}-{traceid}-{pid}.json
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string filename = logFilePath_ + "/skywalking-" +
        std::to_string(now) + "-" + traceIdShort + "-" + std::to_string(getpid()) + ".json";

    // 写入文件
    std::ofstream outfile(filename, std::ios::out | std::ios::trunc);
    if (outfile.good()) {
        outfile << jsonStr;
        outfile.close();

        if (outfile.good()) {
            if (SKYWALKING_G(log_enable)) {
                sky_log("JsonStorage: write trace to file: " + filename);
            }
            return true;
        } else {
            if (SKYWALKING_G(log_enable)) {
                sky_log("JsonStorage: failed to write trace data to: " + filename);
            }
        }
    } else {
        if (SKYWALKING_G(log_enable)) {
            sky_log("JsonStorage: failed to open trace file: " + filename);
        }
    }

    return false;
}
