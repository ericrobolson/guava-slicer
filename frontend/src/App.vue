<template>
  <div class="app" @keydown="onGlobalKeyDown">
    <header class="toolbar">
      <button @click="openFile" :disabled="!connected || busy">Open File</button>
      <span v-if="filePath" class="file-name">{{ fileBaseName }}</span>

      <div class="toolbar-spacer"></div>

      <button
        @click="doUndo"
        :disabled="!transformState.can_undo"
        :title="transformState.undo_name ? 'Undo: ' + transformState.undo_name : 'Nothing to undo'"
        class="toolbar-icon"
      >Undo</button>
      <button
        @click="doRedo"
        :disabled="!transformState.can_redo"
        :title="transformState.redo_name ? 'Redo: ' + transformState.redo_name : 'Nothing to redo'"
        class="toolbar-icon"
      >Redo</button>

      <button class="toolbar-icon" @click="showKeybindings = true" title="Keybindings">Keys</button>

      <span :class="['status', connected ? 'connected' : 'disconnected']">
        {{ connected ? 'Connected' : 'Disconnected' }}
      </span>
    </header>

    <div v-if="phase" :class="['progress-bar', { indeterminate: phase === 'processing' }]">
      <div class="progress-fill" :style="{ width: progressPercent + '%' }"></div>
      <span class="progress-text">{{ phaseLabel }}</span>
    </div>

    <div v-if="errorMessage" class="error-banner">
      <span>{{ errorMessage }}</span>
      <button class="dismiss" @click="errorMessage = null">×</button>
    </div>

    <div class="main-content">
      <div class="left-sidebar-area">
        <OverhangPanel
          :hasMesh="!!meshData"
          :enabled="overhangEnabled"
          :threshold="overhangThreshold"
          :overhangData="overhangData"
          :orientationCache="orientationCache"
          :computingAxis="computingAxis"
          :precomputeDone="precomputeDone"
          :precomputeTotal="precomputeTotal"
          @toggle-overlay="overhangEnabled = !overhangEnabled"
          @update:threshold="onThresholdChange"
          @apply-orientation="onApplyOrientation"
        />
        <SlicePanel
          :hasMesh="!!meshData"
          :inspecting="sliceInspecting"
          :slicing="sliceSlicing"
          :layerCount="sliceLayerCount"
          :currentLayer="sliceCurrentLayer"
          :zHeight="sliceCurrentZ"
          :layerHeight="sliceLayerHeight"
          :warningCount="sliceWarningCount"
          :sliceProgress="sliceProgress"
          :islandDetecting="islandDetecting"
          :islandResult="islandResult"
          :currentLayerIslands="currentLayerIslands"
          @toggle-inspect="toggleSliceInspect"
          @jump-to-layer="onSliceLayerChange"
          @update:layerHeight="onLayerHeightChange"
        />
        <SupportPanel
          :hasMesh="!!meshData"
          :generating="supportGenerating"
          :supportResult="supportResult"
          :interactionMode="supportMode"
          :genProgress="supportProgress"
          @auto-generate="autoGenerateSupports"
          @set-mode="onSupportModeChange"
          @clear-supports="clearSupports"
          @update:params="onSupportParamsChange"
          @update:categories="onSupportCategoriesChange"
        />
      </div>
      <Viewport
        :meshData="meshData"
        :transformMatrix="transformState.matrix"
        :overhangIndices="overhangIndices"
        :overhangVisible="overhangEnabled"
        :sliceInspecting="sliceInspecting"
        :sliceLayerZ="sliceCurrentZ"
        :sliceContours="sliceContours"
        :sliceLayerCount="sliceLayerCount"
        :sliceCurrentLayer="sliceCurrentLayer"
        :islandContourIndices="currentLayerIslands ? currentLayerIslands.islands.map(i => i.contour_index) : null"
        :severityScores="islandResult ? islandResult.severityScores : null"
        :supportMeshData="supportMeshData"
        :supportMode="supportMode"
        @drop-file="loadFromPath"
        @orient="onOrient"
        @update:sliceLayer="onSliceLayerChange"
        @place-support="onPlaceSupport"
        @remove-support="onRemoveSupport"
      />
      <div class="sidebar-area">
        <Sidebar
          :stats="meshStats"
          :filePath="filePath"
          :transformState="transformState"
          :hasMesh="!!meshData"
          @orient="onOrient"
        />
      </div>
    </div>

    <KeybindingsModal :visible="showKeybindings" @close="showKeybindings = false" />
  </div>
