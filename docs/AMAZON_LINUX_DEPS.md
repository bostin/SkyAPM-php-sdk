# Amazon Linux 1 - SkyAPM PHP SDK 依赖项指南

## 快速开始

### 选项 1：仅检查依赖项（推荐）

上传并运行此脚本来检查您的 Amazon Linux 1 服务器：

```bash
# 1. 上传脚本到服务器
scp check_only.sh user@your-server:/tmp/

# 2. 在服务器上运行
ssh user@your-server
cd /tmp
chmod +x check_only.sh
./check_only.sh
```

### 选项 2：检查并自动安装依赖项

**警告**：此脚本会安装软件包，需要 sudo 权限

```bash
# 1. 上传脚本到服务器
scp check_dependencies_amazon_linux.sh user@your-server:/tmp/

# 2. 在服务器上运行
ssh user@your-server
cd /tmp
chmod +x check_dependencies_amazon_linux.sh
./check_dependencies_amazon_linux.sh
```

## 手动安装依赖项

如果您想手动安装依赖项，请按照以下步骤操作：

### 1. 更新 Yum 仓库

```bash
sudo yum clean all
sudo yum makecache
sudo yum update -y
```

### 2. 安装构建工具

```bash
# 安装开发工具组
sudo yum groupinstall -y "Development Tools"

# 安装额外的构建工具
sudo yum install -y autoconf automake libtool make gcc-c++
```

**说明**：
- `gcc-c++` - C++ 编译器（支持 C++11）
- `autoconf` - 自动配置工具
- `automake` - 自动生成 Makefile
- `libtool` - 库构建工具
- `make` - 构建工具

### 3. 安装系统库

```bash
# Boost 库（用于消息队列）
sudo yum install -y boost-devel boost-static

# 其他系统库
sudo yum install -y libcurl-devel openssl-devel
sudo yum install -y libstdc++-devel
sudo yum install -y glibc-devel
```

**说明**：
- `boost-devel` - Boost C++ 库开发文件（必需）
- `libcurl-devel` - CURL 开发文件（用于 HTTP 插件）
- `glibc-devel` - GNU C 库开发文件

### 4. 安装 PHP 和扩展

```bash
# 安装 PHP 核心和开发文件
sudo yum install -y php php-devel php-pear

# 安装必需的 PHP 扩展
sudo yum install -y php-json
sudo yum install -y php-curl
sudo yum install -y php-pdo
sudo yum install -y php-mysql
sudo yum install -y php-mysqli
sudo yum install -y php-process
```

**说明**：
- `php-devel` - PHP 开发头文件（必需）
- `php-json` - JSON 扩展（必需）
- `php-curl` - CURL 扩展（用于 HTTP 追踪）
- `php-pdo` - PDO 数据库抽象层
- `php-mysqli` - MySQL 改进扩展
- `php-process` - 进程控制扩展

### 5. 安装可选 PHP 扩展

这些扩展用于特定的追踪插件：

```bash
# Redis 客户端追踪
sudo yum install -y php-pecl-redis

# Memcached 追踪
sudo yum install -y php-pecl-memcached

# 如果上述包不可用，可以通过 PECL 安装
sudo pecl install redis
sudo pecl install memcached
```

### 6. 配置消息队列

SkyAPM 使用 POSIX 消息队列进行进程间通信：

```bash
# 创建消息队列挂载点
sudo mkdir -p /dev/mqueue

# 挂载消息队列
sudo mount -t mqueue none /dev/mqueue

# 永久挂载（添加到 fstab）
echo "none /dev/mqueue mqueue defaults 0 0" | sudo tee -a /etc/fstab
```

### 7. 验证安装

运行快速检查脚本验证所有依赖项：

```bash
./check_only.sh
```

或手动验证：

```bash
# 检查编译工具
g++ --version
autoconf --version
automake --version
libtool --version
make --version
phpize --version

# 检查系统库
ldconfig -p | grep boost
ldconfig -p | grep pthread
ldconfig -p | grep dl

# 检查 PHP
php -v
php -m | grep -E "json|curl|pdo|mysqli"

# 检查 PHP 头文件
find /usr/include -name "php_json.h"
find /usr/include -name "php.h"

# 检查 C++11 支持
echo 'int main() { return 0; }' | g++ -std=c++11 -x c++ -o /tmp/test - && echo "C++11 OK" && rm /tmp/test

# 检查线程安全（应该是 disabled）
php -i | grep "Thread Safety"

# 检查消息队列
ls -ld /dev/mqueue
```

## 构建 SkyAPM PHP SDK

依赖项安装完成后，构建扩展：

```bash
# 1. 进入源代码目录
cd /path/to/SkyAPM-php-sdk

# 2. 准备构建环境
phpize

# 3. 配置构建
./configure

# 4. 编译
make -j$(nproc)

# 5. 安装
sudo make install

# 6. 配置 php.ini
php --ini  # 找到 php.ini 位置

# 添加到 php.ini
echo "extension=skywalking.so" | sudo tee -a /etc/php.ini

# 7. 验证安装
php -m | grep skywalking
```

## 配置 SkyAPM

编辑 `/etc/php.ini` 或创建 `/etc/php.d/skywalking.ini`：

```ini
[skywalking]
extension=skywalking.so

# 基本配置
skywalking.enable = 1
skywalking.app_code = your_application_name
skywalking.version = 8

# 文件日志配置
skywalking.log_file_path = /tmp/skywalking
skywalking.log_file_max_size = 10485760
skywalking.log_file_max_files = 100

# 调试日志（可选）
skywalking.log_enable = 1
skywalking.log_path = /tmp/skywalking-php.log

# 实例名称（可选）
skywalking.instance_name = server-1

# 采样率（可选，-1 表示不限制）
skywalking.sample_n_per_3_secs = -1
```

