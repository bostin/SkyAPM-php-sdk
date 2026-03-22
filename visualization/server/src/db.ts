/**
 * SQLite Database Module
 * Initializes and manages SQLite database connection for trace storage
 */

import Database from 'better-sqlite3';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Database instance
let db: Database.Database | null = null;

/**
 * Initialize SQLite database
 */
export function initDatabase(dbPath?: string): Database.Database | null {
    if (db) {
        return db;
    }

    const finalPath = dbPath || path.resolve(__dirname, '../../..', 'skywalking_traces.db');
    const dbDir = path.dirname(finalPath);

    // Ensure directory exists
    if (!fs.existsSync(dbDir)) {
        fs.mkdirSync(dbDir, { recursive: true });
    }

    try {
        db = new Database(finalPath);

        // Enable WAL mode for better concurrency
        db.pragma('journal_mode = WAL');
        db.pragma('synchronous = NORMAL');
        db.pragma('cache_size = -64000'); // 64MB cache

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

        console.log(`SQLite database initialized: ${finalPath}`);
        return db;
    } catch (error) {
        console.error('Failed to initialize SQLite database:', error);
        return null;
    }
}

/**
 * Get database instance
 */
export function getDatabase(): Database.Database | null {
    return db;
}

/**
 * Close database connection
 */
export function closeDatabase(): void {
    if (db) {
        db.close();
        db = null;
    }
}

/**
 * Query traces from SQLite
 */
export function queryTraces(options: {
    url?: string;
    startTime?: number;
    endTime?: number;
    minDuration?: number;
    maxDuration?: number;
    sortBy?: string;
    order?: string;
    page?: number;
    pageSize?: number;
}): { data: any[]; total: number } {
    if (!db) {
        return { data: [], total: 0 };
    }

    let sql = 'SELECT * FROM traces WHERE 1=1';
    const params: any[] = [];

    if (options.url) {
        sql += ' AND url LIKE ?';
        params.push(`%${options.url}%`);
    }
    if (options.startTime) {
        sql += ' AND start_time >= ?';
        params.push(options.startTime);
    }
    if (options.endTime) {
        sql += ' AND end_time <= ?';
        params.push(options.endTime);
    }
    if (options.minDuration) {
        sql += ' AND duration_ms >= ?';
        params.push(options.minDuration);
    }
    if (options.maxDuration) {
        sql += ' AND duration_ms <= ?';
        params.push(options.maxDuration);
    }

    // Sorting
    if (options.sortBy === 'duration') {
        sql += ` ORDER BY duration_ms ${options.order === 'asc' ? 'ASC' : 'DESC'}`;
    } else {
        sql += ' ORDER BY start_time DESC';
    }

    // Get total count
    const countSql = sql.replace('SELECT *', 'SELECT COUNT(*) as count');
    const countResult = db.prepare(countSql).get(...params) as { count: number } | undefined;
    const total = countResult?.count || 0;

    // Pagination
    const page = options.page || 1;
    const pageSize = options.pageSize || 20;
    sql += ' LIMIT ? OFFSET ?';
    params.push(pageSize, (page - 1) * pageSize);

    const rows = db.prepare(sql).all(...params) as any[];

    const data = rows.map((row: any) => ({
        traceId: row.trace_id,
        traceSegmentId: row.trace_segment_id,
        service: row.service,
        serviceInstance: row.service_instance,
        duration: row.duration_ms,
        url: row.url,
        timestamp: row.start_time,
        spans: JSON.parse(row.full_json).spans,
        isError: row.is_error === 1,
        statusCode: row.status_code
    }));

    return { data, total };
}

/**
 * Get single trace by ID
 */
export function getTraceById(traceId: string): any | null {
    if (!db) {
        return null;
    }

    const row = db.prepare(
        'SELECT full_json FROM traces WHERE trace_id = ?'
    ).get(traceId) as { full_json: string } | undefined;

    return row ? JSON.parse(row.full_json) : null;
}

/**
 * Get unique URLs
 */
export function getUniqueUrls(): string[] {
    if (!db) {
        return [];
    }

    const rows = db.prepare(
        'SELECT DISTINCT url FROM traces WHERE url IS NOT NULL AND url != "" ORDER BY url'
    ).all() as { url: string }[];

    return rows.map(row => row.url);
}

/**
 * Get statistics
 */
export function getStats(): any {
    if (!db) {
        return {
            totalTraces: 0,
            totalDuration: 0,
            avgDuration: 0,
            maxDuration: 0,
            uniqueUrls: 0,
            services: 0
        };
    }

    const stats = db.prepare(`
        SELECT
            COUNT(*) as total_traces,
            COALESCE(SUM(duration_ms), 0) as total_duration,
            COALESCE(AVG(duration_ms), 0) as avg_duration,
            COALESCE(MAX(duration_ms), 0) as max_duration,
            COUNT(DISTINCT url) as unique_urls,
            COUNT(DISTINCT service) as services
        FROM traces
    `).get() as any;

    return {
        totalTraces: stats.total_traces || 0,
        totalDuration: stats.total_duration || 0,
        avgDuration: Math.round(stats.avg_duration || 0),
        maxDuration: stats.max_duration || 0,
        uniqueUrls: stats.unique_urls || 0,
        services: stats.services || 0
    };
}

/**
 * Get database file size
 */
export function getDbStats(): { size: number; count: number } {
    if (!db) {
        return { size: 0, count: 0 };
    }

    try {
        const dbPath = (db as any).name;
        const stats = fs.statSync(dbPath);
        const countResult = db.prepare('SELECT COUNT(*) as count FROM traces').get() as { count: number };
        return {
            size: stats.size,
            count: countResult?.count || 0
        };
    } catch {
        return { size: 0, count: 0 };
    }
}
