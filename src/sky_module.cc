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


#include <unordered_map>
#include <iostream>
#include "sky_module.h"
#include <fstream>
#include <sys/stat.h>
#include <chrono>
#include <random>
#include <fcntl.h>
#include <sys/file.h>

#include "segment.h"
#include "sky_utils.h"
#include "sky_plugin_curl.h"
#include "sky_execute.h"
#include "manager.h"
#include "sky_plugin_error.h"
#include "sky_log.h"
#include "sky_rate_limit.h"
#include "storage/storage_interface.h"
#include "storage/json_storage.h"
#ifdef HAVE_SQLITE3
#include "storage/sqlite_storage.h"
#endif

extern void (*ori_execute_ex)(zend_execute_data *execute_data);

extern void (*ori_execute_internal)(zend_execute_data *execute_data, zval *return_value);

extern void (*orig_curl_exec)(INTERNAL_FUNCTION_PARAMETERS);

extern void (*orig_curl_setopt)(INTERNAL_FUNCTION_PARAMETERS);

extern void (*orig_curl_setopt_array)(INTERNAL_FUNCTION_PARAMETERS);

extern void (*orig_curl_close)(INTERNAL_FUNCTION_PARAMETERS);

// 全局存储接口指针
static StorageInterface* g_storage = nullptr;

// 创建存储后端实例
static StorageInterface* create_storage_backend() {
    std::string logPath = SKYWALKING_G(log_file_path) ? SKYWALKING_G(log_file_path) : "/tmp/skywalking";
    std::string storageType = SKYWALKING_G(storage_type) ? SKYWALKING_G(storage_type) : "sqlite";

    StorageInterface* storage = nullptr;

#ifdef HAVE_SQLITE3
    if (storageType == "sqlite") {
        // 使用 SQLite 数据库存储
        std::string dbPath = logPath + "/skywalking_traces.db";
        int maxSizeMB = SKYWALKING_G(sqlite_max_size_mb);
        storage = new SqliteStorage(dbPath, true, maxSizeMB);
        if (storage->initialize()) {
            return storage;
        }
        delete storage;
        // SQLite 初始化失败，回退到 JSON 文件
    }
#endif

    // 默认或 JSON 模式：使用 JSON 文件存储
    storage = new JsonStorage(logPath);
    storage->initialize();
    return storage;
}

void sky_module_init(struct service_info *info) {
    ori_execute_ex = zend_execute_ex;
    zend_execute_ex = sky_execute_ex;

    ori_execute_internal = zend_execute_internal;
    zend_execute_internal = sky_execute_internal;

    if (SKYWALKING_G(error_handler_enable)) {
        sky_plugin_error_init();
    }

    // bind curl
    zend_function *old_function;
    if ((old_function = SKY_OLD_FN("curl_exec")) != nullptr) {
        orig_curl_exec = old_function->internal_function.handler;
        old_function->internal_function.handler = sky_curl_exec_handler;
    }
    if ((old_function = SKY_OLD_FN("curl_setopt")) != nullptr) {
        orig_curl_setopt = old_function->internal_function.handler;
        old_function->internal_function.handler = sky_curl_setopt_handler;
    }
    if ((old_function = SKY_OLD_FN("curl_setopt_array")) != nullptr) {
        orig_curl_setopt_array = old_function->internal_function.handler;
        old_function->internal_function.handler = sky_curl_setopt_array_handler;
    }
    if ((old_function = SKY_OLD_FN("curl_close")) != nullptr) {
        orig_curl_close = old_function->internal_function.handler;
        old_function->internal_function.handler = sky_curl_close_handler;
    }

    std::unordered_map<uint64_t, Segment *> *segments = new std::unordered_map<uint64_t, Segment *>;
    SKYWALKING_G(segment) = segments;

    FixedWindowRateLimiter *rate_limiter = new FixedWindowRateLimiter(SKYWALKING_G(sample_n_per_3_secs));
    SKYWALKING_G(rate_limiter) = rate_limiter;

    // 初始化服务信息（使用确定性实例名生成）
    ManagerOptions opt;
    opt.version = SKYWALKING_G(version);
    opt.code = SKYWALKING_G(app_code);
    opt.log_file_path = SKYWALKING_G(log_file_path) ? SKYWALKING_G(log_file_path) : "/tmp/skywalking";
    opt.log_file_max_size = SKYWALKING_G(log_file_max_size);
    opt.log_file_max_files = SKYWALKING_G(log_file_max_files);
    opt.instance_name = SKYWALKING_G(instance_name);

    Manager::setupServiceInfo(opt, info);

    // 初始化存储后端（仅 JSON 文件存储）
    g_storage = create_storage_backend();

    // 使用原子文件创建替代文件锁机制
    // O_CREAT | O_EXCL 是内核级原子操作，确保只有一个进程输出初始化日志
    // 避免多进程环境下的日志洪水
    if (SKYWALKING_G(log_enable)) {
        std::string marker_file = opt.log_file_path + "/.skywalking_init_marker";

        // 确保日志目录存在
        mkdir(opt.log_file_path.c_str(), 0755);

        // 检查标记文件是否存在且太旧（超过60秒）
        struct stat st;
        bool should_create_log = true;
        if (stat(marker_file.c_str(), &st) == 0) {
            // 文件存在，检查时间
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto file_age = now - st.st_mtime;
            if (file_age < 60) {
                // 文件较新，跳过日志输出
                should_create_log = false;
            } else {
                // 文件太旧，删除它
                unlink(marker_file.c_str());
            }
        }

        if (should_create_log) {
            // 原子操作：只允许一个进程创建标记文件
            int marker_fd = open(marker_file.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);

            if (marker_fd != -1) {
                // 我们是第一个进程 - 输出初始化日志
                sky_log("service: " + std::string(info->service));
                sky_log("service_instance: " + std::string(info->service_instance));
                sky_log("log_file_path: " + opt.log_file_path);

                // 获取实际使用的存储类型
                const char* storageType = SKYWALKING_G(storage_type) ? SKYWALKING_G(storage_type) : "sqlite";
                #ifndef HAVE_SQLITE3
                storageType = "json";
                #endif
                sky_log("storage: " + std::string(storageType) + " backend");
                sky_log("the apache skywalking php plugin mounted");

                // 写入 PID（用于调试，可选）
                std::string pid_str = std::to_string(getpid()) + "\n";
                write(marker_fd, pid_str.c_str(), pid_str.length());
                close(marker_fd);
            }
        }
    }
}

