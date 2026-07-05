<template>
  <div class="app">
    <header class="toolbar">
      <button @click="openFile" :disabled="!connected || busy">Open File</button>
      <span v-if="filePath" class="file-name">{{ fileBaseName }}</span>
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
      <Viewport :meshData="meshData" @drop-file="loadFromPath" />
      <Sidebar :stats="meshStats" :filePath="filePath" />
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import Viewport from './components/Viewport.vue'
import Sidebar from './components/Sidebar.vue'
import { basename } from './utils/path.js'

const connected = ref(false)
const filePath = ref('')
const errorMessage = ref(null)
const meshData = ref(null)
const meshStats = ref(null)
const phase = ref(null)
const progress = ref({ read: 0, total: 0 })

let pendingRequestId = null

const fileBaseName = computed(() => basename(filePath.value))

const busy = computed(() => phase.value !== null)

const progressPercent = computed(() => {
  if (phase.value === 'processing') return 100
  if (!progress.value.total) return 0
  return Math.round((progress.value.read / progress.value.total) * 100)
})

const phaseLabel = computed(() => {
  if (phase.value === 'processing') return 'Processing mesh...'
  return `Loading... ${progressPercent.value}%`
})

function loadFromPath(path) {
  if (!window.electronIPC || !path) return

  filePath.value = path
  phase.value = 'loading'
  errorMessage.value = null
  meshData.value = null
  meshStats.value = null
  progress.value = { read: 0, total: 0 }

  const id = crypto.randomUUID()
  pendingRequestId = id
  window.electronIPC.send(JSON.stringify({
    cmd: 'load_mesh',
    id,
    params: { path },
  }))
}

async function openFile() {
  if (!window.electronIPC) return
  const path = await window.electronIPC.openFileDialog()
  if (path) loadFromPath(path)
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

  if (msg.id && msg.id === pendingRequestId && 'ok' in msg) {
    if (msg.ok) {
      meshStats.value = msg.result

      if (msg._binaryPaths && msg._binaryPaths.length >= 3) {
        phase.value = 'processing'

        requestAnimationFrame(() => {
          const positions = new Float32Array(window.electronIPC.readBinaryFile(msg._binaryPaths[0]))
          const normals = new Float32Array(window.electronIPC.readBinaryFile(msg._binaryPaths[1]))
          const indices = new Uint32Array(window.electronIPC.readBinaryFile(msg._binaryPaths[2]))

          meshData.value = { positions, normals, indices }
          phase.value = null
        })
      } else {
        phase.value = null
      }
    } else {
      errorMessage.value = msg.error
        ? `${msg.error.code}: ${msg.error.message}`
        : 'Unknown error loading mesh'
      phase.value = null
    }

    pendingRequestId = null
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

.file-name {
  font-size: 13px;
  color: #aaa;
  flex: 1;
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
