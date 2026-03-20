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
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2017 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

#include <src/sky_shm.h>
#include <fstream>
#include "php_skywalking.h"

#include "src/sky_utils.h"
#include "src/sky_module.h"
#include "src/segment.h"
#include "sys/mman.h"

#ifdef MYSQLI_USE_MYSQLND
#include "ext/mysqli/php_mysqli_structs.h"
#endif

ZEND_DECLARE_MODULE_GLOBALS(skywalking)

struct service_info *s_info = nullptr;

PHP_INI_BEGIN()
    // 启用/禁用 SkyWalking 扩展
    // 0 = 禁用, 1 = 启用
    STD_PHP_INI_BOOLEAN("skywalking.enable", "0", PHP_INI_ALL, OnUpdateBool, enable, zend_skywalking_globals, skywalking_globals)

    // SkyWalking 协议版本 (用于 SW8 header 生成)
    // 支持版本: 5, 6, 7, 8
	STD_PHP_INI_ENTRY("skywalking.version", "8", PHP_INI_ALL, OnUpdateLong, version, zend_skywalking_globals, skywalking_globals)

    // 应用代码/名称，用于标识服务
	STD_PHP_INI_ENTRY("skywalking.app_code", "hello_skywalking", PHP_INI_ALL, OnUpdateString, app_code, zend_skywalking_globals, skywalking_globals)

    // 追踪数据文件存储路径
    // 追踪数据将以 JSON 格式写入此目录
	STD_PHP_INI_ENTRY("skywalking.log_file_path", "/tmp/skywalking", PHP_INI_ALL, OnUpdateString, log_file_path, zend_skywalking_globals, skywalking_globals)

    // 单个追踪日志文件的最大大小（字节）
    // 默认: 10485760 (10MB)
	STD_PHP_INI_ENTRY("skywalking.log_file_max_size", "10485760", PHP_INI_ALL, OnUpdateLong, log_file_max_size, zend_skywalking_globals, skywalking_globals)

    // 保留的最大日志文件数量
    // 超过此数量时，最旧的文件将被删除
    // 默认: 100
	STD_PHP_INI_ENTRY("skywalking.log_file_max_files", "100", PHP_INI_ALL, OnUpdateLong, log_file_max_files, zend_skywalking_globals, skywalking_globals)

	// 启用/禁用扩展内部调试日志
    // 0 = 禁用, 1 = 启用
    // 调试日志写入到 log_path 指定的文件
	STD_PHP_INI_BOOLEAN("skywalking.log_enable", "0", PHP_INI_ALL, OnUpdateBool, log_enable, zend_skywalking_globals, skywalking_globals)

    // 扩展内部调试日志文件路径
	STD_PHP_INI_ENTRY("skywalking.log_path", "/tmp/skywalking-php.log", PHP_INI_ALL, OnUpdateString, log_path, zend_skywalking_globals, skywalking_globals)

    // 启用/禁用 PHP 错误处理器
    // 0 = 禁用, 1 = 启用
    // 启用后会自动追踪 PHP 错误和异常
    STD_PHP_INI_BOOLEAN("skywalking.error_handler_enable", "0", PHP_INI_ALL, OnUpdateBool, error_handler_enable, zend_skywalking_globals, skywalking_globals)

    // 消息队列最大消息长度（字节）
    // 用于进程间通信，单个追踪数据不能超过此大小
    // 默认: 20480 (20KB)
    STD_PHP_INI_ENTRY("skywalking.mq_max_message_length", "20480", PHP_INI_ALL, OnUpdateLong, mq_max_message_length, zend_skywalking_globals, skywalking_globals)

    // 采样率配置
    // -1 = 不限制，采集所有追踪
    // N = 每 3 秒最多采集 N 条追踪
    // 用于控制高负载下的追踪数据量
    STD_PHP_INI_ENTRY("skywalking.sample_n_per_3_secs", "-1", PHP_INI_ALL, OnUpdateLong, sample_n_per_3_secs, zend_skywalking_globals, skywalking_globals)

    // 实例名称（可选）
    // 留空则自动生成 UUID@IP 格式的实例名
    // 用于标识服务实例，便于在分布式环境中识别
    STD_PHP_INI_ENTRY("skywalking.instance_name", "", PHP_INI_ALL, OnUpdateString, instance_name, zend_skywalking_globals, skywalking_globals)