## Amazon Linux 1 特定注意事项

### 1. 旧版 GCC

Amazon Linux 1 使用较旧版本的 GCC。如果 C++11 支持检查失败：

```bash
# 检查 GCC 版本
g++ --version

# 如果版本 < 4.8，可能需要升级
# Amazon Linux 1 通常使用 gcc 4.4.7
# 考虑使用 Amazon Linux 2 或 devtoolset-7
sudo yum install -y devtoolset-7
scl enable devtoolset-7 bash
```

### 2. PHP 版本

Amazon Linux 1 默认提供 PHP 5.3-5.6。SkyAPM 需要 PHP 7.0+：

```bash
# 检查当前 PHP 版本
php -v

# 如果 PHP 版本 < 7.0，需要安装新版本
# 使用 Remi 仓库
sudo yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-6.noarch.rpm
sudo yum install -y https://rpms.remirepo.net/enterprise/remi-release-6.rpm

# 启用 PHP 7.4 仓库
sudo yum-config-manager --enable remi-php74

# 安装 PHP 7.4
sudo yum install -y php php-devel php-json php-curl php-pdo php-mysqli
```

### 3. SELinux

如果 SELinux 启用，可能需要调整上下文：

```bash
# 检查 SELinux 状态
getenforce

# 如果是 Enforcing，临时设置为 Permissive 进行测试
sudo setenforce 0

# 或为消息队列添加 SELinux 策略
sudo chcon -t tmpfs_t /dev/mqueue
```

### 4. 防火墙

如果使用远程日志收集，确保防火墙允许相关端口：

```bash
# 如果未来需要发送到远程服务器
# sudo iptables -I INPUT -p tcp --dport 11800 -j ACCEPT
# sudo service iptables save
```

## 常见问题排查

### Q: phpize 命令未找到

```bash
# 安装 php-devel
sudo yum install -y php-devel
```

### Q: configure 时找不到 php_json.h

```bash
# 确认安装了 php-devel 和 php-json
sudo yum install -y php-devel php-json

# 查找头文件位置
find /usr -name "php_json.h"

# 如果在非标准位置，创建符号链接
sudo ln -s /usr/include/php/ext/json/php_json.h /usr/include/php/
```

### Q: Boost 库未找到

```bash
# 安装 boost-devel
sudo yum install -y boost-devel

# 验证安装
ldconfig -p | grep boost
```

### Q: 消息队列权限错误

```bash
# 检查消息队列挂载
mount | grep mqueue

# 重新挂载
sudo umount /dev/mqueue
sudo mount -t mqueue none /dev/mqueue

# 检查权限
ls -ld /dev/mqueue
# 应该显示: drwxrwxrwt
```

### Q: 构建时 C++11 错误

```bash
# 检查 GCC 版本
g++ --version

# 使用 devtoolset（如果可用）
sudo yum install -y devtoolset-7
scl enable devtoolset-7 bash

# 然后重新构建
phpize
./configure
make -j$(nproc)
```

## 依赖项快速参考

| 类别 | 包名 | 用途 | 必需/可选 |
|------|------|------|----------|
| 构建工具 | gcc-c++ | C++ 编译器 | 必需 |
| 构建工具 | autoconf | 自动配置 | 必需 |
| 构建工具 | automake | 自动生成 Makefile | 必需 |
| 构建工具 | libtool | 库构建 | 必需 |
| 构建工具 | make | 构建工具 | 必需 |
| 系统库 | boost-devel | Boost C++ 库 | 必需 |
| 系统库 | glibc-devel | GNU C 库 | 必需 |
| 系统库 | libcurl-devel | CURL 开发 | 必需 |
| PHP | php | PHP 运行时 | 必需 |
| PHP | php-devel | PHP 开发文件 | 必需 |
| PHP 扩展 | php-json | JSON 支持 | 必需 |
| PHP 扩展 | php-curl | HTTP 追踪 | 必需 |
| PHP 扩展 | php-pdo | 数据库追踪 | 必需 |
| PHP 扩展 | php-mysqli | MySQL 追踪 | 可选 |
| PHP 扩展 | php-pecl-redis | Redis 追踪 | 可选 |
| PHP 扩展 | php-pecl-memcached | Memcached 追踪 | 可选 |

## 支持的 PHP 版本

- ✅ PHP 7.0
- ✅ PHP 7.1
- ✅ PHP 7.2
- ✅ PHP 7.3
- ✅ PHP 7.4
- ✅ PHP 8.0
- ❌ PHP 5.x（不支持）
- ⚠️ PHP 8.1+（未测试）

## 下一步

依赖项安装完成后：

1. **构建扩展**：按照上述构建步骤操作
2. **配置 php.ini**：添加 SkyAPM 配置
3. **重启 PHP-FPM**（如果使用）：
   ```bash
   sudo service php-fpm restart
   ```
4. **验证安装**：
   ```bash
   php -m | grep skywalking
   php -r "echo skywalking_trace_id();"
   ```
5. **检查日志**：
   ```bash
   tail -f /tmp/skywalking-php.log
   ls -la /tmp/skywalking/
   ```

## 获取帮助

如果遇到问题：

1. 运行 `check_only.sh` 获取详细诊断信息
2. 检查 `/tmp/skywalking-php.log` 查看错误消息
3. 查看 GitHub Issues: https://github.com/SkyAPM/SkyAPM-php-sdk/issues