</template>

<script setup>
import { ref, computed, reactive } from 'vue'
import Viewport from './components/Viewport.vue'
import Sidebar from './components/Sidebar.vue'
import OverhangPanel from './components/OverhangPanel.vue'
import SlicePanel from './components/SlicePanel.vue'
import SupportPanel from './components/SupportPanel.vue'
import KeybindingsModal from './components/KeybindingsModal.vue'
import { basename } from './utils/path.js'

const connected = ref(false)
const showKeybindings = ref(false)
const filePath = ref('')
const errorMessage = ref(null)
const meshData = ref(null)
const meshStats = ref(null)
const phase = ref(null)
const progress = ref({ read: 0, total: 0 })

const transformState = reactive({
  matrix: null,
  can_undo: false,
  can_redo: false,
  undo_name: '',
  redo_name: '',
})

const overhangEnabled = ref(true)
const overhangThreshold = ref(45)
const overhangIndices = ref(null)
const overhangData = ref(null)
const orientationCache = reactive({})
const computingAxis = ref(null)
const precomputeDone = ref(0)
const precomputeTotal = ref(5)

const sliceInspecting = ref(false)
const sliceSlicing = ref(false)
const sliceLayerCount = ref(0)
const sliceCurrentLayer = ref(0)
const sliceCurrentZ = ref(0)
const sliceLayerHeight = ref(0.06)
const sliceWarningCount = ref(0)
const sliceContours = ref(null)
const sliceProgress = ref({ current: 0, total: 0 })
let sliceReady = false

const islandDetecting = ref(false)
const islandResult = ref(null)
const currentLayerIslands = ref(null)

const supportGenerating = ref(false)
const supportResult = ref(null)
const supportMeshData = ref(null)
const supportMode = ref(null)
const supportProgress = ref({ phase: '', step: 0, total: 4 })
let supportParams = {
  tip_diameter: 0.2, shaft_diameter: 0.5, base_diameter: 2.0, spacing: 0.8,
  tip_end_diameter: 0.3, raycast_margin: 0.5,
  enabled_categories: 5,
}

const pendingCallbacks = new Map()
const progressCallbacks = new Map()

const fileBaseName = computed(() => basename(filePath.value))
const busy = computed(() => phase.value !== null)

function formatError(msg, fallback = 'Unknown error') {
  return msg.error ? `${msg.error.code}: ${msg.error.message}` : fallback
}

const progressPercent = computed(() => {
  if (phase.value === 'processing') return 100
  if (!progress.value.total) return 0
  return Math.round((progress.value.read / progress.value.total) * 100)
})

const phaseLabel = computed(() => {
  if (phase.value === 'processing') return 'Processing mesh...'
  return `Loading... ${progressPercent.value}%`
})

function sendCommand(cmd, params, onProgress) {
  return new Promise((resolve) => {
    if (!window.electronIPC) { resolve(null); return }
    const id = crypto.randomUUID()
    pendingCallbacks.set(id, resolve)
    if (onProgress) progressCallbacks.set(id, onProgress)
    window.electronIPC.send(JSON.stringify({ cmd, id, params }))
  })
}

function updateTransformState(result) {
  if (!result) return
  if (result.transform_matrix) transformState.matrix = result.transform_matrix
  if ('can_undo' in result) transformState.can_undo = result.can_undo
  if ('can_redo' in result) transformState.can_redo = result.can_redo
  if ('undo_name' in result) transformState.undo_name = result.undo_name
  if ('redo_name' in result) transformState.redo_name = result.redo_name

  if (result.bounding_box && meshStats.value) {
    meshStats.value = { ...meshStats.value, bounding_box: result.bounding_box }
  }
  if (result.dimensions && meshStats.value) {
    meshStats.value = { ...meshStats.value, dimensions: result.dimensions }
  }
}

