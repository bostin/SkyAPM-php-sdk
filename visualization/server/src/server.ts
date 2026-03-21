#!/usr/bin/env node

/**
 * SkyWalking PHP Visualizer Server
 * Node.js Express Server for tracing data visualization
 *
 * Supports dual data sources:
 * - SQLite database (default): DATA_SOURCE=sqlite
 * - JSON files: DATA_SOURCE=json
 */

import express from 'express';
import cors from 'cors';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import {
    initDatabase,
    queryTraces,
    getTraceById as getTraceByIdFromDb,
    getUniqueUrls as getUniqueUrlsFromDb,
    getStats as getStatsFromDb,
    getDbStats,
    closeDatabase
} from './db.js';

// Get current directory for ES modules
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;

// 数据源配置
const DATA_SOURCE = process.env.DATA_SOURCE || 'sqlite'; // 'sqlite' or 'json'

// 配置 - 数据目录
const TRACE_DIR = path.resolve(__dirname, '../../..', 'output');

// 中间件
app.use(cors());
app.use(express.json());

// 缓存配置（仅用于 JSON 模式）
const CACHE_TTL = 5000; // 5秒缓存
let traceCache: any[] | null = null;
let cacheTime = 0;

// 初始化数据库（SQLite 模式）
let dbInitialized = false;
if (DATA_SOURCE === 'sqlite') {
    initDatabase();
    dbInitialized = true;
}

// ============ JSON 文件解析函数（兼容旧模式）============

function parseTraceFiles() {
    const now = Date.now();
    if (traceCache && (now - cacheTime) < CACHE_TTL) {
        return traceCache;
    }

    const traces: any[] = [];

    if (!fs.existsSync(TRACE_DIR)) {
        console.log(`Trace directory not found: ${TRACE_DIR}`);
        return [];
    }

    const files = fs.readdirSync(TRACE_DIR).filter(f => f.endsWith('.json'));

    for (const file of files) {
        try {
            const filePath = path.join(TRACE_DIR, file);
            const stats = fs.statSync(filePath);
            const content = fs.readFileSync(filePath, 'utf8');
            const data = JSON.parse(content);

            // 计算总耗时
            let duration = 0;
            if (data.spans && data.spans.length > 0) {
                const start = data.spans[0].startTime;
                const end = data.spans[data.spans.length - 1].endTime;
                duration = end - start;
            }

            // 提取主 URL（第一个 span 的 operationName）
            const url = data.spans && data.spans.length > 0
                ? data.spans[0].operationName
                : 'unknown';

            traces.push({
                traceId: data.traceId,
                traceSegmentId: data.traceSegmentId,
                service: data.service,
                serviceInstance: data.serviceInstance,
                spans: data.spans || [],
                duration,
                url,
                timestamp: data.spans && data.spans.length > 0
                    ? data.spans[0].startTime
                    : stats.mtimeMs,
                fileName: file,
                fileSize: stats.size
            });
        } catch (e) {
            console.error(`Error parsing file ${file}:`, e);
        }
    }

    // 按时间降序排序（最新的在前）
    traces.sort((a, b) => b.timestamp - a.timestamp);

    traceCache = traces;
    cacheTime = now;

    return traces;
}

// ============ API 路由 ================

// 1. 获取 trace 列表（支持过滤/排序/分页）
app.get('/api/traces', (req, res) => {
    try {
        const page = parseInt(req.query.page?.toString() || '1');
        const pageSize = parseInt(req.query.pageSize?.toString() || '20');
        const sortBy = req.query.sortBy?.toString();
        const order = req.query.order?.toString();

        if (DATA_SOURCE === 'sqlite' && dbInitialized) {
            // SQLite 模式
            const result = queryTraces({
                url: req.query.url?.toString(),
                startTime: req.query.startTime ? parseInt(req.query.startTime.toString()) : undefined,
                endTime: req.query.endTime ? parseInt(req.query.endTime.toString()) : undefined,
                minDuration: req.query.minDuration ? parseInt(req.query.minDuration.toString()) : undefined,
                maxDuration: req.query.maxDuration ? parseInt(req.query.maxDuration.toString()) : undefined,
                sortBy,
                order,
                page,
                pageSize
            });

            res.json({
                success: true,
                data: result.data,
                pagination: {
                    total: result.total,
                    page,
                    pageSize,
                    totalPages: Math.ceil(result.total / pageSize)
                }
            });
        } else {
            // JSON 文件模式（兼容旧版）
            let traces = parseTraceFiles();

            // URL 过滤
            if (req.query.url) {
                const urlPattern = req.query.url.toString().toLowerCase();
                traces = traces.filter(t => t.url.toLowerCase().includes(urlPattern));
            }

            // 时间区间过滤
            if (req.query.startTime) {
                const startTime = parseInt(req.query.startTime.toString());
                traces = traces.filter(t => t.timestamp >= startTime);
            }

            if (req.query.endTime) {
                const endTime = parseInt(req.query.endTime.toString());
                traces = traces.filter(t => t.timestamp <= endTime);
            }

            // 耗时过滤
            if (req.query.minDuration) {
                const minDuration = parseInt(req.query.minDuration.toString());
                traces = traces.filter(t => t.duration >= minDuration);
            }

            if (req.query.maxDuration) {
                const maxDuration = parseInt(req.query.maxDuration.toString());
                traces = traces.filter(t => t.duration <= maxDuration);
            }

            // 排序
            if (sortBy === 'duration') {
                const orderNum = order === 'asc' ? 1 : -1;
                traces.sort((a, b) => orderNum * (a.duration - b.duration));
            } else if (sortBy === 'timestamp') {
                const orderNum = order === 'asc' ? 1 : -1;
                traces.sort((a, b) => orderNum * (a.timestamp - b.timestamp));
            }

            // 分页
            const total = traces.length;
            const startIndex = (page - 1) * pageSize;
            const endIndex = startIndex + pageSize;
            const paginatedTraces = traces.slice(startIndex, endIndex);

            res.json({
                success: true,
                data: paginatedTraces,
                pagination: {
                    total,
                    page,
                    pageSize,
                    totalPages: Math.ceil(total / pageSize)
                }
            });
        }
    } catch (e) {
        res.status(500).json({ success: false, message: (e as Error).message });
    }
});

