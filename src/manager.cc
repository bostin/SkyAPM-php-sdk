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
#include <iostream>
#include <string>
#include <zconf.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <random>

#include "php_skywalking.h"
#include "sky_log.h"
#include <unistd.h>

static std::string fixed_uuid;

// 设置服务信息（服务名、实例名）
// 使用确定性实例名生成，确保所有进程生成相同的实例名
// @param options 配置选项
// @param info 服务信息结构体指针
void Manager::setupServiceInfo(const ManagerOptions &options, struct service_info *info) {
    if (info == nullptr) {
        return;
    }

    std::string instance;

    if (!options.instance_name.empty()) {
        // 使用用户指定的实例名（确定性）
        instance = options.instance_name;
    } else {
        // 生成确定性实例名：主机名 + 固定标识
        // 所有相同配置的进程会生成相同的实例名
        char hostname[HOST_NAME_MAX + 1];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            instance = std::string(hostname) + "-skywalking-agent";
        } else {
            // 回退到 "localhost-skywalking-agent"
            instance = "localhost-skywalking-agent";
        }
    }

    // 安全地复制字符串，防止缓冲区溢出
    strncpy(info->service, options.code.c_str(), sizeof(info->service) - 1);
    info->service[sizeof(info->service) - 1] = '\0';

    strncpy(info->service_instance, instance.c_str(), sizeof(info->service_instance) - 1);
    info->service_instance[sizeof(info->service_instance) - 1] = '\0';

    // 标记为已初始化
    info->initialized = 1;
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
