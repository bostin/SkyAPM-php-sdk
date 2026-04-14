<template>
  <div class="trace-detail-container">
    <el-card class="info-card">
      <template #header>
        <div class="card-header">
          <el-button link @click="goBack">
            <ArrowLeft />返回
          </el-button>
          <span class="card-title">调用详情</span>
        </div>
      </template>

      <div class="trace-info">
        <el-space wrap :fill="true">
          <el-statistic title="调用ID" :value="trace?.traceId" show-group-separator>
            <template #suffix>
              <el-button link type="primary" @click="copyToClipboard(trace?.traceId || '')">
                复制
              </el-button>
            </template>
          </el-statistic>

          <el-statistic
            title="总耗时"
            :value="duration"
            :precision="0"
            suffix="ms"
          >
            <template #title>
              <el-tooltip content="从第一个 Span 开始到最后一个 Span 结束" placement="top">
                <span>总耗时<el-icon><InfoFilled /></el-icon></span>
              </el-tooltip>
            </template>
          </el-statistic>

          <el-statistic
            title="Span数"
            :value="spans?.length"
          />
        </el-space>
      </div>
    </el-card>

    <el-card class="chart-card" v-loading="loading">
      <template #header>
        <div class="card-header">
          <span class="card-title">调用瀑布图</span>
          <div class="filter-controls">
            <el-input-number
              v-model="minDuration"
              :min="0"
              :step="10"
              placeholder="最小耗时(ms)"
              class="duration-input"
              @change="handleFilterChange"
            />
            <el-select
              v-model="selectedDbTypes"
              multiple
              collapse-tags
              collapse-tags-tooltip
              placeholder="DB 类型筛选"
              class="db-type-select"
              @change="handleFilterChange"
            >
              <el-option
                v-for="dbType in availableDbTypes"
                :key="dbType"
                :label="dbType"
                :value="dbType"
              />
            </el-select>
            <el-button @click="resetFilters">重置</el-button>
          </div>
        </div>
      </template>

      <div class="chart-container">
        <div class="chart-container-inner" ref="chartWrapper">
          <!-- Entry Span 置顶区域 -->
          <div v-if="entrySpans.length > 0" class="entry-span-header">
            <div
              v-for="span in entrySpans"
              :key="span.spanId"
              class="span-row entry-span-row"
              :style="{ paddingLeft: (span.depth * 20) + 'px' }"
            >
              <div class="span-info">
                <div class="span-name">
                  <el-tag
                    :size="span.spanType === 0 ? 'default' : 'small'"
                    :type="
                      span.spanLayer === 3 ? 'primary' :
                      span.spanLayer === 5 ? 'info' :
                      span.spanLayer === 2 ? 'success' : 'warning'
                    "
                    effect="light"
                  >
                    {{ getSpanTypeName(span.spanType) }}
                  </el-tag>
                  <span class="operation-name">{{ span.operationName }}</span>
                </div>
                <div class="span-meta">
                  <el-text size="small" type="info">
                    #{{ span.spanId }}
                  </el-text>
                  <el-text v-if="span.peer" size="small" type="info" style="margin-left: 8px">
                    {{ span.peer }}
                  </el-text>
                </div>
              </div>

              <div class="span-timeline">
                <el-tooltip placement="top" :disabled="span.spanType === 0">
                  <template #content>
                    <div class="span-tooltip">
                      <div><strong>{{ span.operationName }}</strong></div>
                      <div>耗时: {{ span.duration }} ms</div>
                      <div v-if="span.peer">对端: {{ span.peer }}</div>
                      <div v-if="span.tags && span.tags.length > 0">
                        <strong>Tags:</strong>
                        <div v-for="tag in span.tags" :key="tag.key" style="margin-left: 10px;">
                          {{ tag.key }}: {{ tag.value }}
                        </div>
                      </div>
                    </div>
                  </template>
                  <div
                    class="span-bar"
                    :style="{
                      left: spanStartTimePos(span) + 'px',
                      width: spanDurationWidth(span) + 'px',
                      backgroundColor: getSpanColor(span)
                    }"
                  >
                    <div class="span-bar-content">
                      <el-text type="white" size="small">
                        {{ span.duration }}
                      </el-text>
                    </div>
                  </div>
                </el-tooltip>
              </div>
            </div>
          </div>

          <!-- 虚拟列表容器（非 Entry Span） -->
          <div
            v-if="nonEntrySpans.length > 0"
            ref="virtualScrollContainer"
            class="virtual-scroll-container"
            @scroll="handleScroll"
          >
            <!-- 虚拟列表总高度占位 -->
            <div class="virtual-scroll-spacer" :style="{ height: totalHeight + 'px' }">
              <!-- 可见区域的内容 -->
              <div
                class="virtual-scroll-content"
                :style="{ transform: `translateY(${offsetY}px)` }"
              >
                <div
                  v-for="span in visibleNonEntrySpans"
                  :key="span.spanId"
                  class="span-row"
                  :style="{ paddingLeft: (span.depth * 20) + 'px' }"
                >
                  <div class="span-info">
                    <div class="span-name">
                      <el-tag
                        :size="span.spanType === 0 ? 'default' : 'small'"
                        :type="
                          span.spanLayer === 3 ? 'primary' :
                          span.spanLayer === 5 ? 'info' :
                          span.spanLayer === 2 ? 'success' : 'warning'
                        "
                        effect="light"
                      >
                        {{ getSpanTypeName(span.spanType) }}
                      </el-tag>
                      <span class="operation-name">{{ span.operationName }}</span>
                    </div>
                    <div class="span-meta">
                      <el-text size="small" type="info">
                        #{{ span.spanId }}
                      </el-text>
                      <el-text v-if="span.peer" size="small" type="info" style="margin-left: 8px">
                        {{ span.peer }}
                      </el-text>
                    </div>
                  </div>

                  <div class="span-timeline">
                    <el-tooltip placement="top" :disabled="span.spanType === 0">
                      <template #content>
                        <div class="span-tooltip">
                          <div><strong>{{ span.operationName }}</strong></div>
                          <div>耗时: {{ span.duration }} ms</div>
                          <div v-if="span.peer">对端: {{ span.peer }}</div>
                          <div v-if="span.tags && span.tags.length > 0">
                            <strong>Tags:</strong>
                            <div v-for="tag in span.tags" :key="tag.key" style="margin-left: 10px;">
                              {{ tag.key }}: {{ tag.value }}
                            </div>
                          </div>
                        </div>
                      </template>
                      <div
                        class="span-bar"
                        :style="{
                          left: spanStartTimePos(span) + 'px',
                          width: spanDurationWidth(span) + 'px',
                          backgroundColor: getSpanColor(span)
                        }"
                      >
                        <div class="span-bar-content">
                          <el-text type="white" size="small">
                            {{ span.duration }}
                          </el-text>
                        </div>
                      </div>
                    </el-tooltip>
                  </div>
                </div>
              </div>
            </div>
          </div>

          <div v-else-if="spans && spans.length > 0 && nonEntrySpans.length === 0" class="empty-chart">
            <el-empty description="暂无更多 Span 数据" />
          </div>

          <div v-else class="empty-chart">
            <el-empty description="暂无 Span 数据" />
          </div>
        </div>

        <!-- 时间轴 -->
        <div class="timeline">
          <div class="timeline-ruler">
            <div
              v-for="tick in timelineTicks"
              :key="tick.value"
              class="timeline-tick"
              :style="{ left: tick.position + 'px' }"
            >
              <el-text size="small" type="info">{{ tick.value }}ms</el-text>
            </div>
          </div>
        </div>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { api } from '@/api'
