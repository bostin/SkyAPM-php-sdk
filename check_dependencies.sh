#!/bin/bash

# SkyAPM PHP SDK - Dependency Check Script for Amazon Linux 1 + PHP 7.0 NTS
# Simplified version without pdo/grpc checks

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
PASSED=0
FAILED=0

# Function to print check result
print_result() {
    local status=$1
    local message=$2

    if [ "$status" = "PASS" ]; then
        echo -e "${GREEN}✓ $message${NC}"
        PASSED=$((PASSED + 1))
    elif [ "$status" = "FAIL" ]; then
        echo -e "${RED}✗ $message${NC}"
        FAILED=$((FAILED + 1))
    else
        echo -e "${YELLOW}⚠ $message${NC}"
    fi
}

# Function to check command exists
check_command() {
    local cmd=$1
    if command -v "$cmd" &> /dev/null; then
        print_result "PASS" "$cmd is installed"
        return 0
    else
        print_result "FAIL" "$cmd is NOT installed"
        return 1
    fi
}

# Function to check library
check_library() {
    local lib=$1
    if ldconfig -p 2>/dev/null | grep -q "$lib"; then
        print_result "PASS" "Library $lib found"
        return 0
    else
        print_result "FAIL" "Library $lib NOT found"
        return 1
    fi
}

echo "======================================"
echo "SkyAPM PHP SDK - Dependency Check"
echo "Platform: Amazon Linux 1 + PHP 7.0 NTS"
echo "======================================"
echo ""

# Detect OS
if [ -f /etc/system-release ]; then
    RELEASE=$(cat /etc/system-release)
    echo "Detected: $RELEASE"
    echo ""
fi

# Build Tools
echo "1. Build Tools"
echo "--------------------------------------"
check_command "g++"
check_command "autoconf"
check_command "automake"
check_command "libtool"
check_command "make"
check_command "phpize"
echo ""

# System Libraries
echo "2. System Libraries"
echo "--------------------------------------"
check_library "pthread"
check_library "dl"
check_library "rt"
check_library "boost"
echo ""

# PHP Environment
echo "3. PHP Environment"
echo "--------------------------------------"
if command -v php &> /dev/null; then
    PHP_VERSION=$(php -v | head -n 1)
    print_result "PASS" "PHP: $PHP_VERSION"

    # Check if PHP 7.0
    if php -v | grep -q "PHP 7.0"; then
        print_result "PASS" "PHP 7.0 detected"
    else
        echo -e "${YELLOW}⚠ Warning: Expected PHP 7.0${NC}"
    fi

    # Check if non-ZTS
    if php -i | grep -q "Thread Safety => disabled"; then
        print_result "PASS" "PHP is non-ZTS (NTS)"
    else
        print_result "FAIL" "PHP is ZTS (not compatible)"
    fi
else
    print_result "FAIL" "PHP is NOT installed"
fi
echo ""

# PHP Extensions (required only)
echo "4. PHP Extensions"
echo "--------------------------------------"
if command -v php &> /dev/null; then
    if php -m 2>/dev/null | grep -q "^json$"; then
        print_result "PASS" "PHP extension json is loaded"
    else
        print_result "FAIL" "PHP extension json is NOT loaded"
    fi

    if php -m 2>/dev/null | grep -q "^curl$"; then
        print_result "PASS" "PHP extension curl is loaded"
    else
        print_result "FAIL" "PHP extension curl is NOT loaded"
    fi
fi
echo ""

# PHP Headers
echo "5. PHP Headers"
echo "--------------------------------------"
if command -v php &> /dev/null; then
    if [ -d "/usr/include/php" ] || [ -d "$(php-config --include-dir 2>/dev/null)" ]; then
        print_result "PASS" "PHP development headers are installed"
    else
        print_result "FAIL" "PHP development headers are NOT found"
    fi

    if find /usr/include -name "php_json.h" 2>/dev/null | grep -q .; then
        print_result "PASS" "php_json.h is found"
    else
        print_result "FAIL" "php_json.h is NOT found"
    fi
fi
echo ""

# System Capabilities
echo "6. System Capabilities"
echo "--------------------------------------"
echo 'int main() { return 0; }' | g++ -std=c++11 -x c++ -o /tmp/test_cpp11 - 2>/dev/null
if [ $? -eq 0 ]; then
    print_result "PASS" "C++11 support available"
    rm -f /tmp/test_cpp11
else
    print_result "FAIL" "C++11 support NOT available"
fi

if [ -d "/dev/mqueue" ]; then
    print_result "PASS" "POSIX message queue available"
else
    echo -e "${YELLOW}⚠ Message queue not mounted (optional)${NC}"
fi
echo ""

# Build Test
echo "7. Build Test"
echo "--------------------------------------"
if [ -f "./configure" ] || [ -f "config.m4" ]; then
    if phpize > /dev/null 2>&1; then
        print_result "PASS" "phpize successful"
        if ./configure > /dev/null 2>&1; then
            print_result "PASS" "configure successful"
            if make -j$(nproc) > /dev/null 2>&1; then
                print_result "PASS" "Build successful!"
                make clean > /dev/null 2>&1
            else
                print_result "FAIL" "Build failed during make"
            fi
        else
            print_result "FAIL" "Configure failed"
        fi
        phpize --clean > /dev/null 2>&1
    else
        print_result "FAIL" "phpize failed"
    fi
else
    echo -e "${YELLOW}⚠ Not in SkyAPM source directory${NC}"
fi
echo ""

# Summary
echo "======================================"
echo "Summary: $PASSED passed, $FAILED failed"
echo "======================================"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✓ All dependencies are satisfied!${NC}"
    echo ""
    echo "Build SkyAPM:"
    echo "  phpize"
    echo "  ./configure"
    echo "  make -j\$(nproc)"
    echo "  sudo make install"
    exit 0
else
    echo -e "${RED}✗ Some dependencies are missing${NC}"
    exit 1
fi
