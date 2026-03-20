# Config SkyWalking PHP Agent

## Add SkyWalking config to php.ini and restart php-fpm

```shell
; Loading extensions in PHP
extension=skywalking.so

; enable skywalking
skywalking.enable = 1

; Set skyWalking collector version (5 or 6 or 7 or 8)
skywalking.version = 8

; Set app code e.g. MyProjectName
skywalking.app_code = the_skywalking_php_agent

; Set SkyWalking OAP server address
skywalking.grpc = 127.0.0.1:11800

; File logging configuration (optional)
skywalking.log_file_path = /var/log/skywalking
skywalking.log_file_max_size = 10485760  ; 10MB per file
skywalking.log_file_max_files = 100      ; keep 100 files max
```



## Important

1. Make sure php-fpm is running in foreground mode `--nodaemonize`

2. The extension sends trace data directly to SkyWalking OAP server via gRPC/HTTP

3. Application logs are written to the configured log directory for debugging

4. SkyWalking OAP server must be running and accessible at the configured address
