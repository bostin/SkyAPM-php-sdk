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

void Manager::setupServiceInfo(const ManagerOptions &options, struct service_info *info) {
    if (info == nullptr) {
        return;
    }

    auto ips = getIps();
    std::string instance;
    if (!ips.empty()) {
        if (!options.instance_name.empty()) {
            instance = options.instance_name;
        } else {
            instance = generateUUID() + "@" + ips[0];
        }
    }

    strcpy(info->service, options.code.c_str());
    strcpy(info->service_instance, instance.c_str());
}

void Manager::init(const ManagerOptions &options, struct service_info *info) {
    if (!options.instance_name.empty()) {
        fixed_uuid = options.instance_name;
    }

    // 直接设置服务信息，无需 gRPC 登录
    setupServiceInfo(options, info);

    // 启动文件写入线程
    std::thread c(consumer, options);
    c.detach();

    sky_log("the apache skywalking php plugin mounted (file logging mode)");
}

[[noreturn]] void Manager::consumer(const ManagerOptions &options) {
    // 创建日志目录
    mkdir(options.log_file_path.c_str(), 0755);

    std::vector<std::string> file_list;

    try {
        boost::interprocess::message_queue mq(boost::interprocess::open_only, s_info->mq_name);

        while (true) {
            std::string data;
            data.resize(SKYWALKING_G(mq_max_message_length));
            size_t msg_size;
            unsigned msg_priority;
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

            // 文件轮转
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
