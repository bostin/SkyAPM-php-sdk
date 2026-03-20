# 依赖检查结果分析和解决方案

## 您的检查结果分析

根据您提供的检查结果，以下是**需要处理的问题**：

## 🔴 关键问题（必须解决）

### 1. PHP Thread Safety (ZTS) 问题

**当前状态：**
```
✗ PHP is ZTS (not compatible)
SkyAPM does not support ZTS builds
```

**但是**，您的 PHP 版本信息显示：
```
PHP 7.0.33 (cli) (built: Jan  9 2019 22:04:26) ( NTS )
```

`NTS` 通常表示 "Non-Thread Safety"，所以这里可能有误报。

**验证方法：**
```bash
php -i | grep "Thread Safety"
```

如果输出是：
- `Thread Safety => disabled` → 您的 PHP 是**非 ZTS**版本，可以继续
- `Thread Safety => enabled` → 您的 PHP 是 **ZTS** 版本，需要重新安装

**如果是 ZTS，解决方案：**
```bash
# Amazon Linux 1 - 安装 Remi 仓库的非 ZTS PHP
sudo yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-6.noarch.rpm
sudo yum install -y https://rpms.remirepo.net/enterprise/remi-release-6.rpm

# 启用 PHP 7.0 仓库
sudo yum-config-manager --enable remi-php70

# 删除现有的 ZTS PHP
sudo yum remove php-* php-*

# 安装非 ZTS PHP
sudo yum install -y php php-devel php-common php-json php-curl

# 验证
php -i | grep "Thread Safety"
# 应该显示：Thread Safety => disabled
```

---

## 🟡 次要问题（建议解决）

### 2. 缺少 php_curl.h 头文件

**当前状态：**
```
✗ PHP header php_curl.h NOT found
```

**解决方案：**
```bash
sudo yum install -y php-devel php-common
```

**验证：**
```bash
find /usr/include -name "php_curl.h"
```

---

### 3. 消息队列未挂载

**当前状态：**
```
⚠ POSIX message queue may not be available
Run: sudo mount -t mqueue none /dev/mqueue
```

**解决方案：**
```bash
# 创建并挂载消息队列
sudo mkdir -p /dev/mqueue
sudo mount -t mqueue none /dev/mqueue

# 永久挂载（重启后自动挂载）
echo "none /dev/mqueue mqueue defaults 0 0" | sudo tee -a /etc/fstab
```

**验证：**
```bash
mount | grep mqueue
```

---

## ✅ 已满足的依赖

以下依赖项**已经满足**，无需操作：

- ✅ phpize 可用
- ✅ pthread, dl, rt 系统库
- ✅ PHP 7.0.33（版本兼容）
- ✅ json 扩展
- ✅ curl 扩展
- ✅ mysqli 扩展
- ✅ redis 扩展
- ✅ memcached 扩展
- ✅ PHP 开发头文件（php_json.h, php.h, zend_API.h）
- ✅ C++11 支持

---

## ❌ 不需要的依赖

根据您的说明，以下扩展**不需要**，可以忽略警告：

- ⚠ PDO 扩展 - 不需要
- ⚠ YAR 扩展 - 不需要
- ✅ gRPC 扩展 - 已安装但不需要

---

## 🚀 完整的修复步骤

### 方案 1：自动修复（推荐）

上传并运行修复脚本：

```bash
# 上传 quick_fix.sh 到服务器
# 然后运行：
chmod +x quick_fix.sh
./quick_fix.sh
```

### 方案 2：手动修复

```bash
# 1. 确认 PHP Thread Safety 状态
php -i | grep "Thread Safety"
# 如果是 enabled，需要重新安装 PHP（见上面的步骤）

# 2. 安装缺失的头文件
sudo yum install -y php-devel php-common

# 3. 挂载消息队列
sudo mkdir -p /dev/mqueue
sudo mount -t mqueue none /dev/mqueue
echo "none /dev/mqueue mqueue defaults 0 0" | sudo tee -a /etc/fstab

# 4. 验证所有依赖
./check_only.sh  # 或者重新运行您的检查脚本
```

---

## 📋 构建前的最终检查

完成上述修复后，确认以下所有项目：

```bash
# 1. PHP 非ZTS
php -i | grep "Thread Safety"
# 期望输出：Thread Safety => disabled

# 2. php_curl.h 存在
find /usr/include -name "php_curl.h"
# 应该找到文件

# 3. 消息队列已挂载
mount | grep mqueue
# 应该显示 /dev/mqueue 挂载信息

# 4. 基本工具可用
phpize --version
g++ --version
autoconf --version
```

---

## 🎯 如果一切正常，开始构建

```bash
cd /path/to/SkyAPM-php-sdk

phpize
./configure
make -j$(nproc)
sudo make install
```

---

## 🔧 故障排查

### 如果 configure 失败

**错误：** `Could not find php_json.h`

```bash
# 解决方案
sudo yum install -y php-devel
```

**错误：** `compiler not accept c++11`

```bash
# 检查 GCC 版本
g++ --version

# 如果版本太老，安装 devtoolset
sudo yum install -y devtoolset-7
scl enable devtoolset-7 bash
```

**错误：** `skywalking does not support ZTS`

这说明您的 PHP 是 ZTS 版本，需要按照上面的步骤重新安装非 ZTS 版本。

---

## 📞 需要帮助？

如果修复后仍有问题：

1. 重新运行检查脚本：`./check_only.sh`
2. 查看详细日志：运行修复脚本时的输出
3. 检查 PHP 配置：`php -i` 的完整输出
4. 检查系统日志：`/var/log/messages`

---

## 总结

您的情况：

| 项目 | 状态 | 操作 |
|------|------|------|
| PHP 版本 | ✅ 7.0.33 | 无需操作 |
| PHP ZTS | ⚠️ 需确认 | 运行 `php -i \| grep "Thread Safety"` |
| php_curl.h | ❌ 缺失 | `sudo yum install -y php-devel` |
| 消息队列 | ⚠️ 未挂载 | `sudo mount -t mqueue none /dev/mqueue` |
| PDO | ⚠️ 缺失 | 忽略（您不需要） |
| YAR | ⚠️ 缺失 | 忽略（您不需要） |

**最关键的步骤**：确认 PHP 是否真的是 ZTS 版本。如果不是，那只是误报，您只需安装 php-devel 和挂载消息队列就可以了。