// 2. 获取单个 trace 详情
app.get('/api/traces/:traceId', (req, res) => {
    try {
        if (DATA_SOURCE === 'sqlite' && dbInitialized) {
            // SQLite 模式
            const trace = getTraceByIdFromDb(req.params.traceId);
            if (!trace) {
                return res.status(404).json({ success: false, message: 'Trace not found' });
            }
            res.json({ success: true, data: trace });
        } else {
            // JSON 文件模式
            const traces = parseTraceFiles();
            const trace = traces.find(t => t.traceId === req.params.traceId);

            if (!trace) {
                return res.status(404).json({ success: false, message: 'Trace not found' });
            }

            // 重新从原文件读取完整的数据（包含所有 tags、logs 等）
            const filePath = path.join(TRACE_DIR, trace.fileName);
            const content = fs.readFileSync(filePath, 'utf8');
            const fullData = JSON.parse(content);

            res.json({ success: true, data: fullData });
        }
    } catch (e) {
        res.status(500).json({ success: false, message: (e as Error).message });
    }
});

// 3. 获取所有唯一的 URL 列表（用于下拉筛选）
app.get('/api/urls', (req, res) => {
    try {
        let urls: string[];

        if (DATA_SOURCE === 'sqlite' && dbInitialized) {
            // SQLite 模式
            urls = getUniqueUrlsFromDb();
        } else {
            // JSON 文件模式
            const traces = parseTraceFiles();
            const urlSet = new Set(traces.map(t => t.url));
            urls = Array.from(urlSet).sort();
        }

        res.json({ success: true, data: urls });
    } catch (e) {
        res.status(500).json({ success: false, message: (e as Error).message });
    }
});

// 4. 获取统计信息
app.get('/api/stats', (req, res) => {
    try {
        let stats: any;

        if (DATA_SOURCE === 'sqlite' && dbInitialized) {
            // SQLite 模式
            stats = getStatsFromDb();
        } else {
            // JSON 文件模式
            const traces = parseTraceFiles();
            stats = {
                totalTraces: traces.length,
                totalDuration: traces.reduce((sum, t) => sum + t.duration, 0),
                avgDuration: traces.length > 0
                    ? Math.round(traces.reduce((sum, t) => sum + t.duration, 0) / traces.length)
                    : 0,
                maxDuration: traces.length > 0
                    ? Math.max(...traces.map(t => t.duration))
                    : 0,
                uniqueUrls: new Set(traces.map(t => t.url)).size,
                services: new Set(traces.map(t => t.service)).size
            };
        }

        res.json({ success: true, data: stats });
    } catch (e) {
        res.status(500).json({ success: false, message: (e as Error).message });
    }
});

// 5. 强制刷新缓存
app.post('/api/refresh', (req, res) => {
    traceCache = null;
    parseTraceFiles();
    res.json({ success: true, message: 'Cache refreshed' });
});

// 6. 健康检查和数据源信息
app.get('/api/health', (req, res) => {
    try {
        const health: any = {
            status: 'healthy',
            dataSource: DATA_SOURCE,
            timestamp: Date.now()
        };

        if (DATA_SOURCE === 'sqlite' && dbInitialized) {
            const dbStats = getDbStats();
            health.dbSize = dbStats.size;
            health.tracesCount = dbStats.count;
        } else {
            // JSON 文件模式
            if (fs.existsSync(TRACE_DIR)) {
                const files = fs.readdirSync(TRACE_DIR).filter(f => f.endsWith('.json'));
                health.filesCount = files.length;
            }
        }

        res.json(health);
    } catch (e) {
        res.status(500).json({ status: 'error', message: (e as Error).message });
    }
});

// 静态文件服务（生产环境）
const distPath = path.resolve(__dirname, '../..', 'frontend', 'dist');
if (fs.existsSync(distPath)) {
    app.use(express.static(distPath));
    app.get('*', (req, res) => {
        res.sendFile(path.join(distPath, 'index.html'));
    });
}

// 启动服务器
app.listen(PORT, () => {
    console.log(`Server running on http://localhost:${PORT}`);
    console.log(`Data source: ${DATA_SOURCE}`);
    if (DATA_SOURCE === 'sqlite') {
        console.log(`SQLite database: ${path.resolve(__dirname, '../../..', 'skywalking_traces.db')}`);
    } else {
        console.log(`Trace directory: ${TRACE_DIR}`);
    }
});

// 优雅关闭
process.on('SIGINT', () => {
    console.log('Shutting down...');
    closeDatabase();
    process.exit(0);
});

process.on('SIGTERM', () => {
    console.log('Shutting down...');
    closeDatabase();
    process.exit(0);
});
