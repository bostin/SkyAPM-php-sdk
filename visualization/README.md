# PHP外部调用链路可视化

本系统为PHP应用提供Web可视化界面，用于查看和分析分布式调用链路数据。

## 快速开始

### 开发

推荐使用 Node.js 20 或 22；当前依赖中的 `better-sqlite3` 不支持 Node.js 25。

```bash
cd visualization
pnpm install
pnpm dev
```

访问：http://localhost:3000

开发模式下，Express 会在同一个端口挂载 Vite 中间件，同时提供 `/api/*` 接口和前端页面。

### 生产部署
```bash
cd visualization
pnpm install
pnpm build
pnpm start
```

生产模式下，后端会直接托管 `frontend/dist`，部署时只需要启动一个 Node 服务。

如果部署在域名子目录下，例如 `/skywalking/`：

```bash
VITE_BASE_URL=/skywalking/ pnpm build
pnpm start
```

这样前端静态资源、路由和 API 请求都会使用 `/skywalking/` 前缀。

nginx 可以将 `/skywalking/` 前缀剥掉后转发给 Node 服务：

```nginx
location /skywalking/ {
    proxy_pass http://127.0.0.1:3000/;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
}
```

注意 `proxy_pass` 末尾的 `/`。上面的配置会把浏览器请求的 `/skywalking/api/traces` 转发成 Node 服务里的 `/api/traces`，也会把 `/skywalking/assets/...` 转发成 `/assets/...`。因此后端 Express 路由仍然保持 `/api/*`，不需要改成 `/skywalking/api/*`。

完整示例：

```bash
cd visualization
pnpm install
VITE_BASE_URL=/skywalking/ pnpm build
DATA_SOURCE=sqlite DB_PATH=/path/to/skywalking_traces.db pnpm start
```

浏览器访问：

```text
https://ft-nuxt-3.jjshouse.com/skywalking/
```

### 数据源

默认使用 SQLite：

```bash
DATA_SOURCE=sqlite DB_PATH=/path/to/skywalking_traces.db pnpm start
```

也可以使用 JSON 文件模式：

```bash
DATA_SOURCE=json pnpm start
```

## 功能特性

- 跟踪列表查看和筛选
- 调用链瀑布图可视化
- 时间范围和耗时过滤
- 响应式中文界面

## 技术架构

- **前端**: Vue 3 + TypeScript + Element Plus
- **后端**: Node.js + Express + TypeScript
- **数据源**: SQLite 或 JSON 文件（output目录）

更多详情请参阅：
- [前端代码说明](frontend/README.md)
- [后端 API 文档](server/README.md)
