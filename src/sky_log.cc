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
#include <string>
#include <sstream>
#include <ostream>
#include <iostream>
#include <fstream>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include "php_skywalking.h"

void sky_log(std::string log) {
    if (!SKYWALKING_G(log_enable)) {
        return;
    }

    const char* log_path = SKYWALKING_G(log_path);
    if (!log_path || log_path[0] == '\0') {
        return;
    }

    // 每次写入时打开文件、加锁、写入、关闭
    // 这样可以避免 fork() 后 ofstream 状态不确定的问题
    // 也确保多进程并发写入的安全性
    int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        return;
    }

    // 获取文件锁（阻塞直到获得锁）
    if (flock(fd, LOCK_EX) == -1) {
        close(fd);
        return;
    }

    // 写入日志
    std::string log_line = log + "\n";
    ssize_t written = write(fd, log_line.c_str(), log_line.length());
    (void)written;  // 忽略返回值

    // 释放锁并关闭文件
    flock(fd, LOCK_UN);
    close(fd);
}
