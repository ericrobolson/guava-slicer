<template>
  <div class="support-panel" v-if="hasMesh">
    <h2 class="section-header">Supports</h2>

    <div class="category-toggles">
      <label v-for="cat in categories" :key="cat.key" class="toggle-label">
        <input
          type="checkbox"
          :checked="isCategoryEnabled(cat.bit)"
          @change="toggleCategory(cat.bit)"
        />
        {{ cat.label }}
        <span v-if="supportResult" class="cat-count">({{ getCategoryCount(cat.key) }})</span>
      </label>
    </div>

    <div class="param-section">
      <div class="param-row" v-for="param in paramDefs" :key="param.key">
        <span class="label">{{ param.label }}</span>
        <div class="input-group">
          <input
            v-model.number="localParams[param.key]"
            type="number"
            :min="param.min"
            :max="param.max"
            :step="param.step"
            class="param-input"
          />
          <span class="unit">mm</span>
        </div>
      </div>
    </div>

    <div class="action-buttons">
      <button
        class="action-btn full-width primary"
        :disabled="generating"
        @click="$emit('auto-generate')"
      >
        {{ generating ? 'Generating...' : 'Auto Support' }}
      </button>

      <div class="mode-buttons">
        <button
          class="action-btn mode-btn"
          :class="{ active: interactionMode === 'place' }"
          @click="$emit('set-mode', interactionMode === 'place' ? null : 'place')"
          title="Place support (P)"
        >
          + Place
        </button>
        <button
          class="action-btn mode-btn"
          :class="{ active: interactionMode === 'remove' }"
          @click="$emit('set-mode', interactionMode === 'remove' ? null : 'remove')"
          title="Remove support (X)"
        >
          - Remove
        </button>
      </div>

      <button
        class="action-btn full-width"
        :disabled="!supportResult || supportResult.totalCount === 0"
        @click="$emit('clear-supports')"
      >
        Clear All
      </button>
    </div>

    <div v-if="generating" class="gen-progress">
      <div class="progress-track">
        <div class="progress-fill" :style="{ width: progressPercent + '%' }"></div>
      </div>
      <span class="progress-label">{{ progressLabel }}</span>
    </div>

    <div v-if="supportResult" class="support-info">
      <div class="stat-row">
        <span class="label">Total</span>
        <span class="value">{{ supportResult.totalCount }}</span>
      </div>
      <div class="stat-row" v-if="supportResult.vertexCount > 0">
        <span class="label">Mesh</span>
        <span class="value">{{ supportResult.triangleCount.toLocaleString() }} tris</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, watch, computed } from 'vue'

const CATEGORY_BIT_ISLAND = 1
const CATEGORY_BIT_REINFORCEMENT = 2
const CATEGORY_BIT_OVERHANG = 4
const CATEGORY_BIT_STABILIZATION = 8

const props = defineProps({
  hasMesh: { type: Boolean, default: false },
  generating: { type: Boolean, default: false },
  supportResult: { type: Object, default: null },
  interactionMode: { type: String, default: null },
  genProgress: { type: Object, default: () => ({ phase: '', step: 0, total: 4 }) },
})

const emit = defineEmits([
  'auto-generate',
  'set-mode',
  'clear-supports',
  'update:params',
  'update:categories',
])

const categories = [
  { key: 'island', label: 'Islands', bit: CATEGORY_BIT_ISLAND },
  { key: 'reinforcement', label: 'Reinforcement', bit: CATEGORY_BIT_REINFORCEMENT },
  { key: 'overhang', label: 'Overhangs', bit: CATEGORY_BIT_OVERHANG },
  { key: 'stabilization', label: 'Stabilization', bit: CATEGORY_BIT_STABILIZATION },
]

const paramDefs = [
  { key: 'tip_diameter', label: 'Tip', min: 0.1, max: 2.0, step: 0.1 },
  { key: 'shaft_diameter', label: 'Shaft', min: 0.3, max: 5.0, step: 0.1 },
  { key: 'base_diameter', label: 'Base', min: 1.0, max: 10.0, step: 0.5 },
  { key: 'spacing', label: 'Spacing', min: 0.5, max: 20.0, step: 0.5 },
]

const enabledCategories = ref(
  CATEGORY_BIT_ISLAND | CATEGORY_BIT_REINFORCEMENT | CATEGORY_BIT_OVERHANG | CATEGORY_BIT_STABILIZATION
)

const localParams = reactive({
  tip_diameter: 0.3,
  shaft_diameter: 0.8,
  base_diameter: 3.0,
  spacing: 2.0,
})

const progressPercent = computed(() => {
  if (!props.genProgress.total) return 0
  return Math.round((props.genProgress.step / props.genProgress.total) * 100)
})

const PHASE_LABELS = {
  analyzing: 'Analyzing overhangs...',
  sampling: 'Sampling support points...',
  generating_mesh: 'Building pillar geometry...',
  complete: 'Complete',
}

const progressLabel = computed(() => {
  return PHASE_LABELS[props.genProgress.phase] || 'Working...'
})

function isCategoryEnabled(bit) {
  return (enabledCategories.value & bit) !== 0
}

function toggleCategory(bit) {
  enabledCategories.value ^= bit
  emit('update:categories', enabledCategories.value)
}

function getCategoryCount(key) {
  if (!props.supportResult) return 0
  const map = {
    island: props.supportResult.islandCount,
    reinforcement: props.supportResult.reinforcementCount,
    overhang: props.supportResult.overhangCount,
    stabilization: props.supportResult.stabilizationCount,
  }
  return map[key] || 0
}

watch(localParams, () => {
  emit('update:params', { ...localParams, enabled_categories: enabledCategories.value })
}, { deep: true })

defineExpose({ enabledCategories, localParams })
</script>

<style scoped>
.support-panel {
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

.category-toggles {
  display: flex;
  flex-direction: column;
  gap: 4px;
  margin-bottom: 12px;
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
  accent-color: #cc8833;
}

.cat-count {
  color: #888;
  font-size: 11px;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.param-section {
  margin-bottom: 12px;
}

.param-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
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
  width: 60px;
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

.param-input:focus { border-color: #cc8833; }

.unit {
  color: #666;
  font-size: 11px;
}

.action-buttons {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.action-btn {
  padding: 6px 10px;
  border: 1px solid #444;
  border-radius: 3px;
  background: #2a2a4a;
  color: #ccc;
  cursor: pointer;
  font-size: 12px;
}

.action-btn:hover:not(:disabled) { background: #3a3a5a; }
.action-btn:disabled { opacity: 0.4; cursor: not-allowed; }

.action-btn.primary {
  background: #4a3520;
  border-color: #cc8833;
  color: #ffcc88;
}
.action-btn.primary:hover:not(:disabled) {
  background: #5a4530;
}

.mode-buttons {
  display: flex;
  gap: 6px;
}

.mode-btn {
  flex: 1;
}

.mode-btn.active {
  background: #3a5a3a;
  border-color: #5a8a5a;
  color: #aaddaa;
}

.full-width { width: 100%; }

.gen-progress {
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
  background: #cc8833;
  transition: width 0.15s ease;
}

.progress-label {
  display: block;
  margin-top: 4px;
  font-size: 11px;
  color: #888;
}

.support-info {
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
</style>
