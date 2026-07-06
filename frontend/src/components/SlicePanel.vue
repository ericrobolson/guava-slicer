<template>
  <div class="slice-panel">
    <h2 class="section-header">Slice</h2>

    <div class="param-row">
      <span class="label">Layer Height</span>
      <div class="input-group">
        <input
          v-model.number="localHeight"
          type="number"
          :min="MIN_LAYER_HEIGHT"
          :max="MAX_LAYER_HEIGHT"
          step="0.01"
          class="param-input"
          @keydown.enter="onHeightChange"
          @blur="onHeightChange"
        />
        <span class="unit">mm</span>
      </div>
    </div>

    <button
      class="action-btn full-width"
      :class="{ active: inspecting }"
      :disabled="!hasMesh"
      @click="$emit('toggle-inspect')"
    >
      {{ inspecting ? 'Exit Layer View' : 'Slice' }}
    </button>

    <div v-if="slicing" class="slice-progress">
      <div class="progress-track">
        <div class="progress-fill" :style="{ width: progressPercent + '%' }"></div>
      </div>
      <span class="progress-label">Slicing... {{ progressPercent }}%</span>
    </div>

    <div v-if="inspecting && layerCount > 0" class="layer-info">
      <div class="stat-row">
        <span class="label">Layers</span>
        <span class="value">{{ layerCount }}</span>
      </div>
      <div class="stat-row">
        <span class="label">Current</span>
        <span class="value">{{ currentLayer + 1 }} / {{ layerCount }}</span>
      </div>
      <div class="stat-row">
        <span class="label">Z Height</span>
        <span class="value">{{ zHeight.toFixed(3) }} mm</span>
      </div>
      <div v-if="warningCount > 0" class="stat-row warning">
        <span class="label">Warnings</span>
        <span class="value">{{ warningCount }} degenerate</span>
      </div>
    </div>

    <div v-if="islandDetecting" class="island-detecting">
      <span class="progress-label">Detecting islands...</span>
    </div>

    <div v-if="inspecting && islandResult" class="island-info">
      <div class="stat-row" :class="{ 'island-alert': islandResult.totalCount > 0 }">
        <span class="label">Islands</span>
        <span class="value">{{ islandResult.totalCount }}</span>
      </div>
      <div v-if="currentLayerIslands && currentLayerIslands.count > 0" class="stat-row island-alert">
        <span class="label">This layer</span>
        <span class="value">{{ currentLayerIslands.count }} ({{ currentLayerIslands.area.toFixed(2) }} mm²)</span>
      </div>
      <div v-if="islandResult.totalCount > 0" class="stat-row clickable" @click="$emit('jump-to-layer', islandResult.worstLayer)">
        <span class="label">Worst layer</span>
        <span class="value link">{{ islandResult.worstLayer + 1 }} →</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'

const MIN_LAYER_HEIGHT = 0.01
const MAX_LAYER_HEIGHT = 1.0

const props = defineProps({
  hasMesh: { type: Boolean, default: false },
  inspecting: { type: Boolean, default: false },
  slicing: { type: Boolean, default: false },
  layerCount: { type: Number, default: 0 },
  currentLayer: { type: Number, default: 0 },
  zHeight: { type: Number, default: 0 },
  layerHeight: { type: Number, default: 0.06 },
  warningCount: { type: Number, default: 0 },
  sliceProgress: { type: Object, default: () => ({ current: 0, total: 0 }) },
  islandDetecting: { type: Boolean, default: false },
  islandResult: { type: Object, default: null },
  currentLayerIslands: { type: Object, default: null },
})

const emit = defineEmits(['toggle-inspect', 'update:layerHeight', 'jump-to-layer'])

const localHeight = ref(props.layerHeight)

const progressPercent = ref(0)

watch(() => props.sliceProgress, (p) => {
  progressPercent.value = p.total > 0 ? Math.round((p.current / p.total) * 100) : 0
})

watch(() => props.layerHeight, (h) => { localHeight.value = h })

function onHeightChange() {
  const clamped = Math.max(MIN_LAYER_HEIGHT, Math.min(MAX_LAYER_HEIGHT, localHeight.value))
  localHeight.value = clamped
  if (clamped !== props.layerHeight) {
    emit('update:layerHeight', clamped)
  }
}
</script>

<style scoped>
.slice-panel {
  padding: 0 16px 16px;
}

.section-header {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 12px;
  margin-top: 20px;
  padding-top: 16px;
  border-top: 1px solid #333;
  color: #aaa;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.param-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 10px;
  font-size: 13px;
}

.label { color: #888; }
.value {
  color: #ddd;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 12px;
}

.input-group {
  display: flex;
  align-items: center;
  gap: 4px;
}

.param-input {
  width: 70px;
  padding: 3px 6px;
  border: 1px solid #444;
  border-radius: 3px;
  background: #2a2a4a;
  color: #ddd;
  font-size: 12px;
  font-family: 'SF Mono', 'Fira Code', monospace;
  text-align: right;
  outline: none;
}

.param-input:focus { border-color: #6699cc; }

.unit {
  color: #666;
  font-size: 11px;
}

.action-btn {
  padding: 6px 10px;
  border: 1px solid #444;
  border-radius: 3px;
  background: #2a2a4a;
  color: #ccc;
  cursor: pointer;
  font-size: 12px;
  width: 100%;
}

.action-btn:hover:not(:disabled) { background: #3a3a5a; }
.action-btn:disabled { opacity: 0.4; cursor: not-allowed; }
.action-btn.active {
  background: #3a5a3a;
  border-color: #5a8a5a;
  color: #aaddaa;
}

.slice-progress {
  margin-top: 8px;
}

.progress-track {
  height: 4px;
  background: #2a2a4a;
  border-radius: 2px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: #4a7a4a;
  transition: width 0.1s ease;
}

.progress-label {
  display: block;
  margin-top: 4px;
  font-size: 11px;
  color: #888;
}

.layer-info {
  margin-top: 12px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.stat-row {
  display: flex;
  justify-content: space-between;
  font-size: 13px;
}

.warning .value { color: #df7d7d; }

.island-detecting {
  margin-top: 8px;
}

.island-info {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px solid #333;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.island-alert .value { color: #ff6666; }
.island-alert .label { color: #cc8888; }

.clickable { cursor: pointer; }
.clickable:hover .label { color: #aaa; }
.clickable:hover .link { color: #88bbff; }
.link { text-decoration: underline; text-underline-offset: 2px; }
</style>
