# SkyAPM PHP SDK - 依赖项列表

## 概述

本文档列出了构建和运行 SkyAPM PHP SDK 所需的所有依赖项。

## 核心依赖（必需）

### 编译工具

| 工具 | 版本要求 | 说明 |
|------|---------|------|
| PHP | 7.0 - 8.0 | 目标 PHP 版本 |
| phpize | - | PHP 扩展构建工具 |
| C++ 编译器 | 支持 C++11 | g++ 或 clang |
| autoconf | 2.59+ | 自动配置工具 |
| automake | - | 自动生成 Makefile |
| libtool | - | 库构建工具 |
| make | - | 构建工具 |

### 系统库（核心功能）

| 库名称 | Linux 包名 | macOS 包名 | 说明 |
|---------|-----------|-----------|------|
| pthread | libc6-dev | 内置 | POSIX 线程库 |
| libdl | libc6-dev | 内置 | 动态链接库 |
| librt | (libc6) | 不需要 | 实时扩展库（仅 Linux） |
| libc++ | - | 内置 | C++ 标准库（仅 macOS） |
| boost | libboost-all-dev | boost | Boost 库（用于消息队列） |

### PHP 内置扩展（必需）

| 扩展名 | 说明 | 检查命令 |
|--------|------|---------|
| json | JSON 序列化 | `php -m \| grep json` |
| curl | HTTP 请求拦截 | `php -m \| grep curl` |

### PHP 内部头文件

| 头文件 | 用途 |
|--------|------|
| php_json.h | JSON API |
| php_curl.h | CURL 拦截 |
| mysqli_mysqlnd.h | MySQLi 支持 |

## 可选依赖（插件支持）

### 数据库插件

| 扩展名 | 用途 | 对应插件 |
|--------|------|---------|
| mysqli | MySQL 数据库追踪 | sky_plugin_mysqli.cc |

### 缓存插件

| 扩展名 | 用途 | 对应插件 |
|--------|------|---------|
| redis | Redis 客户端追踪 | sky_plugin_redis.cc |
| memcached | Memcached 追踪 | sky_plugin_memcached.cc |

### RPC/消息队列插件

| 扩展名 | 用途 | 对应插件 |
|--------|------|---------|
| yar | Yar RPC 追踪 | sky_plugin_yar.cc |
| amqp | RabbitMQ 追踪 | sky_plugin_rabbit_mq.cc |

### 框架插件

| 扩展名 | 用途 | 对应插件 |
|--------|------|---------|
| swoole | Swoole 框架支持 | sky_plugin_swoole_curl.cc |

## 系统调用依赖

| 头文件/库 | 用途 | 平台 |
|-----------|------|------|
| `<ifaddrs.h>` | 获取网络接口 | Linux/macOS |
| `<arpa/inet.h>` | IP 地址操作 | Linux/macOS |
| `<sys/stat.h>` | 文件系统操作 | Linux/macOS |
| `<zconf.h>` | zlib 配置 | Linux/macOS |

## 平台特定依赖

### Linux (Ubuntu/Debian)

```bash
# 基础构建工具
sudo apt-get install build-essential autoconf automake libtool

# C++ 编译器
sudo apt-get install g++

# Boost 库
sudo apt-get install libboost-all-dev

# PHP 开发文件
sudo apt-get install php-dev php-pear

# 可选：PHP 扩展开发文件
sudo apt-get install php-curl php-json php-mysql php-redis php-memcached
```

### macOS

```bash
# 安装 Homebrew（如果没有）
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 构建工具
brew install autoconf automake libtool shtool

# Boost 库
brew install boost

# PHP（通过 Homebrew 或自定义安装）
brew install php@7.4 php@8.0
```

### Alpine Linux

```bash
# 基础工具
apk add --no-cache autoconf automake libtool cmake g++ make file

# Boost 库
apk add --no-cache boost-dev

# PHP 开发文件
apk add --no-cache php-dev php-json php-curl php-mysqli
```

## 构建配置检查

config.m4 会自动检查以下依赖：

