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

; File logging configuration
skywalking.log_file_path = /var/log/skywalking
skywalking.log_file_max_size = 10485760  ; 10MB per file
skywalking.log_file_max_files = 100      ; keep 100 files max
```



## Important

1. Make sure php-fpm is running in foreground mode `--nodaemonize`

2. The extension writes trace data as JSON files to the configured log directory
3. File rotation is automatic based on the max_files configuration
4. Each trace file is named: `skywalking-{timestamp}-{traceid}.json`