import {
  ArrowLeft,
  InfoFilled
} from '@element-plus/icons-vue'

const router = useRouter()
const route = useRoute()

const trace = ref<any>(null)
const spans = ref<any[]>([])
const chartWrapper = ref<HTMLElement | null>(null)
const minDuration = ref<number>(0)
const selectedDbTypes = ref<string[]>([])
const loading = ref(false)

// 虚拟列表相关
const virtualScrollContainer = ref<HTMLElement | null>(null)
const ROW_HEIGHT = 50 // 每行高度 (px)
const BUFFER_SIZE = 5 // 缓冲行数
const scrollTop = ref(0)
const containerHeight = ref(600)

// 收集所有 db.type
const availableDbTypes = computed(() => {
  const dbTypes = new Set<string>()
  spans.value.forEach((span: any) => {
    if (span.tags) {
      span.tags.forEach((tag: any) => {
        if (tag.key === 'db.type') {
          dbTypes.add(tag.value)
        }
      })
    }
  })
  return Array.from(dbTypes).sort()
})

// Entry Span（置顶显示）
const entrySpans = computed(() => {
  return flattenedSpans.value.filter(span => span.spanType === 0)
})

// 非 Entry Span（虚拟滚动）
const nonEntrySpans = computed(() => {
  return flattenedSpans.value.filter(span => span.spanType !== 0)
})

