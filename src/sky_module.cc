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

#include "segment.h"
#include "sky_utils.h"
#include "sky_plugin_curl.h"
#include "sky_execute.h"
#include "manager.h"
#include "sky_plugin_error.h"
#include "sky_log.h"
#include "sky_rate_limit.h"

extern struct service_info *s_info;

extern void (*ori_execute_ex)(zend_execute_data *execute_data);

extern void (*ori_execute_internal)(zend_execute_data *execute_data, zval *return_value);

extern void (*orig_curl_exec)(INTERNAL_FUNCTION_PARAMETERS);

extern void (*orig_curl_setopt)(INTERNAL_FUNCTION_PARAMETERS);

extern void (*orig_curl_setopt_array)(INTERNAL_FUNCTION_PARAMETERS);

extern void (*orig_curl_close)(INTERNAL_FUNCTION_PARAMETERS);

void sky_module_init() {
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

    // 初始化服务信息（不再使用消息队列和后台线程）
    ManagerOptions opt;
    opt.version = SKYWALKING_G(version);
    opt.code = SKYWALKING_G(app_code);
    opt.log_file_path = SKYWALKING_G(log_file_path) ? SKYWALKING_G(log_file_path) : "/tmp/skywalking";
    opt.log_file_max_size = SKYWALKING_G(log_file_max_size);
    opt.log_file_max_files = SKYWALKING_G(log_file_max_files);
    opt.instance_name = SKYWALKING_G(instance_name);

    Manager::setupServiceInfo(opt, s_info);

    sky_log("service: " + std::string(s_info->service));
    sky_log("service_instance: " + std::string(s_info->service_instance));
    sky_log("log_file_path: " + opt.log_file_path);
    sky_log("the apache skywalking php plugin mounted (direct file write mode)");
}

void sky_module_cleanup() {
    std::unordered_map<uint64_t, Segment *> *segments = static_cast<std::unordered_map<uint64_t, Segment *> *>(SKYWALKING_G(segment));
    for (auto entry : *segments) {
        delete entry.second;
    }

    delete segments;
    delete static_cast<FixedWindowRateLimiter*>(SKYWALKING_G(rate_limiter));
}

void sky_request_init(zval *request, uint64_t request_id) {
    array_init(&SKYWALKING_G(curl_header));

    sky_log("sky_request_init: starting for request_id=" + std::to_string(request_id));

    if (!static_cast<FixedWindowRateLimiter*>(SKYWALKING_G(rate_limiter))->validate()) {
        sky_log("sky_request_init: rate limited, skipping segment");
        auto *segment = new Segment(s_info->service, s_info->service_instance, SKYWALKING_G(version), "");
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

    auto *segment = new Segment(s_info->service, s_info->service_instance, SKYWALKING_G(version), header);
    (void)sky_insert_segment(request_id, segment);

    auto *span = segments->at(request_id)->createSpan(SkySpanType::Entry, SkySpanLayer::Http, 8001);
    span->setOperationName(uri);
    span->setPeer(peer);
    span->addTag("url", uri);
    segments->at(request_id)->createRefs();

    zval *request_method = zend_hash_str_find(Z_ARRVAL(PG(http_globals)[TRACK_VARS_SERVER]), ZEND_STRL("REQUEST_METHOD"));
    if (request_method != NULL) {
        span->addTag("http.method", Z_STRVAL_P(request_method));
    }

    sky_log("sky_request_init: segment created successfully");
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
        if (end != std::string::npos) {
            traceIdShort = json_str.substr(start, std::min(end - start, size_t(8)));
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
            sky_log("write trace to file: " + filename);
        } else {
            sky_log("failed to write trace data to: " + filename);
        }
    } else {
        sky_log("failed to open trace file: " + filename);
    }
}

void sky_request_flush(zval *response, uint64_t request_id) {
    auto *segment = sky_get_segment(nullptr, request_id);
    if (segment == nullptr) {
        sky_log("sky_request_flush: segment is null, skipping");
        return;
    }

    if (segment->skip()) {
        sky_log("segment skipped, deleting");
        delete segment;
        sky_remove_segment(request_id);

        return;
    }

    if (response == nullptr) {
        segment->setStatusCode(SG(sapi_headers).http_response_code);
    }

    std::string msg = segment->marshal();
    sky_log("segment marshaled, size=" + std::to_string(msg.size()));

    delete segment;
    sky_remove_segment(request_id);

    // 直接写入文件（不再使用消息队列）
    write_trace_to_file(msg);
}
