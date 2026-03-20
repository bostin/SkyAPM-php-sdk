# Config SkyWalking PHP Agent

## Add SkyWalking config to php.ini and restart php-fpm

```shell
; Loading extensions in PHP
extension=skywalking.so

; enable skywalking
skywalking.enable = 1

; Set skyWalking collector version (for SW8 header protocol: 5, 6, 7, or 8)
skywalking.version = 8

; Set app code e.g. MyProjectName
skywalking.app_code = the_skywalking_php_agent

; File logging configuration (optional)
skywalking.log_file_path = /var/log/skywalking
skywalking.log_file_max_size = 10485760  ; 10MB per file
skywalking.log_file_max_files = 100      ; keep 100 files max

; Optional: Enable internal debug logging
skywalking.log_enable = 0
skywalking.log_path = /tmp/skywalking-php.log

; Optional: Enable error handler tracking
skywalking.error_handler_enable = 0

; Optional: Sampling rate (-1 for unlimited, or N traces per 3 seconds)
skywalking.sample_n_per_3_secs = -1

; Optional: Fixed instance name (auto-generated if empty)
skywalking.instance_name = ""
```



## Important

1. **File-Based Mode**: This version writes trace data to local JSON files instead of sending to SkyWalking OAP server

2. **Trace Files**: Traces are saved as `skywalking-{timestamp}-{traceid}.json` in the configured log directory

3. **File Rotation**: Old trace files are automatically removed based on `log_file_max_files` setting

4. **No Server Required**: This version does not require SkyWalking OAP server or gRPC connection

5. **Debug Logging**: Enable `skywalking.log_enable` for troubleshooting (writes to separate log file)
