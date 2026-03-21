# SkyWalking PHP Visualizer Server

Node.js Express API服务器，提供跟踪数据的RESTful接口。

## 开发

```bash
# 安装依赖
pnpm install

# 启动开发服务器（热重载）
pnpm dev

# 构建并启动生产服务器
pnpm build && pnpm start
```

## API接口

### 获取跟踪列表
```
GET /api/traces?page=1&pageSize=20&url=xxx&startTime=xxx&endTime=xxx&sortBy=timestamp&order=desc
```

**查询参数：**
- `page` - 页码（默认：1）
- `pageSize` - 每页数量（默认：20）
- `url` - URL模糊匹配
- `startTime` - 开始时间戳（毫秒）
- `endTime` - 结束时间戳（毫秒）
- `sortBy` - 排序字段（timestamp/duration）
- `order` - 排序方向（asc/desc）

### 获取跟踪详情
```
GET /api/traces/:traceId
```

返回完整的跟踪数据，包含所有spans、tags和logs。

### 获取统计信息
```
GET /api/stats
```

返回总跟踪数、平均耗时、最大耗时等统计信息。

### 刷新缓存
```
POST /api/refresh
```

清除5秒缓存，强制重新读取文件。

## 数据源

从项目根目录的 `output/` 目录读取JSON格式的跟踪文件。

## 缓存机制

- 默认缓存时间：5秒
- 自动刷新机制
- 支持手动刷新