function loadFromPath(path) {
  if (!window.electronIPC || !path) return

  filePath.value = path
  phase.value = 'loading'
  errorMessage.value = null
  meshData.value = null
  meshStats.value = null
  progress.value = { read: 0, total: 0 }

  sendCommand('load_mesh', { path }).then((msg) => {
    if (!msg) return

    if (msg.ok) {
      meshStats.value = msg.result
      updateTransformState(msg.result)

      if (msg._binaryPaths && msg._binaryPaths.length >= 3) {
        phase.value = 'processing'

        requestAnimationFrame(() => {
          const positions = new Float32Array(window.electronIPC.readBinaryFile(msg._binaryPaths[0]))
          const normals = new Float32Array(window.electronIPC.readBinaryFile(msg._binaryPaths[1]))
          const indices = new Uint32Array(window.electronIPC.readBinaryFile(msg._binaryPaths[2]))

          meshData.value = { positions, normals, indices }
          phase.value = null
          onOrient({ type: 'place_on_plate' }).then(() => {
            analyzeOverhangs()
            precomputeOrientations()
            scheduleSlice()
          })
        })
      } else {
        phase.value = null
      }
    } else {
      errorMessage.value = formatError(msg, 'Unknown error loading mesh')
      phase.value = null
    }
  })
}

async function openFile() {
  if (!window.electronIPC) return
  const path = await window.electronIPC.openFileDialog()
  if (path) loadFromPath(path)
}

async function onOrient(params) {
  const msg = await sendCommand('orient_model', params)
  if (!msg) return
  if (msg.ok) {
    updateTransformState(msg.result)
    if (msg.result.supports_cleared) {
      supportMeshData.value = null
      supportResult.value = null
    }
    if (overhangEnabled.value && meshData.value) scheduleOverhangAnalysis()
    sliceReady = false
    scheduleSlice()
  } else {
    errorMessage.value = formatError(msg, 'Transform failed')
  }
}

const OVERHANG_DEBOUNCE_MS = 300
let overhangDebounceTimer = null

async function analyzeOverhangs() {
  if (!meshData.value) return
  const msg = await sendCommand('analyze_overhangs', { threshold: overhangThreshold.value })
  if (!msg || !msg.ok) return

  const result = msg.result
  overhangData.value = {
    count: result.overhang_count,
    area: result.overhang_area,
    totalArea: result.total_area,
  }

  if (result.overhang_count > 0 && msg._binaryPaths && msg._binaryPaths.length >= 1) {
    const buf = window.electronIPC.readBinaryFile(msg._binaryPaths[0])
    overhangIndices.value = new Uint32Array(buf)
  } else {
    overhangIndices.value = null
  }
}

function scheduleOverhangAnalysis() {
  if (overhangDebounceTimer) clearTimeout(overhangDebounceTimer)
  overhangDebounceTimer = setTimeout(() => { analyzeOverhangs() }, OVERHANG_DEBOUNCE_MS)
}

let precomputeDebounceTimer = null

function onThresholdChange(value) {
  overhangThreshold.value = value
  scheduleOverhangAnalysis()
  if (precomputeDebounceTimer) clearTimeout(precomputeDebounceTimer)
  precomputeDebounceTimer = setTimeout(() => {
    sendCommand('cancel_auto_orient', {}).then(() => precomputeOrientations())
  }, 1000)
}

function clearOrientationCache() {
  Object.keys(orientationCache).forEach(k => delete orientationCache[k])
  precomputeDone.value = 0
  computingAxis.value = null
}

function precomputeOrientations() {
  if (!meshData.value) return
  clearOrientationCache()

  sendCommand(
    'precompute_orientations',
    { threshold: overhangThreshold.value },
    (data) => {
      if (data.status === 'computing') {
        computingAxis.value = data.axis
      } else if (data.status === 'done' && data.result) {
        orientationCache[data.axis] = data.result
        precomputeDone.value = (data.index || 0) + 1
        computingAxis.value = null
      }
    }
  ).then((msg) => {
    computingAxis.value = null
    if (msg && !msg.ok && msg.error && msg.error.code !== 'CANCELLED') {
      errorMessage.value = formatError(msg, 'Precompute failed')
    }
  })
}

const SLICE_DEBOUNCE_MS = 500
let sliceDebounceTimer = null

