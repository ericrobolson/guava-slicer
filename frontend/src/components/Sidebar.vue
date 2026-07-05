<template>
  <aside class="sidebar">
    <h2>Mesh Info</h2>

    <div v-if="stats" class="stats">
      <div class="stat-row">
        <span class="label">File</span>
        <span class="value">{{ fileName }}</span>
      </div>
      <div class="stat-row">
        <span class="label">Size</span>
        <span class="value">{{ formatSize(stats.file_size_bytes) }}</span>
      </div>
      <div class="stat-row">
        <span class="label">Vertices</span>
        <span class="value">{{ stats.vertex_count.toLocaleString() }}</span>
      </div>
      <div class="stat-row">
        <span class="label">Triangles</span>
        <span class="value">{{ stats.triangle_count.toLocaleString() }}</span>
      </div>
      <div v-for="({ label, i }) in axes" :key="label" class="stat-row">
        <span class="label">Bounds {{ label }}</span>
        <span class="value">{{ formatRange(stats.bounding_box.min[i], stats.bounding_box.max[i]) }}</span>
      </div>
      <div class="stat-row">
        <span class="label">Dimensions</span>
        <span class="value">{{ formatDimensions(stats.bounding_box) }}</span>
      </div>
      <div v-if="stats.skipped_triangles > 0" class="stat-row warning">
        <span class="label">Skipped</span>
        <span class="value">{{ stats.skipped_triangles }} triangles</span>
      </div>
    </div>

    <div v-else class="empty">
      No mesh loaded
    </div>
  </aside>
</template>

<script setup>
import { computed } from 'vue'
import { basename } from '../utils/path.js'

const props = defineProps({
  stats: { type: Object, default: null },
  filePath: { type: String, default: '' },
})

const axes = [{ label: 'X', i: 0 }, { label: 'Y', i: 1 }, { label: 'Z', i: 2 }]

const fileName = computed(() => basename(props.filePath))

function formatSize(bytes) {
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB'
}

function formatRange(min, max) {
  return `${min.toFixed(1)} .. ${max.toFixed(1)}`
}

function formatDimensions(bbox) {
  const dx = (bbox.max[0] - bbox.min[0]).toFixed(1)
  const dy = (bbox.max[1] - bbox.min[1]).toFixed(1)
  const dz = (bbox.max[2] - bbox.min[2]).toFixed(1)
  return `${dx} × ${dy} × ${dz}`
}
</script>

<style scoped>
.sidebar {
  width: 240px;
  background: #1e1e36;
  border-left: 1px solid #333;
  padding: 16px;
  overflow-y: auto;
  flex-shrink: 0;
}

h2 {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 12px;
  color: #aaa;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.stats {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.stat-row {
  display: flex;
  justify-content: space-between;
  font-size: 13px;
}

.label {
  color: #888;
}

.value {
  color: #ddd;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 12px;
}

.warning .value {
  color: #df7d7d;
}

.empty {
  color: #666;
  font-size: 13px;
  font-style: italic;
}
</style>
