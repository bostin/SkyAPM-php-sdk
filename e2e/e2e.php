<?php
/*
 * Copyright 2021 SkyAPM
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
require_once __DIR__ . '/vendor/autoload.php';

class E2E {
    public $logDir = '/tmp/skywalking_test';
    public $startTime;

    public function __construct() {
        $this->startTime = time();
    }

    public function info($msg) {
        echo chr(27) . "[1;34m" . $msg . chr(27) . "[0m\n";
    }

    public function call() {
        $ch = curl_init('http://127.0.0.1:8083/call');
        curl_exec($ch);
        if (curl_getinfo($ch, CURLINFO_HTTP_CODE) != 200) {
            return false;
        }
        return true;
    }

    public function cleanup() {
        // Clean up old test log files
        $files = glob($this->logDir . '/*.json');
        if (is_array($files)) {
            foreach ($files as $file) {
                if (filemtime($file) < $this->startTime) {
                    @unlink($file);
                }
            }
        }
    }

    public function verifyTraces() {
        // Wait for files to be written
        sleep(2);

        // Get all JSON files in log directory
        $files = glob($this->logDir . '/*.json');
        if (empty($files)) {
            $this->info('No JSON files found in ' . $this->logDir);
            return false;
        }

        $this->info('Found ' . count($files) . ' JSON trace files');

        // Verify each JSON file
        $validTraces = 0;
        foreach ($files as $file) {
            $content = file_get_contents($file);
            if ($content === false) {
                $this->info('Failed to read file: ' . $file);
                continue;
            }

            $json = json_decode($content, true);
            if ($json === null) {
                $this->info('Invalid JSON in file: ' . $file . ' - ' . json_last_error_msg());
                continue;
            }

            // Verify required fields
            if (!isset($json['traceId'])) {
                $this->info('Missing traceId in file: ' . $file);
                continue;
            }

            if (!isset($json['traceSegmentId'])) {
                $this->info('Missing traceSegmentId in file: ' . $file);
                continue;
            }

            if (!isset($json['service']) || $json['service'] !== 'skywalking') {
                $this->info('Invalid or missing service in file: ' . $file);
                continue;
            }

            if (!isset($json['spans']) || !is_array($json['spans'])) {
                $this->info('Missing or invalid spans in file: ' . $file);
                continue;
            }

            // Verify at least one span exists
            if (count($json['spans']) == 0) {
                $this->info('No spans found in file: ' . $file);
                continue;
            }

            // Verify span structure
            foreach ($json['spans'] as $span) {
                if (!isset($span['operationName'])) {
                    $this->info('Missing operationName in span');
                    continue 2;
                }
                if (!isset($span['spanType'])) {
                    $this->info('Missing spanType in span');
                    continue 2;
                }
                if (!isset($span['spanId'])) {
                    $this->info('Missing spanId in span');
                    continue 2;
                }
            }

            $validTraces++;
        }

        $this->info("Found $validTraces valid trace files");

        return $validTraces > 0;
    }

    public function verifyJsonStructure() {
        $files = glob($this->logDir . '/*.json');
        if (empty($files)) {
            return false;
        }

        // Read and validate one file structure in detail
        $content = file_get_contents($files[0]);
        $json = json_decode($content, true);

        // Check for optional arrays
        if (isset($json['tags']) && !is_array($json['tags'])) {
            $this->info('tags should be an array');
            return false;
        }

        if (isset($json['logs']) && !is_array($json['logs'])) {
            $this->info('logs should be an array');
            return false;
        }

        if (isset($json['refs']) && !is_array($json['refs'])) {
            $this->info('refs should be an array');
            return false;
        }

        return true;
    }
}

$check = ['verifyTraces', 'verifyJsonStructure'];
$e2e = new E2E();
$e2e->cleanup();

foreach($check as $func) {
    $e2e->info('php version:' . $argv[1]);
    $e2e->info('exec ' . $func);

    $status = false;
    for($i = 1; $i <= 10; $i++) {
         $e2e->info("test $func $i/10...");
         $status = $e2e->call();
         if (!$status) {
            break;
         }
         sleep(1);
         $status = $e2e->$func();
         if ($status === true) {
             $status = true;
             break;
         }
         sleep(1);
    }

    if (!$status) {
        $e2e->info("test $func fail...");
        echo(file_get_contents("/var/log/php" . $argv[1] . "-fpm.log"));
        echo(file_get_contents("/tmp/skywalking-php.log"));
        system("sudo chmod -R +rwx /var/crash/*");
        exit(2);
    }

    $e2e->info("test $func success...");
}
