<template>
  <div class="about-container">
    <el-card>
      <h2>PHP调用栈可视化</h2>
      <p>版本: 1.0.0</p>
      <p>用于可视化 PHP 应用生成的分布式调用链数据。</p>

      <h3>功能特性</h3>
      <el-list>
        <el-list-item>
          <el-icon :size="20" color="#409eff">
            <Search />
          </el-icon>
          <span>URL 查询 - 支持模糊匹配</span>
        </el-list-item>
        <el-list-item>
          <el-icon :size="20" color="#67c23a">
            <Sort />
          </el-icon>
          <span>按耗时排序 - 升序/降序</span>
        </el-list-item>
        <el-list-item>
          <el-icon :size="20" color="#e6a23c">
            <DataAnalysis />
          </el-icon>
          <span>调用层次可视化 - 瀑布图展示</span>
        </el-list-item>
        <el-list-item>
          <el-icon :size="20" color="#f56c6c">
            <Time />
          </el-icon>
          <span>时间轴分析 - 精确到毫秒</span>
        </el-list-item>
      </el-list>

      <h3>数据来源</h3>
      <el-text>
        系统从 <code>{{ traceDir }}</code> 目录读取 JSON trace 文件。
      </el-text>

      <h3>技术栈</h3>
      <el-space wrap>
        <el-tag>Node.js</el-tag>
        <el-tag>Express</el-tag>
        <el-tag type="success">Vue 3</el-tag>
        <el-tag type="success">TypeScript</el-tag>
        <el-tag type="info">Element Plus</el-tag>
      </el-space>

      <h3>使用说明</h3>
      <ol>
        <li>启动后端服务: <code>cd server && npm install && npm run dev</code></li>
        <li>启动前端开发服务器: <code>cd frontend && npm install && npm run dev</code></li>
        <li>访问 <a href="http://localhost:5173" target="_blank">http://localhost:5173</a></li>
      </ol>

      <el-button type="primary" @click="router.push('/')">
        返回首页
      </el-button>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import {
  Search,
  Sort,
  DataAnalysis,
  Time
} from '@element-plus/icons-vue'

const router = useRouter()
const traceDir = ref('/path/to/output')
</script>

<style scoped>
.about-container {
  padding: 20px;
  max-width: 800px;
  margin: 0 auto;
}

h2 {
  color: #303133;
  border-bottom: 1px solid #e0e0e0;
  padding-bottom: 10px;
}

h3 {
  color: #606266;
  margin-top: 20px;
}

code {
  background: #f5f5f5;
  padding: 2px 6px;
  border-radius: 4px;
  font-family: monospace;
}

el-list-item {
  margin-bottom: 10px;
}
</style>