// 虚拟列表计算（基于非 Entry Span）
const totalHeight = computed(() => {
  return nonEntrySpans.value.length * ROW_HEIGHT
})

const offsetY = computed(() => {
  return Math.floor(scrollTop.value / ROW_HEIGHT) * ROW_HEIGHT
})

const visibleCount = computed(() => {
  return Math.ceil(containerHeight.value / ROW_HEIGHT) + BUFFER_SIZE * 2
})

const startIndex = computed(() => {
  return Math.max(0, Math.floor(scrollTop.value / ROW_HEIGHT) - BUFFER_SIZE)
})

const endIndex = computed(() => {
  return Math.min(nonEntrySpans.value.length, startIndex.value + visibleCount.value)
})

const visibleNonEntrySpans = computed(() => {
  return nonEntrySpans.value.slice(startIndex.value, endIndex.value)
})

const handleScroll = () => {
  if (virtualScrollContainer.value) {
    scrollTop.value = virtualScrollContainer.value.scrollTop
  }
}

// 检查 span 是否匹配过滤条件
const spanMatchesFilter = (span: any): boolean => {
  // 耗时过滤
  if (minDuration.value > 0 && span.duration < minDuration.value) {
    return false
  }

  // db.type 过滤
  if (selectedDbTypes.value.length > 0) {
    let spanDbType = ''
    if (span.tags) {
      span.tags.forEach((tag: any) => {
        if (tag.key === 'db.type') {
          spanDbType = tag.value
        }
      })
    }
    if (!selectedDbTypes.value.includes(spanDbType)) {
      return false
    }
  }

  return true
}

// 构建带有depth的扁平化span列表，用于瀑布图显示
const flattenedSpans = computed(() => {
  if (!spans.value || spans.value.length === 0) return []

  // 构建span映射
  const spanMap = new Map()
  spans.value.forEach((span: any) => {
    spanMap.set(span.spanId, { ...span, children: [], depth: 0 })
  })

  // 构建树结构
  const rootSpans: any[] = []
  spanMap.forEach((span, spanId) => {
    if (span.parentSpanId === -1 || span.parentSpanId === spanId || !spanMap.has(span.parentSpanId)) {
      rootSpans.push(span)
    } else {
      const parent = spanMap.get(span.parentSpanId)
      if (parent) {
        parent.children.push(span)
      }
    }
  })

  // 递归计算depth
  const calculateDepth = (span: any, depth: number) => {
    span.depth = depth
    span.children.forEach((child: any) => calculateDepth(child, depth + 1))
  }
  rootSpans.forEach((span: any) => calculateDepth(span, 0))

  // 扁平化为一维数组，按开始时间排序
  const flatten = (spans: any[]): any[] => {
    const result: any[] = []
    const sorted = [...spans].sort((a, b) => a.startTime - b.startTime)
    sorted.forEach((span: any) => {
      result.push(span)
      if (span.children && span.children.length > 0) {
        result.push(...flatten(span.children))
      }
    })
    return result
  }

  const allFlattened = flatten(rootSpans)

  // 应用过滤条件
  if (minDuration.value > 0 || selectedDbTypes.value.length > 0) {
    return allFlattened.filter(spanMatchesFilter)
  }

  return allFlattened
})

