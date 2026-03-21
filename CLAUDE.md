# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SkyAPM PHP is a PHP extension (C++) that provides distributed tracing instrumentation for Apache SkyWalking. It intercepts PHP function calls using the Zend Engine to automatically collect traces without requiring application code changes.

**Current Branch:** `v4.2.0_local_v2` - SQLite single-file storage version

### Web Visualization System

- **`visualization/frontend/`** - Vue 3 frontend application
- **`visualization/server/`** - Node.js Express API server
- **Technology Stack**: Vue 3 + TypeScript + Element Plus (frontend), Node.js + Express (backend)
- **Data Source**: SQLite database (default) or JSON files (legacy)

## Architecture

This is a **PHP extension written in C++** that uses JSON-based protocol to communicate with SkyWalking OAP server. The architecture consists of:

### Core Components

- **`src/sky_module.cc`** - Extension entry point, initializes Zend hooks and intercepts execution
- **`src/manager.cc/h`** - Manages connection to SkyWalking OAP, handles login/heartbeat
- **`src/segment.cc/h`** - Trace segment container that holds multiple spans for a single request
- **`src/span.cc/h`** - Individual span representation with operation name, tags, logs
- **`src/sky_execute.cc/h`** - Zend execution hook for intercepting function calls
- **`src/sky_shm.cc/h`** - Shared memory management for cross-process data
- **`src/json_builder.cc/h`** - JSON serialization for SkyWalking protocol (replaces protobuf)

### Plugin System

Instrumentation for specific libraries is implemented via plugins in `src/sky_plugin_*.cc`:
- `sky_plugin_curl.cc` - CURL HTTP requests
- `sky_plugin_mysqli.cc` - MySQLi database queries
- `sky_plugin_redis.cc` - Redis extension
- `sky_plugin_predis.cc` - Predis client
- `sky_plugin_memcached.cc` - Memcached
- `sky_plugin_yar.cc` - Yar RPC
- `sky_plugin_rabbit_mq.cc` - RabbitMQ
- `sky_plugin_swoole_curl.cc` - Swoole coroutine CURL
- `sky_plugin_hyperf_guzzle.cc` - Hyperf framework Guzzle
- `sky_plugin_error.cc` - Error tracking

**Removed Dependencies (v4.2.0_local):**
- PDO plugin (`sky_pdo.cc/h`) - No longer supported
- gRPC plugin (`sky_plugin_grpc.cc/h`) - No longer supported

### Build System

- **`config.m4`** - PHP extension build configuration (used by phpize)
- **`docker/`** - Docker image definitions for pre-built extensions

## Building from Source

**This version does NOT require gRPC C++ library** - uses JSON-based protocol instead.

### Prerequisites

**Linux (Amazon Linux 1):**
```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y autoconf automake libtool make gcc-c++
sudo yum install -y boost-devel boost-static
sudo yum install -y libcurl-devel openssl-devel sqlite-devel
sudo yum install -y php php-devel php-pear
sudo yum install -y php-json php-curl php-process
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install build-essential autoconf automake libtool make g++
sudo apt-get install libboost-all-dev libcurl-dev openssl-dev libsqlite3-dev
sudo apt-get install php php-dev php-json php-curl
```

**macOS:**
```bash
xcode-select --install
brew install autoconf automake libtool boost sqlite3
```

**Alpine:**
```bash
apk add --no-cache autoconf automake libtool cmake g++ make file linux-headers re2c pkgconf openssl curl boost-dev php-dev php-json php-curl sqlite-dev
```

### Build PHP Extension

```bash
cd /path/to/SkyAPM-php-sdk
phpize
./configure
make -j$(nproc)
sudo make install
```

**Note:** No `--with-grpc` option needed - this version uses built-in JSON protocol.

### Configuration (php.ini)

```ini
extension=skywalking.so
skywalking.enable = 1
skywalking.version = 8
skywalking.app_code = my_application
skywalking.log_file_path = /var/log/skywalking

; Storage configuration
skywalking.storage_type = sqlite     ; sqlite (default) or json (legacy)
skywalking.sqlite_max_size_mb = 1024 ; Max SQLite database size (0 = unlimited)
skywalking.retention_days = 7        ; Data retention in days (0 = permanent)
```

## Dependency Check Script

Use the provided dependency check script for your environment:

```bash
# For Amazon Linux 1 + PHP 7.0 NTS
./check_dependencies.sh
```

This script verifies:
- Build tools (g++, autoconf, automake, libtool, make, phpize)
- System libraries (pthread, dl, rt, boost)
- PHP 7.0 NTS environment
- Required PHP extensions (json, curl)
- PHP development headers
- C++11 support

## Running the Visualization System

### Development Mode

**Frontend:**
```bash
cd visualization/frontend
pnpm install
pnpm dev
# Runs on http://localhost:5173
```

**Backend:**
```bash
cd visualization/server
pnpm install
pnpm dev
# Runs on http://localhost:3000
```

### Production Deployment

1. Build the frontend:
```bash
cd visualization/frontend
pnpm build
```

2. Start the backend (serves both API and static files):
```bash
cd visualization/server
pnpm build && pnpm start
```

3. Access the application at http://localhost:3000

## Testing

The project uses end-to-end tests that verify traces are properly sent to SkyWalking OAP.

### Running E2E Tests

```bash
cd e2e
composer install
# Start services (OAP, MySQL, Redis, Memcached) via docker-compose or CI
php e2e.php <php-version>
```

The E2E test (`e2e/e2e.php`) makes HTTP requests to test applications and queries the SkyWalking GraphQL API to verify traces are captured.

