/// @file dev.cjs
/// @brief Dev launcher — starts Vite, waits for server, then launches Electron.
const { spawn } = require('child_process')
const http = require('http')
const path = require('path')

const VITE_PORT = 5173
const VITE_URL = `http://localhost:${VITE_PORT}`
const POLL_INTERVAL_MS = 200
const MAX_RETRIES = 50

const vite = spawn('npx', ['vite', '--port', String(VITE_PORT)], {
  stdio: 'inherit',
  cwd: path.resolve(__dirname, '..'),
})

let retries = 0
const poll = setInterval(() => {
  http.get(VITE_URL, (res) => {
    if (res.statusCode === 200) {
      clearInterval(poll)
      console.log('[dev] vite ready, launching electron')
      const electron = spawn('npx', ['electron', '.'], {
        stdio: 'inherit',
        cwd: path.resolve(__dirname, '..'),
        env: { ...process.env, VITE_DEV_SERVER_URL: VITE_URL },
      })
      electron.on('close', () => {
        vite.kill()
        process.exit(0)
      })
    }
  }).on('error', () => {
    retries++
    if (retries > MAX_RETRIES) {
      console.error('[dev] vite did not start in time')
      vite.kill()
      process.exit(1)
    }
  })
}, POLL_INTERVAL_MS)

process.on('SIGINT', () => {
  vite.kill()
  process.exit(0)
})