async function triggerSlice() {
  if (!meshData.value) return
  clearIslandState()
  sliceSlicing.value = true
  sliceProgress.value = { current: 0, total: 0 }

  const msg = await sendCommand('slice', { layer_height: sliceLayerHeight.value }, (data) => {
    if (data.layer !== undefined && data.total !== undefined) {
      sliceProgress.value = { current: data.layer, total: data.total }
    }
  })

  sliceSlicing.value = false
  if (!msg || !msg.ok) {
    if (msg && msg.error && msg.error.code !== 'CANCELLED') {
      errorMessage.value = msg ? formatError(msg, 'Slice failed') : 'Slice failed'
    }
    sliceReady = false
    return
  }

  sliceLayerCount.value = msg.result.layer_count
  sliceWarningCount.value = msg.result.warning_count
  sliceReady = true

  detectIslands()

  if (sliceInspecting.value && sliceLayerCount.value > 0) {
    fetchLayer(sliceCurrentLayer.value)
  }
}

function scheduleSlice() {
  if (sliceDebounceTimer) clearTimeout(sliceDebounceTimer)
  sliceDebounceTimer = setTimeout(() => { triggerSlice() }, SLICE_DEBOUNCE_MS)
}

async function fetchLayer(index) {
  if (index < 0 || index >= sliceLayerCount.value) return
  const msg = await sendCommand('get_layer', { layer_index: index })
  if (!msg || !msg.ok) return

  sliceCurrentZ.value = msg.result.z_height
  sliceContours.value = msg.result.contours
}

async function toggleSliceInspect() {
  if (sliceInspecting.value) {
    sliceInspecting.value = false
    sliceContours.value = null
    return
  }

  if (!sliceReady) {
    await triggerSlice()
  }

  if (sliceLayerCount.value > 0) {
    sliceInspecting.value = true
    sliceCurrentLayer.value = 0
    fetchLayer(0)
    fetchIslandLayer(0)
  }
}

function onSliceLayerChange(index) {
  sliceCurrentLayer.value = index
  fetchLayer(index)
  fetchIslandLayer(index)
}

async function detectIslands() {
  if (!meshData.value || !sliceReady) return
  islandDetecting.value = true

  const msg = await sendCommand('detect_islands', {})

  islandDetecting.value = false
  if (!msg || !msg.ok) {
    if (msg && msg.error && msg.error.code !== 'CANCELLED') {
      errorMessage.value = msg ? formatError(msg, 'Island detection failed') : 'Island detection failed'
    }
    islandResult.value = null
    return
  }

  islandResult.value = {
    totalCount: msg.result.total_island_count,
    worstLayer: msg.result.worst_layer_index,
    maxSeverity: msg.result.max_severity,
    severityScores: msg.result.severity_scores,
    islandLayerCount: msg.result.island_layer_count,
  }

  if (sliceInspecting.value) {
    fetchIslandLayer(sliceCurrentLayer.value)
  }
}

async function fetchIslandLayer(index) {
  if (!islandResult.value || index < 0) {
    currentLayerIslands.value = null
    return
  }
  const msg = await sendCommand('get_island_layer', { layer_index: index })
  if (!msg || !msg.ok) {
    currentLayerIslands.value = null
    return
  }
  currentLayerIslands.value = {
    islands: msg.result.islands,
    count: msg.result.island_count,
    area: msg.result.total_island_area,
    severity: msg.result.severity,
  }
}

function clearIslandState() {
  islandResult.value = null
  currentLayerIslands.value = null
  islandDetecting.value = false
}

function onLayerHeightChange(height) {
  sliceLayerHeight.value = height
  sliceReady = false
  if (sliceInspecting.value) {
    triggerSlice()
  } else {
    scheduleSlice()
  }
}

async function onApplyOrientation(axisKey) {
  const cached = orientationCache[axisKey]
  if (!cached) return

  const msg = await sendCommand('apply_orientation', { rotation: cached.rotation })
  if (!msg) return
  if (msg.ok) {
    updateTransformState(msg.result)
    if (msg.result.supports_cleared) {
      supportMeshData.value = null
      supportResult.value = null
    }
    analyzeOverhangs()
  } else {
    errorMessage.value = formatError(msg, 'Apply orientation failed')
  }
}