PHP_INI_END()

// 初始化全局变量默认值
static void php_skywalking_init_globals(zend_skywalking_globals *skywalking_globals) {
    skywalking_globals->app_code = nullptr;
    skywalking_globals->enable = 0;
    skywalking_globals->version = 0;

    // file logging
    skywalking_globals->log_file_path = nullptr;
    skywalking_globals->log_file_max_size = 10485760;  // 10MB
    skywalking_globals->log_file_max_files = 100;

    // log
    skywalking_globals->log_enable = 0;
    skywalking_globals->log_path = nullptr;

    // php error log
    skywalking_globals->error_handler_enable = 0;

    // message queue
    skywalking_globals->mq_max_message_length = 0;

    // rate limit
    skywalking_globals->sample_n_per_3_secs = -1;

    // uuid path
    skywalking_globals->instance_name = nullptr;

}

// 获取当前请求的追踪 ID
// string skywalking_trace_id(void)
PHP_FUNCTION (skywalking_trace_id) {
    auto *segment = sky_get_segment(execute_data, -1);
    if (SKYWALKING_G(enable) && segment != nullptr) {
        std::string trace_id = segment->getTraceId();
        RETURN_STRING(trace_id.c_str());
    } else {
        RETURN_STRING("");
    }
}

/* {{{ proto void skywalking_log(string key, string log [, bool is_error])
 * 向追踪中添加日志条目
 * @param string $name  Span 名称
 * @param string $key   日志键
 * @param string $value 日志值
 * @param bool   $is_error 是否标记为错误（默认 false）
 */
PHP_FUNCTION(skywalking_log)
{
    zend_string *name;
    zend_string *key;
    zend_string *value;
    zend_bool   is_error = 0;

    ZEND_PARSE_PARAMETERS_START(3, 4)
        Z_PARAM_STR(name)
        Z_PARAM_STR(key)
        Z_PARAM_STR(value)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(is_error)
    ZEND_PARSE_PARAMETERS_END();

    auto *segment = sky_get_segment(execute_data, -1);
    if (!SKYWALKING_G(enable) || segment == nullptr) {
        return;
    }

    if (ZSTR_LEN(name) > 0 && ZSTR_LEN(key) > 0 && ZSTR_LEN(value) > 0) {
        auto span = segment->findOrCreateSpan(name->val, SkySpanType::Local, SkySpanLayer::Unknown, 0);
        span->addLog(key->val, value->val);
        if (is_error) {
            span->setIsError(true);
        }
        span->setEndTIme();
    }
}

/* {{{ proto void skywalking_tag(string key, string value)
 * 向追踪中添加标签
 * @param string $name  Span 名称
 * @param string $key   标签键
 * @param string $value 标签值
 */
PHP_FUNCTION(skywalking_tag)
{
    zend_string *name;
    zend_string *key;
    zend_string *value;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STR(name)
        Z_PARAM_STR(key)
        Z_PARAM_STR(value)
    ZEND_PARSE_PARAMETERS_END();

    auto *segment = sky_get_segment(execute_data, -1);
    if (!SKYWALKING_G(enable) || segment == nullptr) {
        return;
    }

    if (ZSTR_LEN(name) > 0 && ZSTR_LEN(key) > 0 && ZSTR_LEN(value) > 0) {
        auto span = segment->findOrCreateSpan(name->val, SkySpanType::Local, SkySpanLayer::Unknown, 0);
        span->addTag(key->val, value->val);
        span->setEndTIme();
    }
}

