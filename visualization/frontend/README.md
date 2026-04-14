# SkyWalking PHP Visualizer Frontend

Vue 3前端应用，提供跟踪数据的可视化展示。

## 开发

依赖和脚本已合并到 `visualization` 根目录，请从根目录运行：

```bash
pnpm install
pnpm dev
pnpm build
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