async function doUndo() {
  const msg = await sendCommand('undo', {})
  if (!msg) return
  if (msg.ok) {
    updateTransformState(msg.result)
    if (!msg.result.has_mesh && msg.result.has_mesh !== undefined) {
      meshData.value = null
      meshStats.value = null
      filePath.value = ''
      overhangIndices.value = null
      overhangData.value = null
      clearOrientationCache()
      clearIslandState()
    } else if (overhangEnabled.value && meshData.value) {
      scheduleOverhangAnalysis()
    }
  } else if (msg.error && msg.error.code !== 'NOTHING_TO_UNDO') {
    errorMessage.value = formatError(msg)
  }
}

async function doRedo() {
  const msg = await sendCommand('redo', {})
  if (!msg) return
  if (msg.ok) {
    updateTransformState(msg.result)
    if (overhangEnabled.value && meshData.value) scheduleOverhangAnalysis()
  } else if (msg.error && msg.error.code !== 'NOTHING_TO_REDO') {
    errorMessage.value = formatError(msg)
  }
}

async function autoGenerateSupports() {
  if (!meshData.value) return
  supportGenerating.value = true
  supportProgress.value = { phase: '', step: 0, total: 4 }

  const msg = await sendCommand('generate_supports', {
    ...supportParams,
    threshold: overhangThreshold.value,
  }, (data) => {
    if (data.phase !== undefined) {
      supportProgress.value = { phase: data.phase, step: data.step || 0, total: data.total || 4 }
    }
  })

  supportGenerating.value = false
  if (!msg || !msg.ok) {
    if (msg && msg.error && msg.error.code !== 'CANCELLED') {
      errorMessage.value = msg ? formatError(msg, 'Support generation failed') : 'Support generation failed'
    }
    return
  }

  updateTransformState(msg.result)
  updateSupportResult(msg.result.supports)

  if (msg.result.binary_follows && msg._binaryPaths && msg._binaryPaths.length >= 3) {
    loadSupportMesh(msg._binaryPaths)
  } else {
    supportMeshData.value = null
  }
}

function updateSupportResult(data) {
  if (!data) { supportResult.value = null; return }
  supportResult.value = {
    totalCount: data.total_count,
    islandCount: data.island_count,
    reinforcementCount: data.reinforcement_count,
    overhangCount: data.overhang_count,
    stabilizationCount: data.stabilization_count,
    vertexCount: data.vertex_count,
    triangleCount: data.triangle_count,
  }
}

function loadSupportMesh(binaryPaths) {
  const positions = new Float32Array(window.electronIPC.readBinaryFile(binaryPaths[0]))
  const normals = new Float32Array(window.electronIPC.readBinaryFile(binaryPaths[1]))
  const indices = new Uint32Array(window.electronIPC.readBinaryFile(binaryPaths[2]))
  supportMeshData.value = { positions, normals, indices }
}

async function onPlaceSupport(position, normal) {
  const msg = await sendCommand('place_support', { position, normal })
  if (!msg || !msg.ok) {
    if (msg) errorMessage.value = formatError(msg, 'Place support failed')
    return
  }
  updateTransformState(msg.result)
  updateSupportResult(msg.result.supports)
  if (msg.result.binary_follows && msg._binaryPaths && msg._binaryPaths.length >= 3) {
    loadSupportMesh(msg._binaryPaths)
  }
}

async function onRemoveSupport(supportId) {
  const msg = await sendCommand('remove_support', { support_id: supportId })
  if (!msg || !msg.ok) {
    if (msg) errorMessage.value = formatError(msg, 'Remove support failed')
    return
  }
  updateTransformState(msg.result)
  updateSupportResult(msg.result.supports)
  if (msg.result.binary_follows && msg._binaryPaths && msg._binaryPaths.length >= 3) {
    loadSupportMesh(msg._binaryPaths)
  } else {
    supportMeshData.value = null
  }
}

async function clearSupports() {
  const msg = await sendCommand('clear_supports', {})
  if (!msg || !msg.ok) return
  updateTransformState(msg.result)
  updateSupportResult(msg.result.supports)
  supportMeshData.value = null
}

function onSupportModeChange(mode) {
  supportMode.value = mode
}

function onSupportParamsChange(params) {
  supportParams = { ...params }
}

function onSupportCategoriesChange(categories) {
  supportParams.enabled_categories = categories
}

