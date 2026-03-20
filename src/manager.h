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
 * SkyWalking PHP 扩展 - 管理器模块
 *
 * 本模块负责：
 * 1. 初始化服务信息（服务名、实例名）
 * 2. 启动后台线程，从消息队列读取追踪数据
 * 3. 将追踪数据写入本地 JSON 文件
 * 4. 管理文件轮转（按数量限制）
 */



#ifndef SKYWALKING_MANAGER_H
#define SKYWALKING_MANAGER_H

#include <string>
#include <vector>

#if (defined(unix) || defined(__unix__) || defined(__unix)) && !defined(__APPLE__)
#define PLATFORM_NAME "Unix"
#elif defined(__linux__)
#define PLATFORM_NAME "Linux"
#elif defined(__APPLE__) && defined(__MACH__)
#define PLATFORM_NAME "MacOS"
#elif defined(__FreeBSD__)
#define PLATFORM_NAME "FreeBSD"
#else
#define PLATFORM_NAME ""
#endif

// 管理器配置选项
struct ManagerOptions {
    int version;              // SkyWalking 协议版本（用于 SW8 header 生成）
    std::string code;         // 应用代码/名称
    std::string log_file_path; // 追踪数据文件存储路径
    int log_file_max_size;    // 单个追踪日志文件的最大大小（字节）
    int log_file_max_files;   // 保留的最大日志文件数量
    std::string instance_name; // 实例名称（留空则自动生成 UUID@IP 格式）
};

class Manager {

public:
    static std::string generateUUID();

    static void init(const ManagerOptions &options, struct service_info *info);

private:
    Manager() = delete;

    static void setupServiceInfo(const ManagerOptions &options, struct service_info *info);

    [[noreturn]] static void consumer(const ManagerOptions &options);

    static void logger(const std::string &log);

    static std::vector<std::string> getIps();
};


#endif //SKYWALKING_MANAGER_H
