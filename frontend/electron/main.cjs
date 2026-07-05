/// @file main.cjs
/// @brief Electron main process — spawns C++ backend, handles stdio IPC with frame parsing.
const { app, BrowserWindow, ipcMain } = require('electron')
const { spawn } = require('child_process')
const path = require('path')

const FRAME_JSON = 0x01
const FRAME_BINARY = 0x02

let mainWindow = null
let backend = null

function sendToRenderer(channel, ...args) {
  if (mainWindow) mainWindow.webContents.send(channel, ...args)
}

function handleBackendExit() {
  backend = null
  sendToRenderer('backend-disconnected')
}

function getBackendPath() {
  const backendDir = path.resolve(__dirname, '../../backend/zig-out/bin')
  return path.join(backendDir, 'guava-slicer-backend')
}

function spawnBackend() {
  const backendPath = getBackendPath()
  console.log('[electron] spawning backend:', backendPath)

  backend = spawn(backendPath, [], { stdio: ['pipe', 'pipe', 'pipe'] })

  let buffer = Buffer.alloc(0)

  backend.stdout.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk])
    parseFrames()
  })

  function parseFrames() {
    while (buffer.length > 0) {
      const frameType = buffer[0]

      if (frameType === FRAME_JSON) {
        const newlineIdx = buffer.indexOf(0x0a, 1)
        if (newlineIdx === -1) return
        const jsonStr = buffer.subarray(1, newlineIdx).toString('utf-8')
        buffer = buffer.subarray(newlineIdx + 1)

        try {
          const msg = JSON.parse(jsonStr)
          sendToRenderer('ipc-message', msg)
        } catch (e) {
          console.error('[electron] failed to parse JSON frame:', e.message)
        }
      } else if (frameType === FRAME_BINARY) {
        if (buffer.length < 5) return
        const length = buffer.readUInt32LE(1)
        const totalFrameSize = 1 + 4 + length
        if (buffer.length < totalFrameSize) return
        sendToRenderer('ipc-binary', length)
        buffer = buffer.subarray(totalFrameSize)
      } else {
        console.error('[electron] unknown frame type:', frameType)
        buffer = buffer.subarray(1)
      }
    }
  }

  backend.stderr.on('data', (data) => {
    process.stderr.write(data)
  })

  backend.on('close', (code) => {
    console.log('[electron] backend exited with code:', code)
    handleBackendExit()
  })

  backend.on('error', (err) => {
    console.error('[electron] backend spawn error:', err.message)
    handleBackendExit()
  })

  sendToRenderer('backend-connected')
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 900,
    height: 600,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  })

  if (process.env.VITE_DEV_SERVER_URL) {
    mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL)
  } else {
    mainWindow.loadFile(path.join(__dirname, '../dist/index.html'))
  }

  mainWindow.on('closed', () => {
    mainWindow = null
  })
}

ipcMain.on('ipc-send', (_event, jsonStr) => {
  if (backend && backend.stdin.writable) {
    backend.stdin.write(jsonStr + '\n')
  }
})

app.whenReady().then(() => {
  createWindow()
  mainWindow.webContents.on('did-finish-load', () => {
    spawnBackend()
  })
})

app.on('window-all-closed', () => {
  if (backend) {
    backend.kill()
    backend = null
  }
  app.quit()
})
