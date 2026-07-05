/// @file main.cjs
/// @brief Electron main process — spawns C++ backend, handles stdio IPC with frame parsing.
const { app, BrowserWindow, ipcMain, dialog } = require('electron')
const { spawn } = require('child_process')
const path = require('path')
const fs = require('fs')
const os = require('os')

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

  let pendingChunks = []
  let pendingLen = 0
  let binaryFileIndex = 0
  const pendingBinaryPaths = []

  function collapse() {
    if (pendingChunks.length === 0) return Buffer.alloc(0)
    if (pendingChunks.length === 1) return pendingChunks[0]
    const buf = Buffer.concat(pendingChunks, pendingLen)
    pendingChunks = [buf]
    return buf
  }

  function consume(n) {
    const buf = collapse()
    pendingChunks = buf.length > n ? [buf.subarray(n)] : []
    pendingLen = pendingChunks.length > 0 ? pendingChunks[0].length : 0
  }

  backend.stdout.on('data', (chunk) => {
    pendingChunks.push(chunk)
    pendingLen += chunk.length
    parseFrames()
  })

  function parseFrames() {
    while (pendingLen > 0) {
      const buf = collapse()
      const frameType = buf[0]

      if (frameType === FRAME_JSON) {
        const newlineIdx = buf.indexOf(0x0a, 1)
        if (newlineIdx === -1) return
        const jsonStr = buf.subarray(1, newlineIdx).toString('utf-8')
        consume(newlineIdx + 1)

        try {
          const msg = JSON.parse(jsonStr)
          if ('ok' in msg && pendingBinaryPaths.length > 0) {
            if (msg.ok) {
              msg._binaryPaths = [...pendingBinaryPaths]
            } else {
              pendingBinaryPaths.forEach(p => { try { fs.unlinkSync(p) } catch {} })
            }
            pendingBinaryPaths.length = 0
          }
          sendToRenderer('ipc-message', msg)
        } catch (e) {
          console.error('[electron] failed to parse JSON frame:', e.message)
        }
      } else if (frameType === FRAME_BINARY) {
        if (pendingLen < 5) return
        const length = buf.readUInt32LE(1)
        const totalFrameSize = 1 + 4 + length
        if (pendingLen < totalFrameSize) return

        const binaryData = buf.subarray(5, totalFrameSize)
        const tmpPath = path.join(os.tmpdir(), `guava-bin-${process.pid}-${binaryFileIndex++}.raw`)
        fs.writeFileSync(tmpPath, binaryData)
        pendingBinaryPaths.push(tmpPath)
        consume(totalFrameSize)
      } else {
        console.error('[electron] unknown frame type:', frameType)
        consume(1)
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
    width: 1280,
    height: 800,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
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

ipcMain.handle('open-file-dialog', async () => {
  const result = await dialog.showOpenDialog(mainWindow, {
    filters: [{ name: 'STL Files', extensions: ['stl'] }],
    properties: ['openFile'],
  })
  if (result.canceled || result.filePaths.length === 0) return null
  return result.filePaths[0]
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
