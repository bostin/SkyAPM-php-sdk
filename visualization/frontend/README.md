# SkyWalking PHP Visualizer Frontend

Vue 3前端应用，提供跟踪数据的可视化展示。

## 开发

```bash
# 安装依赖
pnpm install

# 启动开发服务器
pnpm dev

# 构建生产版本
pnpm build

# 预览生产构建
pnpm preview
```

## 项目结构

- `src/views/` - 页面组件
- `src/router/` - 路由配置
- `src/App.vue` - 根组件
- `main.ts` - 应用入口

## 主要功能

- **TraceList.vue** - 跟踪列表页面
  - 分页显示跟踪数据
  - URL模糊搜索
  - 时间范围筛选（精确到秒）
  - 时间和耗时列排序

- **TraceDetail.vue** - 跟踪详情页面
  - 调用链瀑布图
  - Span颜色编码（按耗时）
  - 详细tooltip信息

- **About.vue** - 关于页面
