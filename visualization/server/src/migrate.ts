#!/usr/bin/env node

/**
 * Migration Script: Import JSON trace files into SQLite database
 *
 * Usage:
 *   pnpm tsx src/migrate.ts [db_path] [output_dir]
 *
 * Arguments:
 *   db_path     - SQLite database path (default: skywalking_traces.db in project root)
 *   output_dir  - Directory containing JSON trace files (default: ../..output in project root)
 *
 * Example:
 *   pnpm tsx src/migrate.ts ./traces.db ./output
 */

import Database from 'better-sqlite3';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Parse command line arguments
const args = process.argv.slice(2);
const DB_PATH = args[0] || path.resolve(__dirname, '../../..', 'skywalking_traces.db');
const OUTPUT_DIR = args[1] || path.resolve(__dirname, '../../..', 'output');

interface TraceData {
    traceId: string;
    traceSegmentId: string;
    service: string;
    serviceInstance: string;
    spans: any[];
}

async function migrate(): Promise<void> {
    console.log('=== SkyWalking Trace Migration Tool ===');
    console.log(`Database: ${DB_PATH}`);
    console.log(`Source directory: ${OUTPUT_DIR}`);
    console.log('');

    // Check if source directory exists
    if (!fs.existsSync(OUTPUT_DIR)) {
        console.error(`Error: Source directory not found: ${OUTPUT_DIR}`);
        process.exit(1);
    }

    // Get list of JSON files
    const files = fs.readdirSync(OUTPUT_DIR).filter(f => f.endsWith('.json'));
    if (files.length === 0) {
        console.log('No JSON files found to migrate.');
        process.exit(0);
    }

    console.log(`Found ${files.length} JSON files to migrate.`);
    console.log('');

    // Ensure database directory exists
    const dbDir = path.dirname(DB_PATH);
    if (!fs.existsSync(dbDir)) {
        fs.mkdirSync(dbDir, { recursive: true });
    }

    // Open database
    const db = new Database(DB_PATH);

    // Enable WAL mode
    db.pragma('journal_mode = WAL');

    // Create tables
    db.exec(`
        CREATE TABLE IF NOT EXISTS traces (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            trace_id TEXT NOT NULL UNIQUE,
            trace_segment_id TEXT NOT NULL,
            service TEXT NOT NULL,
            service_instance TEXT NOT NULL,
            start_time INTEGER NOT NULL,
            end_time INTEGER NOT NULL,
            duration_ms INTEGER NOT NULL,
            status_code INTEGER DEFAULT 200,
            is_error INTEGER DEFAULT 0,
            url TEXT,
            full_json TEXT NOT NULL,
            created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)
        );

        CREATE INDEX IF NOT EXISTS idx_trace_id ON traces(trace_id);
        CREATE INDEX IF NOT EXISTS idx_start_time ON traces(start_time DESC);
        CREATE INDEX IF NOT EXISTS idx_duration ON traces(duration_ms DESC);
        CREATE INDEX IF NOT EXISTS idx_url ON traces(url);
        CREATE INDEX IF NOT EXISTS idx_service ON traces(service);
    `);

    // Prepare insert statement
    const insert = db.prepare(`
        INSERT OR IGNORE INTO traces
        (trace_id, trace_segment_id, service, service_instance,
         start_time, end_time, duration_ms, status_code, is_error, url, full_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    `);

    // Statistics
    let successCount = 0;
    let errorCount = 0;
    let skipCount = 0;
    const errors: { file: string; error: string }[] = [];

    // Process files
    const progressBar = createProgressBar(files.length);
    const startTime = Date.now();

    for (let i = 0; i < files.length; i++) {
        const file = files[i];

        try {
            const filePath = path.join(OUTPUT_DIR, file);
            const content = fs.readFileSync(filePath, 'utf8');
            const data: TraceData = JSON.parse(content);

            // Validate required fields
            if (!data.traceId || !data.spans || !Array.isArray(data.spans)) {
                errorCount++;
                errors.push({ file, error: 'Invalid trace format' });
                progressBar.tick({ status: 'error' });
                continue;
            }

            // Extract metadata
            const startTimeMs = data.spans[0]?.startTime || 0;
            const endTimeMs = data.spans[data.spans.length - 1]?.endTime || 0;
            const duration = endTimeMs - startTimeMs;
            const url = extractUrl(data);
            const isError = data.spans.some((s: any) => s.isError === true);
            const statusCode = extractStatusCode(data);

            // Insert into database
            insert.run(
                data.traceId,
                data.traceSegmentId || data.traceId,
                data.service || 'unknown',
                data.serviceInstance || 'unknown',
                startTimeMs,
                endTimeMs,
                duration,
                statusCode,
                isError ? 1 : 0,
                url,
                content
            );

            successCount++;
            progressBar.tick({ status: 'ok' });
        } catch (e) {
            errorCount++;
            errors.push({ file, error: (e as Error).message });
            progressBar.tick({ status: 'error' });
        }
    }

    // Print summary
    console.log('\n');
    console.log('=== Migration Summary ===');
    console.log(`Total files: ${files.length}`);
    console.log(`Imported: ${successCount}`);
    console.log(`Skipped: ${skipCount}`);
    console.log(`Errors: ${errorCount}`);
    console.log(`Time elapsed: ${((Date.now() - startTime) / 1000).toFixed(2)}s`);

    // Print database stats
    const stats = db.prepare('SELECT COUNT(*) as count FROM traces').get() as { count: number };
    console.log(`Total traces in database: ${stats.count}`);

    // Print errors if any
    if (errors.length > 0) {
        console.log('\n=== Errors ===');
        for (const err of errors.slice(0, 10)) {
            console.log(`  ${err.file}: ${err.error}`);
        }
        if (errors.length > 10) {
            console.log(`  ... and ${errors.length - 10} more errors`);
        }
    }

    // Close database
    db.close();

    console.log('\nMigration completed!');
}

/**
 * Extract URL from trace data
 */
function extractUrl(data: TraceData): string | null {
    if (!data.spans || data.spans.length === 0) {
        return null;
    }

    // First look for entry span
    for (const span of data.spans) {
        if (span.spanType === 0 && span.operationName) { // Entry span
            return span.operationName;
        }
    }

    // Fall back to first span
    return data.spans[0]?.operationName || null;
}

/**
 * Extract status code from trace data
 */
function extractStatusCode(data: TraceData): number {
    if (!data.spans) {
        return 200;
    }

    for (const span of data.spans) {
        if (span.tags && Array.isArray(span.tags)) {
            for (const tag of span.tags) {
                if (tag.key === 'status_code') {
                    const parsed = parseInt(tag.value, 10);
                    return isNaN(parsed) ? 200 : parsed;
                }
            }
        }
    }

    return 200;
}

/**
 * Create a simple progress bar for terminal
 */
function createProgressBar(total: number) {
    const width = 40;
    let lastPercentage = 0;
    let current = 0;

    return {
        tick: (opts?: { status?: string }) => {
            current++;
            const percentage = Math.floor(current / total * 100);
            if (percentage !== lastPercentage) {
                const filled = Math.floor(width * percentage / 100);
                const bar = '█'.repeat(filled) + '░'.repeat(width - filled);
                process.stdout.write(`\r[${bar}] ${percentage}% (${current}/${total})`);
                if (opts?.status) {
                    process.stdout.write(` ${opts.status}`);
                }
                lastPercentage = percentage;
            }
        }
    };
}

// Run migration
migrate().catch(console.error);