1. **PHP JSON 扩展**：检查 `php_json.h` 是否存在
   - 路径：`$phpincludedir/ext/json/php_json.h`
   - 失败错误：`Could not find php_json.h, please reinstall the php-json extension`

2. **C++11 支持**：检查编译器是否支持 C++11
   - 失败错误：`compiler not accept c++11`

3. **ZTS（线程安全）**：不支持 ZTS 构建
   - 失败错误：`skywalking does not support ZTS`

## 运行时依赖

### 必需配置（php.ini）

```ini
extension=skywalking.so

# 基本配置
skywalking.enable = 1
skywalking.app_code = your_app_name
skywalking.version = 8

# 文件日志配置（新版本）
skywalking.log_file_path = /tmp/skywalking
skywalking.log_file_max_size = 10485760
skywalking.log_file_max_files = 100
```

### 可选配置

```ini
# 调试日志
skywalking.log_enable = 1
skywalking.log_path = /tmp/skywalking-php.log

# 采样率（可选）
skywalking.sample_n_per_3_secs = -1

# 实例名称（可选）
skywalking.instance_name = your_instance_name
```

## 依赖验证命令

### 检查编译工具

```bash
# 检查 C++ 编译器
g++ --version

# 检查构建工具
autoconf --version
automake --version
libtool --version
make --version

# 检查 Boost 库
ldconfig -p | grep boost
```

### 检查 PHP 环境

```bash
# 检查 PHP 版本
php -v

# 检查 PHP 扩展
php -m | grep -E "json|curl|mysqli"

# 检查 PHP 开发文件
phpize --version

# 检查 PHP 头文件
find /usr -name "php_json.h" 2>/dev/null
```

### 检查系统库

```bash
# Linux
ldconfig -p | grep -E "pthread|rt|dl"

# macOS
otool -L /usr/lib/libc++.dylib

# 检查消息队列支持（运行时）
# 需要挂载 /dev/mqueue (Linux)
mount | grep mqueue
```

## 常见问题

### Q: 编译时提示找不到 php_json.h

**A:** 安装 PHP 开发文件：
```bash
# Ubuntu/Debian
sudo apt-get install php-dev

# CentOS/RHEL
sudo yum install php-devel

# macOS
brew install php
```

### Q: 编译时提示缺少 pthread/dl/rt

**A:** 这些是标准 C 库的一部分，应该已经安装。如果仍然报错：
```bash
# Ubuntu/Debian
sudo apt-get install libc6-dev

# CentOS/RHEL
sudo yum install glibc-devel
```

### Q: 找不到 Boost 库

**A:** 安装 Boost 开发文件：
```bash
# Ubuntu/Debian
sudo apt-get install libboost-all-dev

# CentOS/RHEL
sudo yum install boost-devel

# macOS
brew install boost
```

### Q: 运行时找不到消息队列

**A:** 确保系统支持 POSIX 消息队列：
```bash
# Linux - 挂载消息队列文件系统
sudo mount -t mqueue none /dev/mqueue

# 添加到 /etc/fstab 永久挂载
echo "none /dev/mqueue mqueue defaults 0 0" | sudo tee -a /etc/fstab
```

## 性能考虑

1. **线程数**：每个 PHP 进程会启动一个后台线程处理日志写入
2. **内存使用**：消息队列占用约 1-10MB 内存（取决于配置）
3. **磁盘 I/O**：日志文件写入是异步的，不会阻塞请求
4. **CPU 使用**：JSON 序列化占用的 CPU 较少

## 安全考虑

1. **日志目录权限**：确保日志目录只有 PHP 进程可写
2. **日志轮转**：配置 `log_file_max_files` 防止磁盘占满
3. **敏感数据**：避免在追踪数据中记录密码、密钥等敏感信息

## 版本兼容性

| 组件 | 最低版本 | 推荐版本 |
|------|---------|---------|
| PHP | 7.0 | 7.4+ |
| GCC | 4.8+ | 7.0+ |
| Clang | 3.3+ | 10.0+ |
| Boost | 1.55+ | 1.71+ |
| autoconf | 2.59+ | 2.69+ |
| automake | 1.11+ | 1.16+ |
