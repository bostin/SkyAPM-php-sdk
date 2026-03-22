# PHP外部调用链路可视化

本系统为PHP应用提供Web可视化界面，用于查看和分析分布式调用链路数据。

## 快速开始

### 前端开发
```bash
cd visualization/frontend
pnpm install
pnpm dev
```

### 后端开发
```bash
cd visualization/server
pnpm install
pnpm dev
```

### 生产部署
1. 构建前端：`cd visualization/frontend && pnpm build`
2. 启动后端：`cd visualization/server && pnpm start`
3. 访问：http://localhost:3000

## 功能特性

- 跟踪列表查看和筛选
- 调用链瀑布图可视化
- 时间范围和耗时过滤
- 响应式中文界面

## 技术架构

- **前端**: Vue 3 + TypeScript + Element Plus
- **后端**: Node.js + Express + TypeScript
- **数据源**: JSON文件（output目录）

更多详情请参阅：
- [前端开发文档](frontend/README.md)
- [后端API文档](server/README.md)
