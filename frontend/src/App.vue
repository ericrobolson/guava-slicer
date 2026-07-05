<template>
  <div class="app">
    <header class="header">
      <h1>Guava Slicer — IPC Console</h1>
      <span :class="['status', connected ? 'connected' : 'disconnected']">
        {{ connected ? 'Backend Connected' : 'Backend Disconnected' }}
      </span>
    </header>

    <div class="controls">
      <button @click="sendPing" :disabled="!connected">Ping</button>
      <button @click="sendSimulate" :disabled="!connected">Simulate (10 steps)</button>
      <button @click="sendGetBinary" :disabled="!connected">Get Binary (1KB)</button>
      <button @click="clearLog">Clear Log</button>
    </div>

    <div v-if="progress !== null" class="progress-bar">
      <div class="progress-fill" :style="{ width: progressPercent + '%' }"></div>
      <span class="progress-text">{{ progress.step }} / {{ progress.total }}</span>
    </div>

    <div class="log" ref="logEl">
      <div v-for="(entry, i) in log" :key="i" :class="['log-entry', entry.direction]">
        <span class="log-time">{{ formatTime(entry.timestamp) }}</span>
        <span class="log-dir">{{ entry.direction === 'sent' ? '→' : '←' }}</span>
        <pre class="log-body">{{ entry.raw }}</pre>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, nextTick, computed } from 'vue'

const connected = ref(false)
const log = ref([])
const progress = ref(null)
const logEl = ref(null)

const progressPercent = computed(() => {
  if (!progress.value) return 0
  return Math.round((progress.value.step / progress.value.total) * 100)
})

function formatTime(ts) {
  const d = new Date(ts)
  return d.toLocaleTimeString('en-US', { hour12: false }) + '.' + String(d.getMilliseconds()).padStart(3, '0')
}

function addLog(direction, raw) {
  log.value.push({ direction, timestamp: Date.now(), raw })
  nextTick(() => {
    if (logEl.value) logEl.value.scrollTop = logEl.value.scrollHeight
  })
}

function sendCommand(cmd, params = {}) {
  if (!window.electronIPC) return
  const id = crypto.randomUUID()
  const request = { cmd, id, params }
  addLog('sent', JSON.stringify(request, null, 2))
  window.electronIPC.send(JSON.stringify(request))
  return id
}

function sendPing() {
  sendCommand('ping')
}

function sendSimulate() {
  progress.value = { step: 0, total: 10 }
  sendCommand('simulate', { steps: 10 })
}

function sendGetBinary() {
  sendCommand('get_binary', { size: 1024 })
}

function clearLog() {
  log.value = []
  progress.value = null
}

if (window.electronIPC) {
  window.electronIPC.onMessage((msg) => {
    addLog('received', JSON.stringify(msg, null, 2))

    if (msg.event === 'progress' && msg.data) {
      progress.value = { step: msg.data.step, total: msg.data.total }
    }

    if ('ok' in msg && progress.value) {
      progress.value = null
    }
  })

  window.electronIPC.onBinary((byteLength) => {
    addLog('received', `[binary frame: ${byteLength} bytes]`)
  })

  window.electronIPC.onConnected(() => {
    connected.value = true
    addLog('received', '[backend connected]')
  })

  window.electronIPC.onDisconnected(() => {
    connected.value = false
    addLog('received', '[backend disconnected]')
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
  padding: 16px;
  gap: 12px;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header h1 {
  font-size: 18px;
  font-weight: 600;
}

.status {
  font-size: 13px;
  padding: 4px 10px;
  border-radius: 12px;
}

.status.connected { background: #2d5a2d; color: #7ddf7d; }
.status.disconnected { background: #5a2d2d; color: #df7d7d; }

.controls {
  display: flex;
  gap: 8px;
}

.controls button {
  padding: 8px 16px;
  border: 1px solid #444;
  border-radius: 6px;
  background: #2a2a4a;
  color: #e0e0e0;
  cursor: pointer;
  font-size: 13px;
}

.controls button:hover:not(:disabled) { background: #3a3a5a; }
.controls button:disabled { opacity: 0.4; cursor: not-allowed; }

.progress-bar {
  position: relative;
  height: 24px;
  background: #2a2a4a;
  border-radius: 4px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: #4a7a4a;
  transition: width 0.15s ease;
}

.progress-text {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  font-size: 12px;
  font-weight: 600;
}

.log {
  flex: 1;
  overflow-y: auto;
  background: #111122;
  border-radius: 6px;
  padding: 8px;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 12px;
}

.log-entry {
  display: flex;
  gap: 8px;
  padding: 4px 0;
  border-bottom: 1px solid #222233;
}

.log-entry.sent { color: #7db4df; }
.log-entry.received { color: #b4df7d; }

.log-time { color: #666; white-space: nowrap; }
.log-dir { font-weight: bold; }

.log-body {
  white-space: pre-wrap;
  word-break: break-all;
  margin: 0;
  font-family: inherit;
  font-size: inherit;
}
</style>