// 模块初始化函数（PHP 进程启动时调用一次）
PHP_MINIT_FUNCTION (skywalking) {
	ZEND_INIT_MODULE_GLOBALS(skywalking, php_skywalking_init_globals, NULL);
	REGISTER_INI_ENTRIES();

	if (SKYWALKING_G(enable)) {
        // 创建共享内存用于存储服务信息
        int protection = PROT_READ | PROT_WRITE;
        int visibility = MAP_SHARED | MAP_ANONYMOUS;

        s_info = (struct service_info *) mmap(nullptr, sizeof(struct service_info), protection, visibility, -1, 0);
        // 初始化模块：注册钩子函数、创建消息队列等
        sky_module_init();
	}

	return SUCCESS;
}

// 模块关闭函数（PHP 进程结束时调用）
PHP_MSHUTDOWN_FUNCTION (skywalking) {
    UNREGISTER_INI_ENTRIES();

    if (SKYWALKING_G(enable)) {
        // 清理模块资源：删除消息队列、释放内存等
        sky_module_cleanup();
    }

    return SUCCESS;
}

// 请求初始化函数（每个 PHP 请求开始时调用）
PHP_RINIT_FUNCTION(skywalking)
{
#if defined(COMPILE_DL_SKYWALKING) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
    if (SKYWALKING_G(enable)) {
        // 只在 PHP-FPM 环境下自动初始化追踪
        if (strcasecmp("fpm-fcgi", sapi_module.name) == 0) {
            if (strlen(s_info->service_instance) == 0) {
                sky_log("service_instance is empty, skip tracing");
                return SUCCESS;
            }

            // 初始化请求追踪：创建追踪段、解析 SW8 header 等
            sky_request_init(nullptr, 0);
        }
    }
    return SUCCESS;
}

// 请求关闭函数（每个 PHP 请求结束时调用）
PHP_RSHUTDOWN_FUNCTION(skywalking)
{
	if (SKYWALKING_G(enable)) {
        // 只在 PHP-FPM 环境下自动刷新追踪数据
        if (strcasecmp("fpm-fcgi", sapi_module.name) == 0) {
            if (SKYWALKING_G(segment) == nullptr) {
                return SUCCESS;
            }

            // 刷新追踪数据：序列化为 JSON、发送到消息队列
            sky_request_flush(nullptr, 0);
            // 清理 CURL header 资源
            zval_dtor(&SKYWALKING_G(curl_header));
        }
	}
	return SUCCESS;
}

PHP_MINFO_FUNCTION(skywalking)
{
	DISPLAY_INI_ENTRIES();
}

//PHP_GINIT_FUNCTION(skywalking)
//{
//    memset(skywalking_globals, 0, sizeof(*skywalking_globals));
//}

zend_module_dep skywalking_deps[] = {
        ZEND_MOD_REQUIRED("json")
        ZEND_MOD_REQUIRED("pcre")
        ZEND_MOD_REQUIRED("standard")
        ZEND_MOD_REQUIRED("curl")
        ZEND_MOD_END
};

ZEND_BEGIN_ARG_INFO_EX(arginfo_skywalking_trace_id, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_skywalking_log, 0, 0, 4)
    ZEND_ARG_INFO(0, name)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, value)
    ZEND_ARG_INFO(0, is_error)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_skywalking_tag, 0, 0, 3)
    ZEND_ARG_INFO(0, name)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

const zend_function_entry skywalking_functions[] = {
        PHP_FE (skywalking_trace_id, arginfo_skywalking_trace_id)
        PHP_FE (skywalking_log,      arginfo_skywalking_log)
        PHP_FE (skywalking_tag,      arginfo_skywalking_tag)
        PHP_FE_END
};

zend_module_entry skywalking_module_entry = {
        STANDARD_MODULE_HEADER_EX,
        NULL,
        skywalking_deps,
        "skywalking",
        skywalking_functions,
        PHP_MINIT(skywalking),
        PHP_MSHUTDOWN(skywalking),
        PHP_RINIT(skywalking),
        PHP_RSHUTDOWN(skywalking),
        PHP_MINFO(skywalking),
        PHP_SKYWALKING_VERSION,
        STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_SKYWALKING
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(skywalking)
#endif
