# 修复重复初始化日志问题

## 问题描述

在 PHP-FPM 环境下，`SqliteStorage: initialized` 日志不断重复输出。

## 根本原因

`SqliteStorage::initialize()` 函数缺少重复初始化检查，导致：
1. 每次调用都会重新打开数据库连接（`sqlite3_open`）
2. 每次调用都会重新准备 SQL 语句
3. 每次调用都会重新输出初始化日志

虽然使用了 `static bool s_init_log_printed` 来防止重复日志，但如果 `SqliteStorage` 对象本身被重复创建，每个新对象都有自己的静态变量，导致日志重复。

## 修复方案

### 1. 在 `SqliteStorage::initialize()` 中添加重复初始化检查

**文件**: `src/storage/sqlite_storage.cc`

**修改位置**: 函数开头（第 51 行之后）

```cpp
bool SqliteStorage::initialize() {
    std::lock_guard<std::mutex> lock(dbMutex_);

    // 防止重复初始化
    if (initialized_) {
        return true;
    }

    // 打开或创建数据库
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    // ... 其余代码
}
```

**效果**: 如果对象已经初始化过，直接返回成功，避免重复初始化。

### 2. 在 `SqliteStorage::shutdown()` 中重置初始化标志

**文件**: `src/storage/sqlite_storage.cc`

**修改位置**: 函数末尾（第 138 行之后）

```cpp
void SqliteStorage::shutdown() {
    std::lock_guard<std::mutex> lock(dbMutex_);

    finalizeStatements();

    if (db_) {
        // 执行最终的 checkpoint
        sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
        sqlite3_close(db_);
        db_ = nullptr;
    }

    // 重置初始化标志，允许重新初始化
    initialized_ = false;
}
```

**效果**: 允许对象在 shutdown 后重新初始化（虽然正常情况下不应该发生）。

## PHP-FPM 环境下的预期行为

### 正常情况
- 每个 PHP-FPM worker 进程启动时，会调用 `PHP_MINIT_FUNCTION`
- `PHP_MINIT_FUNCTION` 调用 `sky_module_init()`
- `sky_module_init()` 创建 `g_storage` 对象（每个进程独立）
- 每个 worker 进程输出**一次**初始化日志
- Worker 进程持续处理请求，不再输出初始化日志

### 如果仍然看到重复日志

如果应用此修复后仍然看到重复日志，可能的原因：

1. **PHP-FPM worker 进程频繁崩溃和重启**
   - 检查 PHP-FPM 日志：`tail -f /var/log/php-fpm/error.log`
   - 检查系统日志：`dmesg | tail` 或 `journalctl -xe`
   - 检查进程状态：`ps aux | grep php-fpm | wc -l`

2. **PHP-FPM 配置问题**
   - 检查 `pm.max_requests` 设置过小，导致频繁重启
   - 检查 `pm.max_children` 设置过大，导致系统资源不足

3. **扩展本身导致崩溃**
   - 检查是否有段错误或其他崩溃
   - 使用 gdb 调试：`gdb php`
   - 检查系统日志中的 OOM killer 记录

## 如何应用此修复

```bash
# 1. 进入源码目录
cd /path/to/SkyAPM-php-sdk

# 2. 清理旧的构建
make clean
phpize --clean

# 3. 重新配置和编译
phpize
./configure
make -j$(nproc)

# 4. 安装
make install

# 5. 重启 PHP-FPM
service php-fpm restart
# 或
systemctl restart php-fpm

# 6. 验证修复
tail -f /tmp/skywalking-php.log
```

## 验证修复效果

修复后，你应该看到：
- 每个启动的 worker 进程输出**一次**初始化日志
- 如果有 5 个 worker 进程，应该看到 5 次初始化日志
- 之后不再有重复的初始化日志

## 修改的文件

- `src/storage/sqlite_storage.cc` - 添加重复初始化检查和标志重置

## 测试建议

1. **单次测试**
   ```bash
   # 重启 PHP-FPM
   service php-fpm restart

   # 查看日志
   tail -20 /tmp/skywalking-php.log

   # 应该看到 N 次初始化日志（N = worker 进程数）
   ```

2. **长时间测试**
   ```bash
   # 监控日志 5 分钟
   timeout 300 tail -f /tmp/skywalking-php.log

   # 不应该看到重复的初始化日志
   ```

3. **压力测试**
   ```bash
   # 使用 ab 或 wrk 进行压力测试
   ab -n 10000 -c 10 http://your-app/

   # 同时监控日志
   tail -f /tmp/skywalking-php.log

   # 不应该看到重复的初始化日志
   ```