const duration = computed(() => {
  if (!spans.value || spans.value.length === 0) return 0
  const start = Math.min(...spans.value.map((s: any) => s.startTime))
  const end = Math.max(...spans.value.map((s: any) => s.endTime))
  return end - start
})

const minTime = computed(() => {
  if (!spans.value || spans.value.length === 0) return 0
  return Math.min(...spans.value.map((s: any) => s.startTime))
})

const maxTime = computed(() => {
  if (!spans.value || spans.value.length === 0) return 0
  return Math.max(...spans.value.map((s: any) => s.endTime))
})

const timeRange = computed(() => {
  return maxTime.value - minTime.value || 1000
})

const chartWidth = 800
const timePerPx = computed(() => {
  return timeRange.value / chartWidth
})

const getSpanTypeName = (type: number) => {
  const map: Record<number, string> = { 0: 'Entry', 1: 'Exit', 2: 'Local' }
  return map[type] || 'Unknown'
}

const getSpanColor = (span: any) => {
  // Entry类型的span始终显示蓝色
  if (span.spanType === 0) return '#409eff'

  // 错误的span显示红色
  if (span.isError) return '#f56c6c'

  // 根据耗时显示不同颜色
  const duration = span.duration || 0
  if (duration === 0) return 'rgba(144, 147, 153, 0.25)' // 半透明灰色
  if (duration <= 50) return '#67c23a' // 绿色
  if (duration <= 100) return '#e6a23c' // 黄色
  return '#f56c6c' // 红色
}

const spanStartTimePos = (span: any) => {
  return (span.startTime - minTime.value) / timePerPx.value
}

const spanDurationWidth = (span: any) => {
  const actualWidth = (span.endTime - span.startTime) / timePerPx.value
  // 计算数值的最小显示宽度：每个数字约7px，左右各留5px padding
  const duration = span.duration || 0
  const digitCount = duration > 0 ? Math.floor(Math.log10(duration)) + 1 : 1
  const minWidth = digitCount * 7 + 10 // 7px per digit + 10px padding (5px each side)
  return Math.max(actualWidth, minWidth)
}

const timelineTicks = computed(() => {
  const ticks: { value: number, position: number }[] = []
  // 根据时间范围动态决定刻度数量，避免太密集
  let tickCount = 5
  if (timeRange.value > 10000) {
    tickCount = 10
  } else if (timeRange.value > 5000) {
    tickCount = 8
  } else if (timeRange.value < 1000) {
    tickCount = 5
  }

  const step = timeRange.value / tickCount

  for (let i = 0; i <= tickCount; i++) {
    const offset = step * i
    const position = offset / timePerPx.value
    ticks.push({
      value: Math.round(offset),
      position
    })
  }
  return ticks
})

const goBack = () => {
  router.back()
}

const handleFilterChange = () => {
  // 过滤变化时自动应用，不需要额外操作
}

const resetFilters = () => {
  minDuration.value = 0
  selectedDbTypes.value = []
}

const copyToClipboard = async (text: string) => {
  try {
    await navigator.clipboard.writeText(text)
  } catch (err) {
    console.error('Failed to copy:', err)
  }
}

