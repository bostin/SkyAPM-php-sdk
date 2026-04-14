<template>
  <div class="trace-list-container">
    <el-card class="filter-card">
      <template #header>
        <div class="card-header">
          <span class="card-title">筛选条件</span>
          <el-button link type="primary" @click="refreshData" :loading="loading">
            <Refresh />刷新
          </el-button>
        </div>
      </template>

      <div class="filter-content">
        <div class="filter-row">
          <el-input
            v-model="filterParams.url"
            placeholder="URL 查询（支持模糊匹配）"
            clearable
            @change="handleFilterChange"
            class="url-input"
          >
            <template #prefix>
              <SvgIcon name="Search" />
            </template>
          </el-input>

          <el-date-picker
            v-model="filterParams.startTime"
            type="datetime"
            placeholder="开始时间"
            format="YYYY-MM-DD HH:mm:ss"
            value-format="YYYY-MM-DD HH:mm:ss"
            clearable
            @change="handleFilterChange"
            class="datetime-picker"
          />

          <el-date-picker
            v-model="filterParams.endTime"
            type="datetime"
            placeholder="结束时间"
            format="YYYY-MM-DD HH:mm:ss"
            value-format="YYYY-MM-DD HH:mm:ss"
            clearable
            @change="handleFilterChange"
            class="datetime-picker"
          />

          <el-input-number
            v-model="filterParams.minDuration"
            :min="0"
            :step="100"
            placeholder="最小耗时"
            controls-position="right"
            @change="handleFilterChange"
            class="duration-input"
          />

          <el-input-number
            v-model="filterParams.maxDuration"
            :min="0"
            :step="100"
            placeholder="最大耗时"
            controls-position="right"
            @change="handleFilterChange"
            class="duration-input"
          />
        </div>
      </div>
    </el-card>

    <el-card class="table-card">
      <template #header>
        <div class="card-header">
          <span class="card-title">调用列表</span>
          <el-text class="total-text" size="small">
            共 {{ pagination.total }} 条
          </el-text>
        </div>
      </template>

      <el-table
        v-loading="loading"
        :data="traces"
        stripe
        style="width: 100%"
        @row-click="handleRowClick"
        @sort-change="handleSortChange"
      >
        <el-table-column prop="timestamp" label="时间" width="180" sortable="custom">
          <template #default="{ row }">
            <el-text size="small">{{ formatTime(row.timestamp) }}</el-text>
          </template>
        </el-table-column>

        <el-table-column prop="duration" label="耗时(ms)" width="100" sortable="custom">
          <template #default="{ row }">
            <el-text :type="getDurationType(row.duration)">
              {{ row.duration }}
            </el-text>
          </template>
        </el-table-column>

        <el-table-column prop="url" label="URL" min-width="200">
          <template #default="{ row }">
            <el-text>{{ row.url }}</el-text>
          </template>
        </el-table-column>

        <el-table-column prop="spanCount" label="Span 数" width="80">
          <template #default="{ row }">
            <el-text size="small">{{ row.spans?.length || 0 }}</el-text>
          </template>
        </el-table-column>

        <el-table-column label="操作" width="80" fixed="right" align="center">
          <template #default="{ row }">
            <el-button link type="primary" @click.stop="viewDetail(row)">
              详情
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <el-pagination
        v-model:current-page="pagination.page"
        v-model:page-size="pagination.pageSize"
        :total="pagination.total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        class="pagination"
        @size-change="handlePaginationChange"
        @current-change="handlePaginationChange"
      />
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { api } from '@/api'

interface Trace {
  traceId: string
  traceSegmentId: string
  service: string
  serviceInstance: string
  spans: any[]
  duration: number
  url: string
  timestamp: number
  fileName: string
  fileSize: number
}

interface Pagination {
  total: number
  page: number
  pageSize: number
  totalPages: number
}

interface FilterParams {
  url: string
  startTime: string | null
  endTime: string | null
  minDuration: number | null
  maxDuration: number | null
}

