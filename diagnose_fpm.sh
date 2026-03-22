#!/bin/bash
# PHP-FPM 重复初始化日志诊断脚本

echo "========================================"
echo "PHP-FPM 重复初始化日志诊断"
echo "========================================"
echo ""

# 1. 检查 PHP-FPM 状态
echo "1. 检查 PHP-FPM 进程状态"
echo "-----------------------------------"
if command -v systemctl &> /dev/null; then
    systemctl status php-fpm | head -20
elif command -v service &> /dev/null; then
    service php-fpm status | head -20
else
    echo "无法检测 PHP-FPM 状态"
fi
echo ""

# 2. 统计 PHP-FPM 进程数
echo "2. 统计 PHP-FPM worker 进程数"
echo "-----------------------------------"
php_fpm_count=$(ps aux | grep "php-fpm: pool" | grep -v grep | wc -l)
echo "当前 PHP-FPM worker 进程数: $php_fpm_count"
echo ""

# 3. 检查 PHP-FPM 配置
echo "3. 检查 PHP-FPM 配置"
echo "-----------------------------------"
php_fpm_conf_files=(
    "/etc/php-fpm.conf"
    "/etc/php-fpm.d/www.conf"
    "/etc/php/7.0/fpm/pool.d/www.conf"
    "/etc/php/7.1/fpm/pool.d/www.conf"
    "/etc/php/7.2/fpm/pool.d/www.conf"
    "/etc/php/7.3/fpm/pool.d/www.conf"
    "/etc/php/7.4/fpm/pool.d/www.conf"
    "/etc/php/8.0/fpm/pool.d/www.conf"
)

for conf_file in "${php_fpm_conf_files[@]}"; do
    if [ -f "$conf_file" ]; then
        echo "找到配置文件: $conf_file"
        echo "pm = $(grep "^pm\s*=" "$conf_file" | head -1)"
        echo "pm.max_children = $(grep "^pm.max_children\s*=" "$conf_file" | head -1)"
        echo "pm.start_servers = $(grep "^pm.start_servers\s*=" "$conf_file" | head -1)"
        echo "pm.min_spare_servers = $(grep "^pm.min_spare_servers\s*=" "$conf_file" | head -1)"
        echo "pm.max_spare_servers = $(grep "^pm.max_spare_servers\s*=" "$conf_file" | head -1)"
        echo "pm.max_requests = $(grep "^pm.max_requests\s*=" "$conf_file" | head -1)"
        echo ""
        break
    fi
done
echo ""

# 4. 检查最近的 SkyWalking 日志
echo "4. 检查最近的 SkyWalking 日志（最后 20 行）"
echo "-----------------------------------"
if [ -f "/tmp/skywalking-php.log" ]; then
    tail -20 /tmp/skywalking-php.log
else
    echo "日志文件不存在: /tmp/skywalking-php.log"
fi
echo ""

# 5. 统计初始化日志次数
echo "5. 统计初始化日志出现次数"
echo "-----------------------------------"
if [ -f "/tmp/skywalking-php.log" ]; then
    init_count=$(grep -c "SqliteStorage: initialized" /tmp/skywalking-php.log)
    echo "初始化日志出现次数: $init_count"
    echo ""
    echo "最近 10 次初始化日志的时间戳："
    grep "SqliteStorage: initialized" /tmp/skywalking-php.log | tail -10
else
    echo "日志文件不存在: /tmp/skywalking-php.log"
fi
echo ""

# 6. 检查 PHP-FPM 错误日志
echo "6. 检查 PHP-FPM 错误日志（最后 20 行）"
echo "-----------------------------------"
php_fpm_error_logs=(
    "/var/log/php-fpm/error.log"
    "/var/log/php7.0-fpm.log"
    "/var/log/php7.1-fpm.log"
    "/var/log/php7.2-fpm.log"
    "/var/log/php7.3-fpm.log"
    "/var/log/php7.4-fpm.log"
    "/var/log/php8.0-fpm.log"
    "/var/log/php-fpm/www-error.log"
)

for error_log in "${php_fpm_error_logs[@]}"; do
    if [ -f "$error_log" ]; then
        echo "找到错误日志: $error_log"
        tail -20 "$error_log"
        echo ""
        break
    fi
done
echo ""

# 7. 检查系统日志中的 OOM killer 记录
echo "7. 检查系统日志中的 OOM killer 记录"
echo "-----------------------------------"
if command -v dmesg &> /dev/null; then
    dmesg | grep -i "out of memory\|oom killer" | tail -5
elif [ -f "/var/log/messages" ]; then
    grep -i "out of memory\|oom killer" /var/log/messages | tail -5
elif [ -f "/var/log/syslog" ]; then
    grep -i "out of memory\|oom killer" /var/log/syslog | tail -5
else
    echo "无法检查系统日志"
fi
echo ""

# 8. 检查 skywalking.so 扩展信息
echo "8. 检查 skywalking.so 扩展信息"
echo "-----------------------------------"
php -m | grep skywalking
if [ $? -eq 0 ]; then
    echo "SkyWalking 扩展已加载"
    php --ri skywalking | head -20
else
    echo "SkyWalking 扩展未加载"
fi
echo ""

# 9. 监控日志输出（可选）
echo "9. 监控日志输出（10 秒）"
echo "-----------------------------------"
echo "将在 10 秒内监控日志输出..."
echo "按 Ctrl+C 可提前退出"
echo ""

if [ -f "/tmp/skywalking-php.log" ]; then
    initial_count=$(wc -l < /tmp/skywalking-php.log)
    timeout 10 tail -f /tmp/skywalking-php.log || true
    final_count=$(wc -l < /tmp/skywalking-php.log)
    new_lines=$((final_count - initial_count))
    echo ""
    echo "10 秒内新增日志行数: $new_lines"
else
    echo "日志文件不存在: /tmp/skywalking-php.log"
fi
echo ""

# 10. 建议和下一步
echo "========================================"
echo "诊断建议"
echo "========================================"
echo ""
echo "根据以上诊断结果，可能的问题和解决方案："
echo ""
echo "1. 如果看到大量重复的初始化日志："
echo "   - 确认已应用代码修复（重新编译和安装扩展）"
echo "   - 检查 PHP-FPM 配置中的 pm.max_requests 设置"
echo ""
echo "2. 如果 PHP-FPM worker 进程数频繁变化："
echo "   - 检查系统资源（内存、CPU）"
echo "   - 调整 pm.max_children、pm.start_servers 等参数"
echo ""
echo "3. 如果看到 OOM killer 记录："
echo "   - 系统内存不足，需要增加内存或减少 PHP-FPM 进程数"
echo ""
echo "4. 如果看到 PHP-FPM 错误日志中的 segfault 或崩溃："
echo "   - 扩展可能存在 bug，需要使用 gdb 调试"
echo "   - 检查是否与 PHP 版本或其他扩展冲突"
echo ""
echo "5. 应用修复的步骤："
echo "   cd /root/SkyAPM-php-sdk"
echo "   make clean && phpize --clean"
echo "   phpize && ./configure && make -j\$(nproc)"
echo "   make install"
echo "   service php-fpm restart"
echo ""