### CI Testing

GitHub Actions (`.github/workflows/ci.yml`) tests against:
- PHP versions: 7.0, 7.1, 7.2, 7.3, 7.4, 8.0
- SkyWalking versions: 8.0.0-es6 through 8.5.0-es6
- Services: MySQL, Redis, Memcached

## Key Implementation Details

### How Tracing Works

1. **Request Init**: When a PHP request starts, a new `Segment` is created with a unique trace ID
2. **Function Interception**: Zend engine hooks (`sky_execute_ex`, `sky_execute_internal`) intercept function calls
3. **Plugin Detection**: Plugins check if intercepted function matches their target (e.g., `curl_exec`)
4. **Span Creation**: Matching plugin creates a span, captures function arguments/return values
5. **Cross-Process Propagation**: For outbound calls (CURL), SW8 header is injected
6. **Segment Serialization**: At request end, segment is serialized to **JSON** (not protobuf)
7. **Send**: Segment sent to SkyWalking OAP via HTTP/gRPC

### Memory Management

- Uses shared memory (POSIX message queues) for communication between PHP processes and background threads
- Each PHP process has its own segment store keyed by request ID
- Rate limiting (`sky_rate_limit.cc`) prevents excessive tracing during high load

### Protocol Changes (v4.2.0_local)

**Removed:** gRPC C++ library dependency and protobuf compilation
**Added:** JSON-based protocol using `src/json_builder.cc/h`
**Benefits:** Simpler build process, fewer dependencies, easier deployment

### Storage System (v4.2.0_local_v2)

The extension supports two storage backends:
- **SQLite** (default): Single-file database storage with SQL query capabilities
- **JSON** (legacy): Multi-file JSON storage for backward compatibility

**Storage Files:**
- `src/storage/storage_interface.h` - Storage abstraction interface
- `src/storage/sqlite_storage.cc/h` - SQLite storage implementation
- `src/storage/json_storage.cc/h` - JSON file storage (legacy)

**SQLite Benefits:**
- Single file storage (no inode exhaustion)
- SQL query support for analytics
- WAL mode for better concurrency
- Built-in indexing for fast lookups

**Database Schema:**
```sql
CREATE TABLE traces (
    id INTEGER PRIMARY KEY,
    trace_id TEXT UNIQUE,
    trace_segment_id TEXT,
    service TEXT,
    service_instance TEXT,
    start_time INTEGER,
    end_time INTEGER,
    duration_ms INTEGER,
    status_code INTEGER,
    is_error INTEGER,
    url TEXT,
    full_json TEXT,
    created_at INTEGER
);
```

## Supported Frameworks and Libraries

- **HTTP**: CURL (ext-curl), Guzzle (via Hyperf plugin), Swoole CURL
- **Database**: MySQLi (PDO support removed)
- **Cache**: Redis (ext-redis), Predis, Memcached (ext-memcached)
- **RPC**: Yar (ext-yar client & server) - gRPC support removed
- **Message Queue**: RabbitMQ (php-amqplib)
- **Frameworks**: Hyperf, Swoft, Tars-php, LaravelS

## Environment-Specific Notes

### Amazon Linux 1 + PHP 7.0 NTS

This branch (`v4.2.0_local_v2`) is optimized for:
- **OS**: Amazon Linux 1
- **PHP**: 7.0 Non-Thread Safe (NTS)
- **Removed Dependencies**: PDO, gRPC plugins
- **New Dependency**: SQLite3 library

Ensure PHP is non-ZTS:
```bash
php -i | grep "Thread Safety"
# Should show: Thread Safety => disabled
```

## Debugging

Enable logging in php.ini:
```ini
skywalking.enable = 1
; Log file path depends on system
skywalking.log_file_path = /tmp/skywalking
```

Check the log file for:
- Connection issues to SkyWalking OAP
- Segment serialization errors
- Plugin registration status

## Development Notes

### Adding New Plugins

1. Create plugin source files in `src/sky_plugin_*.cc` and `src/sky_plugin_*.h`
2. Add source files to `config.m4` in the `PHP_NEW_EXTENSION` macro
3. Add includes in `src/sky_execute.cc`
4. Add plugin detection logic in `src/sky_execute.cc`
5. Update `package.xml` with new files

### JSON Protocol

The `src/json_builder.cc` handles serialization of trace data to JSON format compatible with SkyWalking OAP. Key structures:
- Trace ID generation
- Span serialization with tags and logs
- Segment formatting
- Proper escaping of special characters

## Troubleshooting

### Build Errors

**Missing php_json.h:**
```bash
# Ensure php-dev and php-json are installed
sudo yum install php-devel php-json  # Amazon Linux
sudo apt-get install php-dev php-json  # Ubuntu
```

**Boost library not found:**
```bash
sudo yum install boost-devel  # Amazon Linux
sudo apt-get install libboost-all-dev  # Ubuntu
```

**SQLite3 library not found:**
```bash
sudo yum install sqlite-devel  # Amazon Linux
sudo apt-get install libsqlite3-dev  # Ubuntu
```

**C++11 not supported:**
- Ensure gcc-c++ is up to date
- Amazon Linux 1: `sudo yum update gcc-c++`

### Runtime Issues

**POSIX message queue not available:**
```bash
sudo mkdir -p /dev/mqueue
sudo mount -t mqueue none /dev/mqueue
echo "none /dev/mqueue mqueue defaults 0 0" | sudo tee -a /etc/fstab
```

**ZTS (Thread Safety) not supported:**
- This extension only works with non-ZTS PHP builds
- Check with: `php -i | grep "Thread Safety"`
