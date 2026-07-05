/// @file preload.cjs
/// @brief Electron preload — exposes IPC bridge to renderer via contextBridge.
const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('electronIPC', {
  send: (jsonStr) => ipcRenderer.send('ipc-send', jsonStr),
  onMessage: (cb) => ipcRenderer.on('ipc-message', (_event, msg) => cb(msg)),
  onBinary: (cb) => ipcRenderer.on('ipc-binary', (_event, byteLength) => cb(byteLength)),
  onConnected: (cb) => ipcRenderer.on('backend-connected', () => cb()),
  onDisconnected: (cb) => ipcRenderer.on('backend-disconnected', () => cb()),
})
