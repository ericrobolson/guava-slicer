/// @file preload.cjs
/// @brief Electron preload — exposes IPC bridge to renderer via contextBridge.
const { contextBridge, ipcRenderer, webUtils } = require('electron')
const fs = require('fs')

contextBridge.exposeInMainWorld('electronIPC', {
  send: (jsonStr) => ipcRenderer.send('ipc-send', jsonStr),
  onMessage: (cb) => ipcRenderer.on('ipc-message', (_event, msg) => cb(msg)),
  onConnected: (cb) => ipcRenderer.on('backend-connected', () => cb()),
  onDisconnected: (cb) => ipcRenderer.on('backend-disconnected', () => cb()),
  openFileDialog: () => ipcRenderer.invoke('open-file-dialog'),
  getPathForFile: (file) => webUtils.getPathForFile(file),
  readBinaryFile: (filePath) => {
    const buf = fs.readFileSync(filePath)
    fs.unlinkSync(filePath)
    return buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength)
  },
})