void sky_module_cleanup() {
    std::unordered_map<uint64_t, Segment *> *segments = static_cast<std::unordered_map<uint64_t, Segment *> *>(SKYWALKING_G(segment));
    for (auto entry : *segments) {
        delete entry.second;
    }

    delete segments;
    delete static_cast<FixedWindowRateLimiter*>(SKYWALKING_G(rate_limiter));

    // 清理存储后端（无日志）
    if (g_storage) {
        g_storage->shutdown();
        delete g_storage;
        g_storage = nullptr;
    }
}

void sky_request_init(zval *request, uint64_t request_id, struct service_info *info) {
    array_init(&SKYWALKING_G(curl_header));

    // 只在调试模式下输出详细日志
    if (SKYWALKING_G(log_enable)) {
        sky_log("sky_request_init: starting for request_id=" + std::to_string(request_id));
    }

    if (!static_cast<FixedWindowRateLimiter*>(SKYWALKING_G(rate_limiter))->validate()) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("sky_request_init: rate limited, skipping segment");
        }
        auto *segment = new Segment(info->service, info->service_instance, SKYWALKING_G(version), "");
        segment->setSkip(true);
        (void)sky_insert_segment(request_id, segment);

        return;
    }

    zval *carrier = nullptr;
    zval *sw, *peer_val;
    std::string header;
    std::string uri;
    std::string peer;

    if (request != nullptr) {
        zval *swoole_header = sky_read_property(request, "header", 0);
        zval *swoole_server = sky_read_property(request, "server", 0);

        if (SKYWALKING_G(version) == 8) {
            sw = zend_hash_str_find(Z_ARRVAL_P(swoole_header), "sw8", sizeof("sw8") - 1);
        } else {
            sw = nullptr;
        }

        header = (sw != nullptr ? Z_STRVAL_P(sw) : "");

        zval *uri_val = zend_hash_str_find(Z_ARRVAL_P(swoole_server), "request_uri", sizeof("request_uri") - 1);
        if (uri_val != nullptr) {
            uri = Z_STRVAL_P(uri_val);
        } else {
            uri = "/unknown";
        }

        peer_val = zend_hash_str_find(Z_ARRVAL_P(swoole_header), "host", sizeof("host") - 1);
        if (peer_val != nullptr) {
            peer = Z_STRVAL_P(peer_val);
        } else {
            char hostname[HOST_NAME_MAX + 1];
            if (gethostname(hostname, sizeof(hostname))) {
                hostname[0] = '\0';
            }
            zval *port_val = zend_hash_str_find(Z_ARRVAL_P(swoole_server), "server_port", sizeof("server_port") - 1);
            if (port_val != nullptr) {
                peer += hostname;
                peer += ":";
                peer += std::to_string(Z_LVAL_P(port_val));
            } else {
                peer = hostname;
            }
        }
    } else {
        zend_bool jit_initialization = PG(auto_globals_jit);

        if (jit_initialization) {
            zend_string *server_str = zend_string_init("_SERVER", sizeof("_SERVER") - 1, 0);
            zend_is_auto_global(server_str);
            zend_string_release(server_str);
        }
        carrier = zend_hash_str_find(&EG(symbol_table), ZEND_STRL("_SERVER"));

        if (SKYWALKING_G(version) == 5) {
            sw = zend_hash_str_find(Z_ARRVAL_P(carrier), "HTTP_SW3", sizeof("HTTP_SW3") - 1);
        } else if (SKYWALKING_G(version) == 6 || SKYWALKING_G(version) == 7) {
            sw = zend_hash_str_find(Z_ARRVAL_P(carrier), "HTTP_SW6", sizeof("HTTP_SW6") - 1);
        } else if (SKYWALKING_G(version) == 8) {
            sw = zend_hash_str_find(Z_ARRVAL_P(carrier), "HTTP_SW8", sizeof("HTTP_SW8") - 1);
        } else {
            sw = nullptr;
        }

        header = (sw != nullptr ? Z_STRVAL_P(sw) : "");
        uri = get_page_request_uri();
        peer = get_page_request_peer();
    }

    std::unordered_map<uint64_t, Segment *> *segments = static_cast<std::unordered_map<uint64_t, Segment *> *>SKYWALKING_G(segment);

    auto *segment = new Segment(info->service, info->service_instance, SKYWALKING_G(version), header);

    // 插入 segment 并检查是否成功
    if (!sky_insert_segment(request_id, segment)) {
        sky_log("sky_request_init: failed to insert segment, request_id=" + std::to_string(request_id) + " already exists");
        delete segment;
        return;
    }

    // 获取刚插入的 segment（使用 find 而不是 at，避免异常）
    auto it = segments->find(request_id);
    if (it == segments->end()) {
        sky_log("sky_request_init: segment not found after insert");
        return;
    }

    auto *seg = it->second;
    auto *span = seg->createSpan(SkySpanType::Entry, SkySpanLayer::Http, 8001);
    if (span == nullptr) {
        sky_log("sky_request_init: failed to create span");
        return;
    }

    span->setOperationName(uri);
    span->setPeer(peer);
    span->addTag("url", uri);
    seg->createRefs();

    zval *request_method = zend_hash_str_find(Z_ARRVAL(PG(http_globals)[TRACK_VARS_SERVER]), ZEND_STRL("REQUEST_METHOD"));
    if (request_method != NULL) {
        span->addTag("http.method", Z_STRVAL_P(request_method));
    }

    if (SKYWALKING_G(log_enable)) {
        sky_log("sky_request_init: segment created successfully");
    }
}


