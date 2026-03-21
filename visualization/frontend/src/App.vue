<template>
  <div class="app">
    <el-container>
      <el-header>
        <div class="header-content">
          <h1 class="title">PHP调用链可视化</h1>
        </div>
      </el-header>
      <el-main>
        <router-view />
      </el-main>
    </el-container>
  </div>
</template>

<script setup lang="ts">
</script>

<style scoped>
.app {
  height: 100vh;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
}

.header-content {
  display: flex;
  align-items: center;
  height: 100%;
}

.title {
  margin: 0;
  font-size: 20px;
  color: #303133;
}
</style>
