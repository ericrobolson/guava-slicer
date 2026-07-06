<template>
  <div class="overhang-panel" v-if="hasMesh">
    <h2 class="section-header">Overhangs</h2>

    <div class="toggle-row">
      <label class="toggle-label">
        <input type="checkbox" :checked="enabled" @change="$emit('toggle-overlay')" />
        Show overlay
      </label>
    </div>

    <div class="slider-row">
      <span class="label">Threshold</span>
      <input
        type="range"
        :min="0" :max="90" :step="1"
        :value="threshold"
        @input="$emit('update:threshold', Number($event.target.value))"
        class="threshold-slider"
      />
      <span class="value">{{ threshold }}°</span>
    </div>

    <div v-if="overhangData" class="metrics">
      <div class="stat-row">
        <span class="label">Overhang faces</span>
        <span class="value">{{ overhangData.count.toLocaleString() }}</span>
      </div>
      <div class="stat-row">
        <span class="label">Overhang area</span>
        <span class="value">{{ formatArea(overhangData.area) }}</span>
      </div>
      <div class="stat-row">
        <span class="label">% of surface</span>
        <span class="value">{{ formatPercent(overhangData.area, overhangData.totalArea) }}</span>
      </div>
    </div>

    <h2 class="section-header">Auto Orient</h2>

    <div class="orient-list">
      <button
        v-for="axis in axisOptions"
        :key="axis.key"
        class="orient-row"
        :class="{ ready: axis.ready, computing: axis.computing }"
        :disabled="!axis.ready"
        @click="$emit('apply-orientation', axis.key)"
      >
        <span class="axis-name">{{ axis.label }}</span>
        <span v-if="axis.computing" class="axis-status computing-text">...</span>
        <span v-else-if="axis.ready" class="axis-status area-text">{{ formatArea(axis.overhangArea) }}</span>
        <span v-else class="axis-status pending-text">—</span>
      </button>
    </div>

    <div v-if="precomputeStatus" class="precompute-bar">
      <div class="mini-progress">
        <div class="mini-progress-fill" :style="{ width: precomputePercent + '%' }"></div>
      </div>
      <span class="precompute-label">{{ precomputeStatus }}</span>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const AXIS_LABELS = {
  tilt_back: 'Tilt back',
  tilt_forward: 'Tilt forward',
  tilt_left: 'Tilt left',
  tilt_right: 'Tilt right',
  any: 'Any',
}

const props = defineProps({
  hasMesh: { type: Boolean, default: false },
  enabled: { type: Boolean, default: true },
  threshold: { type: Number, default: 45 },
  overhangData: { type: Object, default: null },
  orientationCache: { type: Object, default: () => ({}) },
  computingAxis: { type: String, default: null },
  precomputeDone: { type: Number, default: 0 },
  precomputeTotal: { type: Number, default: 5 },
})

defineEmits(['update:threshold', 'toggle-overlay', 'apply-orientation'])

const axisOptions = computed(() => {
  const keys = ['tilt_back', 'tilt_forward', 'tilt_left', 'tilt_right', 'any']
  return keys.map(key => {
    const cached = props.orientationCache[key]
    return {
      key,
      label: AXIS_LABELS[key],
      ready: !!cached,
      computing: props.computingAxis === key,
      overhangArea: cached ? cached.overhang_area : 0,
    }
  })
})

const precomputeStatus = computed(() => {
  if (props.precomputeDone >= props.precomputeTotal) return null
  return `Computing ${props.precomputeDone}/${props.precomputeTotal}`
})

const precomputePercent = computed(() => {
  if (!props.precomputeTotal) return 0
  return Math.round((props.precomputeDone / props.precomputeTotal) * 100)
})

function formatArea(area) {
  if (area < 1) return area.toFixed(2) + ' mm²'
  if (area < 1000) return area.toFixed(1) + ' mm²'
  return (area / 100).toFixed(1) + ' cm²'
}

function formatPercent(part, total) {
  if (!total) return '0%'
  return ((part / total) * 100).toFixed(1) + '%'
}
</script>

<style scoped>
.overhang-panel {
  padding: 0 16px 16px;
}

.overhang-panel:first-child .section-header:first-child {
  margin-top: 0;
  padding-top: 0;
  border-top: none;
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

.toggle-row {
  margin-bottom: 10px;
}

.toggle-label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: #ccc;
  cursor: pointer;
}

.toggle-label input {
  accent-color: #6699cc;
}

.slider-row {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 10px;
}

.threshold-slider {
  flex: 1;
  accent-color: #6699cc;
  height: 4px;
}

.label {
  color: #888;
  font-size: 13px;
  white-space: nowrap;
}

.value {
  color: #ddd;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 12px;
  min-width: 32px;
  text-align: right;
}

.metrics {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-bottom: 4px;
}

.stat-row {
  display: flex;
  justify-content: space-between;
  font-size: 13px;
}

.orient-list {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.orient-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 5px 8px;
  border: 1px solid #333;
  border-radius: 3px;
  background: #2a2a4a;
  color: #ccc;
  cursor: pointer;
  font-size: 12px;
  text-align: left;
}

.orient-row:hover:not(:disabled) { background: #3a3a5a; }
.orient-row:disabled { opacity: 0.5; cursor: default; }
.orient-row.ready { border-color: #446644; }

.axis-name {
  font-weight: 500;
}

.axis-status {
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 11px;
}

.area-text { color: #7ddf7d; }
.computing-text { color: #6699cc; }
.pending-text { color: #555; }

.precompute-bar {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 6px;
}

.mini-progress {
  flex: 1;
  height: 4px;
  background: #2a2a4a;
  border-radius: 2px;
  overflow: hidden;
}

.mini-progress-fill {
  height: 100%;
  background: #6699cc;
  transition: width 0.15s ease;
}

.precompute-label {
  font-size: 11px;
  color: #888;
  white-space: nowrap;
}
</style>