// 辅助函数：直接写入追踪数据到文件
static void write_trace_to_file(const std::string &json_str) {
    std::string log_file_path = SKYWALKING_G(log_file_path) ? SKYWALKING_G(log_file_path) : "/tmp/skywalking";

    // 确保日志目录存在（忽略已存在错误）
    mkdir(log_file_path.c_str(), 0755);

    // 从 JSON 中提取 traceId（简单解析）
    size_t traceIdPos = json_str.find("\"traceId\":\"");
    std::string traceIdShort = "unknown";
    if (traceIdPos != std::string::npos) {
        size_t start = traceIdPos + 11; // 跳过 "traceId":"
        size_t end = json_str.find("\"", start);
        if (end != std::string::npos && end > start) {
            size_t len = std::min(end - start, size_t(8));
            traceIdShort = json_str.substr(start, len);
        }
    }

    // 生成文件名: skywalking-{timestamp}-{traceid}-{pid}.json
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string filename = log_file_path + "/skywalking-" +
        std::to_string(now) + "-" + traceIdShort + "-" + std::to_string(getpid()) + ".json";

    // 写入文件
    std::ofstream outfile(filename, std::ios::out | std::ios::trunc);
    if (outfile.good()) {
        outfile << json_str;
        outfile.close();
        if (outfile.good()) {
            if (SKYWALKING_G(log_enable)) {
                sky_log("write trace to file: " + filename);
            }
        } else {
            if (SKYWALKING_G(log_enable)) {
                sky_log("failed to write trace data to: " + filename);
            }
        }
    } else {
        if (SKYWALKING_G(log_enable)) {
            sky_log("failed to open trace file: " + filename);
        }
    }
}

void sky_request_flush(zval *response, uint64_t request_id) {
    auto *segment = sky_get_segment(nullptr, request_id);
    if (segment == nullptr) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("sky_request_flush: segment is null, skipping");
        }
        return;
    }

    if (segment->skip()) {
        if (SKYWALKING_G(log_enable)) {
            sky_log("segment skipped, deleting");
        }
        delete segment;
        sky_remove_segment(request_id);

        return;
    }

    if (response == nullptr) {
        segment->setStatusCode(SG(sapi_headers).http_response_code);
    }

    // 使用存储后端保存追踪数据
    if (g_storage) {
        g_storage->saveSegment(segment);
    } else {
        // 如果存储后端未初始化，回退到直接写入文件
        std::string msg = segment->marshal();
        write_trace_to_file(msg);
    }

    if (SKYWALKING_G(log_enable)) {
        sky_log("segment flushed");
    }

    delete segment;
    sky_remove_segment(request_id);
}
