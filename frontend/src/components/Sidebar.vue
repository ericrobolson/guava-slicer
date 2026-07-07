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
        <span class="value">{{ formatDimensions(stats) }}</span>
      </div>
      <div v-if="stats.skipped_triangles > 0" class="stat-row warning">
        <span class="label">Skipped</span>
        <span class="value">{{ stats.skipped_triangles }} triangles</span>
      </div>
    </div>

    <div v-else class="empty">
      No mesh loaded
    </div>

    <template v-if="hasMesh">
      <h2 class="section-header">Rotate</h2>
      <div class="transform-grid">
        <div v-for="axis in rotateAxes" :key="axis.label" class="axis-row">
          <span class="axis-label" :style="{ color: axis.color }">{{ axis.label }}</span>
          <button class="snap-btn" :disabled="transformLocked" @click="snapRotate(axis.axis, -90)" title="-90">-90</button>
          <button class="snap-btn" :disabled="transformLocked" @click="snapRotate(axis.axis, 90)" title="+90">+90</button>
        </div>
      </div>

      <h2 class="section-header">Scale</h2>
      <div class="scale-row">
        <input
          v-model.number="scaleInput"
          type="number"
          min="0.01"
          step="0.1"
          class="scale-input"
          @keydown.enter="applyScale"
        />
        <button class="action-btn" @click="applyScale" :disabled="!validScale || transformLocked">Apply</button>
      </div>

      <h2 class="section-header">Actions</h2>
      <div class="action-buttons">
        <button class="action-btn full-width" @click="placeOnPlate">Place on Build Plate</button>
        <button class="action-btn full-width" @click="centerOnPlate">Center on Build Plate</button>
        <button class="action-btn full-width" @click="resetOrientation">Reset Orientation</button>
        <button
          :class="['action-btn', 'full-width', { active: transformLocked }]"
          @click="$emit('toggle-lock')"
        >{{ transformLocked ? 'Unlock' : 'Lock' }}</button>
      </div>

      <h2 class="section-header">Transform</h2>
      <pre v-if="transformState.matrix" class="transform-matrix">{{ formatMatrix(transformState.matrix) }}</pre>
      <span v-else class="empty">Identity</span>
    </template>
  </aside>
</template>

<script setup>
import { ref, computed } from 'vue'
import { basename } from '../utils/path.js'

const props = defineProps({
  stats: { type: Object, default: null },
  filePath: { type: String, default: '' },
  transformState: { type: Object, default: () => ({}) },
  hasMesh: { type: Boolean, default: false },
  transformLocked: { type: Boolean, default: false },
})

const emit = defineEmits(['orient', 'toggle-lock'])

const axes = [{ label: 'X', i: 0 }, { label: 'Y', i: 1 }, { label: 'Z', i: 2 }]

const rotateAxes = [
  { label: 'X', axis: [1, 0, 0], color: '#ff4444' },
  { label: 'Y', axis: [0, 1, 0], color: '#44cc44' },
  { label: 'Z', axis: [0, 0, 1], color: '#4488ff' },
]

const scaleInput = ref(1.0)

const fileName = computed(() => basename(props.filePath))
const validScale = computed(() => scaleInput.value > 0)

function formatSize(bytes) {
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB'
}

function formatRange(min, max) {
  return `${min.toFixed(1)} .. ${max.toFixed(1)}`
}

function formatDimensions(stats) {
  if (stats.dimensions) {
    return `${stats.dimensions[0].toFixed(1)} x ${stats.dimensions[1].toFixed(1)} x ${stats.dimensions[2].toFixed(1)}`
  }
  const b = stats.bounding_box
  return `${(b.max[0] - b.min[0]).toFixed(1)} x ${(b.max[1] - b.min[1]).toFixed(1)} x ${(b.max[2] - b.min[2]).toFixed(1)}`
}

function snapRotate(axis, angle) {
  emit('orient', { type: 'rotate', axis, angle })
}

function applyScale() {
  if (!validScale.value) return
  const f = scaleInput.value
  emit('orient', { type: 'scale', factor: [f, f, f] })
  scaleInput.value = 1.0
}

function placeOnPlate() {
  emit('orient', { type: 'place_on_plate' })
}

function centerOnPlate() {
  emit('orient', { type: 'center_on_plate' })
}

function resetOrientation() {
  emit('orient', { type: 'reset' })
}

function formatMatrix(m) {
  if (!m || m.length < 16) return 'Identity'
  const rows = []
  for (let r = 0; r < 4; r++) {
    const cells = []
    for (let c = 0; c < 4; c++) {
      cells.push(m[c * 4 + r].toFixed(4).padStart(9))
    }
    rows.push(cells.join(' '))
  }
  return rows.join('\n')
}
</script>

<style scoped>
.sidebar {
  padding: 16px;
}

h2 {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 12px;
  color: #aaa;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.section-header {
  margin-top: 20px;
  padding-top: 16px;
  border-top: 1px solid #333;
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

.transform-grid {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.axis-row {
  display: flex;
  align-items: center;
  gap: 6px;
}

.axis-label {
  width: 16px;
  font-size: 13px;
  font-weight: 600;
  text-align: center;
}

.snap-btn {
  flex: 1;
  padding: 4px 8px;
  border: 1px solid #444;
  border-radius: 3px;
  background: #2a2a4a;
  color: #ccc;
  cursor: pointer;
  font-size: 12px;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.snap-btn:hover { background: #3a3a5a; }

.scale-row {
  display: flex;
  gap: 6px;
  align-items: center;
}

.scale-input {
  flex: 1;
  padding: 4px 8px;
  border: 1px solid #444;
  border-radius: 3px;
  background: #2a2a4a;
  color: #ddd;
  font-size: 12px;
  font-family: 'SF Mono', 'Fira Code', monospace;
  outline: none;
}

.scale-input:focus {
  border-color: #6699cc;
}

.action-btn {
  padding: 4px 10px;
  border: 1px solid #444;
  border-radius: 3px;
  background: #2a2a4a;
  color: #ccc;
  cursor: pointer;
  font-size: 12px;
}

.action-btn:hover:not(:disabled) { background: #3a3a5a; }
.action-btn:disabled { opacity: 0.4; cursor: not-allowed; }

.action-buttons {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.full-width {
  width: 100%;
  padding: 6px 10px;
}

.transform-matrix {
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 10px;
  color: #999;
  background: #1a1a2e;
  padding: 6px 8px;
  border-radius: 3px;
  border: 1px solid #333;
  overflow-x: auto;
  line-height: 1.4;
  white-space: pre;
  margin: 0;
}
</style>
