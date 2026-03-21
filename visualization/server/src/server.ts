#!/usr/bin/env node

/**
 * SkyWalking PHP Visualizer Server
 * Node.js Express Server for tracing data visualization
 */

import express from 'express';
import cors from 'cors';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

// TaskCreate is not available in this context, skipping...

// Get current directory for ES modules
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;

// 中间件
app.use(cors());
app.use(express.json());

// 配置 - 数据目录在项目根目录的 output 文件夹
const TRACE_DIR = path.resolve(__dirname, '../../..', 'output');
const CACHE_TTL = 5000; // 5秒缓存
let traceCache: any[] | null = null;
let cacheTime = 0;

// 工具函数：扫描并解析 trace 文件
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

// API 路由

// 1. 获取 trace 列表（支持过滤/排序/分页）
app.get('/api/traces', (req, res) => {
  try {
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
    if (req.query.sortBy === 'duration') {
      const order = req.query.order === 'asc' ? 1 : -1;
      traces.sort((a, b) => order * (a.duration - b.duration));
    } else if (req.query.sortBy === 'timestamp') {
      const order = req.query.order === 'asc' ? 1 : -1;
      traces.sort((a, b) => order * (a.timestamp - b.timestamp));
    }

    // 分页
    const page = parseInt(req.query.page?.toString() || '1');
    const pageSize = parseInt(req.query.pageSize?.toString() || '20');
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
  } catch (e) {
    res.status(500).json({ success: false, message: (e as Error).message });
  }
});

// 2. 获取单个 trace 详情
app.get('/api/traces/:traceId', (req, res) => {
  try {
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
  } catch (e) {
    res.status(500).json({ success: false, message: (e as Error).message });
  }
});

// 3. 获取所有唯一的 URL 列表（用于下拉筛选）
app.get('/api/urls', (req, res) => {
  try {
    const traces = parseTraceFiles();
    const urlSet = new Set(traces.map(t => t.url));
    const urls = Array.from(urlSet).sort();
    res.json({ success: true, data: urls });
  } catch (e) {
    res.status(500).json({ success: false, message: (e as Error).message });
  }
});

// 4. 获取统计信息
app.get('/api/stats', (req, res) => {
  try {
    const traces = parseTraceFiles();

    const stats = {
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
  console.log(`Trace directory: ${TRACE_DIR}`);
});
