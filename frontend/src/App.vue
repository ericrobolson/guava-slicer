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
      <Viewport
        :meshData="meshData"
        :transformMatrix="transformState.matrix"
        @drop-file="loadFromPath"
        @orient="onOrient"
      />
      <Sidebar
        :stats="meshStats"
        :filePath="filePath"
        :transformState="transformState"
        :hasMesh="!!meshData"
        @orient="onOrient"
      />
    </div>

    <KeybindingsModal :visible="showKeybindings" @close="showKeybindings = false" />
  </div>
</template>

<script setup>
import { ref, computed, reactive } from 'vue'
import Viewport from './components/Viewport.vue'
import Sidebar from './components/Sidebar.vue'
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

const pendingCallbacks = new Map()

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

function sendCommand(cmd, params) {
  return new Promise((resolve) => {
    if (!window.electronIPC) { resolve(null); return }
    const id = crypto.randomUUID()
    pendingCallbacks.set(id, resolve)
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
          // Auto-place on build plate so the mesh sits on the grid
          onOrient({ type: 'place_on_plate' })
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
  } else {
    errorMessage.value = formatError(msg, 'Transform failed')
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
  } else if (msg.error && msg.error.code !== 'NOTHING_TO_REDO') {
    errorMessage.value = formatError(msg)
  }
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
  }
}

function handleMessage(msg) {
  if (msg.event === 'progress' && msg.data) {
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
</style>