const router = useRouter()
const traces = ref<Trace[]>([])
const loading = ref(false)
const filterParams = reactive<FilterParams>({
  url: '',
  startTime: null,
  endTime: null,
  minDuration: null,
  maxDuration: null
})
const pagination = reactive<Pagination>({
  total: 0,
  page: 1,
  pageSize: 20,
  totalPages: 1
})
const currentSort = reactive({
  prop: 'timestamp',
  order: 'descending'
})

const getDurationType = (duration: number) => {
  if (duration > 1000) return 'danger'
  if (duration > 500) return 'warning'
  return ''
}

const formatTime = (timestamp: number) => {
  const date = new Date(timestamp)
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hour = String(date.getHours()).padStart(2, '0')
  const minute = String(date.getMinutes()).padStart(2, '0')
  const second = String(date.getSeconds()).padStart(2, '0')
  return `${year}-${month}-${day} ${hour}:${minute}:${second}`
}

const fetchData = async () => {
  loading.value = true
  try {
    const params: Record<string, any> = {
      page: pagination.page,
      pageSize: pagination.pageSize
    }

    if (filterParams.url) params.url = filterParams.url

    // 处理时间区间筛选
    if (filterParams.startTime) {
      params.startTime = new Date(filterParams.startTime).getTime()
    }
    if (filterParams.endTime) {
      params.endTime = new Date(filterParams.endTime).getTime()
    }

    // 处理耗时区间筛选
    if (filterParams.minDuration != null && filterParams.minDuration > 0) {
      params.minDuration = filterParams.minDuration
    }
    if (filterParams.maxDuration != null && filterParams.maxDuration > 0) {
      params.maxDuration = filterParams.maxDuration
    }

    // 处理排序
    if (currentSort.prop) {
      params.sortBy = currentSort.prop
      if (currentSort.order === 'descending') {
        params.order = 'desc'
      } else if (currentSort.order === 'ascending') {
        params.order = 'asc'
      }
    }

    const response = await api.get('traces', { params })
    if (response.data.success) {
      traces.value = response.data.data
      pagination.total = response.data.pagination?.total || 0
      pagination.pageSize = response.data.pagination?.pageSize || 20
    }
  } catch (error) {
    console.error('Error fetching traces:', error)
  } finally {
    loading.value = false
  }
}

const handleSortChange = ({ prop, order }: { prop: string; order: string }) => {
  currentSort.prop = prop
  currentSort.order = order
  fetchData()
}

const refreshData = () => {
  fetchData()
}

const handleFilterChange = () => {
  pagination.page = 1
  fetchData()
}

const handlePaginationChange = () => {
  fetchData()
}

const handleRowClick = (row: Trace) => {
  viewDetail(row)
}

const viewDetail = (row: Trace) => {
  router.push(`/trace/${row.traceId}`)
}

onMounted(() => {
  fetchData()
})
</script>

<style scoped>
.trace-list-container {
  padding: 20px;
}

.filter-card {
  margin-bottom: 20px;
}

.filter-content {
  width: 100%;
}

.filter-row {
  display: flex;
  gap: 10px;
  align-items: center;
}

.url-input {
  flex: 1;
  min-width: 0;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.card-title {
  font-weight: bold;
  font-size: 16px;
}

.total-text {
  color: #909399;
}

.table-card {
  margin-top: 20px;
}

.pagination {
  margin-top: 20px;
  text-align: right;
}

.duration-input {
  width: 130px;
  flex-shrink: 0;
}

/* 确保表头排序按钮和文本在一行显示 */
:deep(.el-table .cell) {
  white-space: nowrap;
  overflow: visible;
}

:deep(.el-table th .cell) {
  display: flex;
  align-items: center;
}

:deep(.el-table .caret-wrapper) {
  margin-left: 4px;
}

/* 时间选择器紧凑模式 */
:deep(.datetime-picker) {
  width: auto;
  flex-shrink: 0;
}

:deep(.datetime-picker .el-input__wrapper) {
  width: 195px;
}
</style>