const fetchTrace = async () => {
  loading.value = true
  const traceId = route.query.traceId || route.params.traceId
  try {
    const response = await api.get(`traces/${traceId}`)
    if (response.data.success) {
      trace.value = response.data.data
      spans.value = response.data.data.spans || []
      // Calculate duration for each span
      spans.value = spans.value.map((span: any) => ({
        ...span,
        duration: span.endTime - span.startTime
      }))
    }
  } catch (error) {
    console.error('Error fetching trace:', error)
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  fetchTrace()
})

// 当过滤条件变化时重置滚动位置
watch([minDuration, selectedDbTypes], () => {
  scrollTop.value = 0
  if (virtualScrollContainer.value) {
    virtualScrollContainer.value.scrollTop = 0
  }
})
</script>

<style scoped>
.trace-detail-container {
  padding: 20px;
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

.trace-info {
  margin-top: 15px;
}

.chart-card {
  margin-top: 20px;
}

.filter-controls {
  display: flex;
  gap: 10px;
  align-items: center;
}

.duration-input {
  width: 140px;
}

.db-type-select {
  width: 180px;
}

.chart-container {
  position: relative;
  overflow-x: auto;
}

.chart-container-inner {
  position: relative;
  min-height: 200px;
  min-width: 100%;
}

.waterfall-chart {
  position: relative;
}

.span-row {
  display: flex;
  align-items: center;
  padding: 8px 0;
  border-bottom: 1px solid #f0f0f0;
  transition: background-color 0.2s;
}

.span-row:hover {
  background-color: #f8f9fa;
}

.span-info {
  width: 350px;
  flex-shrink: 0;
  padding-right: 15px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  padding-left: 20px;
}

.span-name {
  display: flex;
  align-items: center;
  margin-bottom: 4px;
  gap: 8px;
}

.operation-name {
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.span-meta {
  font-size: 12px;
  display: flex;
  align-items: center;
  gap: 8px;
}

.span-timeline {
  flex-grow: 1;
  position: relative;
  height: 32px;
  background: linear-gradient(90deg, #fafafa 0%, #f5f5f5 100%);
  border-radius: 4px;
  overflow: visible;
  min-width: 800px;
  flex-shrink: 0;
}

.span-bar {
  position: absolute;
  height: 22px;
  top: 5px;
  border-radius: 3px;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: visible;
  cursor: pointer;
  transition: all 0.2s;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.1);
}

.span-bar:hover {
  opacity: 0.85;
  transform: translateY(-1px);
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.15);
  z-index: 10;
}

.span-bar-content {
  white-space: nowrap;
  font-size: 11px;
  font-weight: 600;
  color: white;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.3);
  pointer-events: none;
  display: flex;
  align-items: center;
  justify-content: center;
}

.timeline {
  display: flex;
  margin-top: 10px;
  padding-left: 385px;
  height: 30px;
  border-top: 1px solid #e0e0e0;
  position: relative;
}

.timeline-ruler {
  position: relative;
  width: 100%;
  height: 100%;
  min-width: 800px;
}

.timeline-tick {
  position: absolute;
  height: 100%;
  border-left: 1px solid #e0e0e0;
  padding-left: 8px;
  display: flex;
  align-items: center;
  white-space: nowrap;
  font-size: 11px;
  color: #909399;
  font-weight: 500;
}

.timeline-tick:last-child {
  border-left: none;
}

.span-tooltip {
  font-size: 12px;
  line-height: 1.6;
}

.span-tooltip div {
  margin-bottom: 4px;
}

.span-tooltip strong {
  color: #409eff;
}

/* 虚拟列表样式 */
.virtual-scroll-container {
  height: 600px;
  overflow-y: auto;
  position: relative;
}

.virtual-scroll-spacer {
  position: relative;
}

.virtual-scroll-content {
  position: absolute;
  left: 0;
  right: 0;
  top: 0;
}

/* Entry Span 置顶区域样式 */
.entry-span-header {
  position: sticky;
  top: 0;
  z-index: 100;
  border-bottom: 2px solid #409eff;
  background-color: #ecf5ff;
}

.entry-span-row {
  background-color: #ecf5ff;
}
</style>