function onGlobalKeyDown(e) {
  const ctrl = e.ctrlKey || e.metaKey
  const key = e.key.toLowerCase()
  if (ctrl && key === 'z' && !e.shiftKey) {
    e.preventDefault()
    doUndo()
  } else if (ctrl && key === 'z' && e.shiftKey) {
    e.preventDefault()
    doRedo()
  } else if (ctrl && key === 'y') {
    e.preventDefault()
    doRedo()
  } else if (key === 'p' && !ctrl) {
    e.preventDefault()
    supportMode.value = supportMode.value === 'place' ? null : 'place'
  } else if (key === 'x' && !ctrl) {
    e.preventDefault()
    supportMode.value = supportMode.value === 'remove' ? null : 'remove'
  } else if (key === 'escape') {
    e.preventDefault()
    if (supportMode.value) {
      supportMode.value = null
    } else if (sliceInspecting.value) {
      sliceInspecting.value = false
      sliceContours.value = null
    }
  }
}

function handleMessage(msg) {
  if (msg.event === 'progress' && msg.data && msg.id) {
    const progressCb = progressCallbacks.get(msg.id)
    if (progressCb) {
      progressCb(msg.data)
      return
    }
    const read = msg.data.triangles_read || 0
    const total = msg.data.total || 0
    progress.value = { read, total }

    if (total > 0 && read >= total && phase.value === 'loading') {
      phase.value = 'processing'
    }
    return
  }

  if (msg.id && pendingCallbacks.has(msg.id) && 'ok' in msg) {
    const cb = pendingCallbacks.get(msg.id)
    pendingCallbacks.delete(msg.id)
    progressCallbacks.delete(msg.id)
    cb(msg)
  }
}

if (window.electronIPC) {
  window.electronIPC.onMessage(handleMessage)

  window.electronIPC.onConnected(() => {
    connected.value = true
  })

  window.electronIPC.onDisconnected(() => {
    connected.value = false
  })
}
</script>

<style>
* { margin: 0; padding: 0; box-sizing: border-box; }

body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  background: #1a1a2e;
  color: #e0e0e0;
}

.app {
  display: flex;
  flex-direction: column;
  height: 100vh;
  outline: none;
}

.toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 16px;
  background: #22223a;
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}

.toolbar button {
  padding: 6px 14px;
  border: 1px solid #444;
  border-radius: 4px;
  background: #2a2a4a;
  color: #e0e0e0;
  cursor: pointer;
  font-size: 13px;
}

.toolbar button:hover:not(:disabled) { background: #3a3a5a; }
.toolbar button:disabled { opacity: 0.4; cursor: not-allowed; }

.toolbar-spacer { flex: 1; }

.toolbar-icon {
  padding: 6px 10px !important;
  font-size: 12px !important;
}

.file-name {
  font-size: 13px;
  color: #aaa;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.status {
  font-size: 12px;
  padding: 3px 8px;
  border-radius: 10px;
  flex-shrink: 0;
}

.status.connected { background: #2d5a2d; color: #7ddf7d; }
.status.disconnected { background: #5a2d2d; color: #df7d7d; }

.progress-bar {
  position: relative;
  height: 20px;
  background: #2a2a4a;
  flex-shrink: 0;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: #4a7a4a;
  transition: width 0.1s ease;
}

.progress-bar.indeterminate .progress-fill {
  background: linear-gradient(90deg, #4a7a4a 0%, #6aaa6a 50%, #4a7a4a 100%);
  background-size: 200% 100%;
  animation: shimmer 1.5s ease-in-out infinite;
  width: 100% !important;
}

@keyframes shimmer {
  0% { background-position: 200% 0; }
  100% { background-position: -200% 0; }
}

.progress-text {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  font-size: 11px;
  font-weight: 600;
}

.error-banner {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 16px;
  background: #5a2d2d;
  color: #df7d7d;
  font-size: 13px;
  flex-shrink: 0;
}

.error-banner .dismiss {
  background: none;
  border: none;
  color: #df7d7d;
  font-size: 18px;
  cursor: pointer;
  padding: 0 4px;
}

.main-content {
  display: flex;
  flex: 1;
  overflow: hidden;
}

.left-sidebar-area {
  width: 240px;
  background: #1e1e36;
  border-right: 1px solid #333;
  overflow-y: auto;
  flex-shrink: 0;
  padding-top: 8px;
}

.sidebar-area {
  width: 240px;
  background: #1e1e36;
  border-left: 1px solid #333;
  overflow-y: auto;
  flex-shrink: 0;
}
</style>
