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
 * SkyWalking PHP 扩展 - 管理器实现
 *
 * 本文件实现了：
 * 1. 服务信息初始化（服务名、实例名、IP 地址获取）
 * 2. UUID 生成器
 * 3. 后台消费者线程（从消息队列读取追踪数据并写入文件）
 * 4. 文件轮转管理
 */



#include "manager.h"
#include <thread>
#include <iostream>
#include <string>
#include <zconf.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sstream>
#include <random>
#include <fstream>
#include <queue>
#include <sys/stat.h>
#include <chrono>
#include "segment.h"
#include "common.h"
#include "sky_shm.h"
#include <boost/interprocess/ipc/message_queue.hpp>

#include "php_skywalking.h"
#include "sky_log.h"

extern struct service_info *s_info;

static std::string fixed_uuid;

// 设置服务信息（服务名、实例名）
// @param options 配置选项
// @param info 服务信息结构体指针
void Manager::setupServiceInfo(const ManagerOptions &options, struct service_info *info) {
    if (info == nullptr) {
        return;
    }

    auto ips = getIps();
    std::string instance;
    if (!ips.empty()) {
        if (!options.instance_name.empty()) {
            // 使用用户指定的实例名
            instance = options.instance_name;
        } else {
            // 自动生成实例名：UUID@IP
            instance = generateUUID() + "@" + ips[0];
        }
    }

    strcpy(info->service, options.code.c_str());
    strcpy(info->service_instance, instance.c_str());
}

// 初始化管理器
// @param options 配置选项
// @param info 服务信息结构体指针
void Manager::init(const ManagerOptions &options, struct service_info *info) {
    if (!options.instance_name.empty()) {
        fixed_uuid = options.instance_name;
    }

    // 直接设置服务信息，无需 gRPC 登录（文件日志模式）
    setupServiceInfo(options, info);

    // 启动后台文件写入线程
    std::thread c(consumer, options);
    c.detach();

    sky_log("the apache skywalking php plugin mounted (file logging mode)");
}

// 后台消费者线程函数（无限循环）
// 从消息队列读取追踪数据，写入本地 JSON 文件
// @param options 配置选项
[[noreturn]] void Manager::consumer(const ManagerOptions &options) {
    // 创建日志目录（权限 0755）
    mkdir(options.log_file_path.c_str(), 0755);

    std::vector<std::string> file_list;

    try {
        // 打开消息队列（只读模式）
        boost::interprocess::message_queue mq(boost::interprocess::open_only, s_info->mq_name);

        while (true) {
            std::string data;
            data.resize(SKYWALKING_G(mq_max_message_length));
            size_t msg_size;
            unsigned msg_priority;
            // 从消息队列接收数据（阻塞等待）
            mq.receive(&data[0], data.size(), msg_size, msg_priority);
            data.resize(msg_size);

            // data 已经是 JSON 格式，直接使用
            std::string json_str = data;

            // 从 JSON 中提取 traceId（简单解析，取第一个 "traceId":"..." 的值）
            size_t traceIdPos = json_str.find("\"traceId\":\"");
            std::string traceIdShort = "unknown";
            if (traceIdPos != std::string::npos) {
                size_t start = traceIdPos + 11; // 跳过 "traceId":"
                size_t end = json_str.find("\"", start);
                if (end != std::string::npos) {
                    traceIdShort = json_str.substr(start, std::min(end - start, size_t(8)));
                }
            }

            // 生成文件名: skywalking-{timestamp}-{traceid}.json
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::string filename = options.log_file_path + "/skywalking-" +
                std::to_string(now) + "-" + traceIdShort + ".json";

            // 写入文件
            std::ofstream outfile(filename);
            outfile << json_str;
            outfile.close();

            sky_log("write trace to file: " + filename);

            // 文件轮转：当文件数量超过限制时，删除最旧的文件
            file_list.push_back(filename);
            if (file_list.size() > options.log_file_max_files) {
                std::string old_file = file_list.front();
                file_list.erase(file_list.begin());
                if (remove(old_file.c_str()) == 0) {
                    sky_log("removed old file: " + old_file);
                }
            }
        }
    } catch (boost::interprocess::interprocess_exception &ex) {
        sky_log(ex.what());
        php_error(E_WARNING, "%s %s", "[skywalking] consumer error ", ex.what());
    }
}

// 获取本机的所有 IP 地址（排除 127.x.x.x）
// @return IP 地址列表
std::vector<std::string> Manager::getIps() {

    std::vector<std::string> ips;

    struct ifaddrs *interfaces = nullptr;
    struct ifaddrs *tempAddress = nullptr;
    int success = getifaddrs(&interfaces);

    if (success == 0) {
        tempAddress = interfaces;
        while (tempAddress != nullptr) {
            if (tempAddress->ifa_addr->sa_family == AF_INET) {
                std::string ip = inet_ntoa(((struct sockaddr_in *) tempAddress->ifa_addr)->sin_addr);
                // 排除回环地址
                if (ip.find("127") != 0) {
                    ips.push_back(ip);
                }
            }
            tempAddress = tempAddress->ifa_next;
        }
    }

    freeifaddrs(interfaces);

    return ips;
}

// 生成 UUID（如果设置了固定实例名，则返回固定值）
// @return UUID 字符串（格式：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx）
std::string Manager::generateUUID() {

    if (!fixed_uuid.empty()) {
        return fixed_uuid;
    }

    static std::random_device dev;
    static std::mt19937 rng(dev());
    std::uniform_int_distribution<int> dist(0, 15);

    const char *v = "0123456789abcdef";
    const bool dash[] = {0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0};

    std::string res;
    for (bool i : dash) {
        if (i) res += "-";
        res += v[dist(rng)];
        res += v[dist(rng)];
    }

    return res;
}
