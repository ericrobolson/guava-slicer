<template>
  <div
    ref="container"
    class="viewport"
    tabindex="0"
    @keydown="onKeyDown"
    @keyup="onKeyUp"
    @dragover.prevent="onDragOver"
    @dragleave="onDragLeave"
    @drop.prevent="onDrop"
  >
    <div v-if="dragOver" class="drop-overlay">Drop STL file here</div>
    <div v-if="holdMode" class="mode-indicator">{{ holdModeLabel }} — drag to transform, release to confirm</div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { TransformControls } from 'three/addons/controls/TransformControls.js'
import { computeRotationDelta, computeTranslationDelta, computeScaleDelta } from '../utils/transformDelta.js'

const props = defineProps({
  meshData: { type: Object, default: null },
  transformMatrix: { type: Array, default: null },
  overhangIndices: { type: Uint32Array, default: null },
  overhangVisible: { type: Boolean, default: true },
})

const emit = defineEmits(['drop-file', 'orient'])

const container = ref(null)
const dragOver = ref(false)
const holdMode = ref(null)

const holdModeLabel = computed(() => {
  if (holdMode.value === 'rotate') return 'ROTATE (R)'
  if (holdMode.value === 'translate') return 'GRAB (G)'
  if (holdMode.value === 'scale') return 'SCALE (S)'
  return ''
})

let scene, camera, renderer, controls, gizmo
let meshPivot, meshObject, overhangMesh, overhangMaterial
let axisScene, axisCamera, axisRenderer
let animFrameId = null
let basePosition = null
let baseQuaternion = null
let baseScale = null
let gizmoDragging = false

const ORBIT_STEP_DEG = 15
const BUILD_PLATE_SIZE = 256
const BUILD_PLATE_DIVISIONS = 16
const GRID_COLOR_CENTER = 0x444466
const GRID_COLOR_LINES = 0x333344
const BUILD_PLATE_COLOR = 0x2a2a3a
const MESH_COLOR = 0x5588bb
const MESH_EMISSIVE = 0x060c12
const MESH_SHININESS = 80
const BACKGROUND_COLOR = 0x1a1a2e
const AMBIENT_LIGHT_INTENSITY = 0.35
const DIR_LIGHT_INTENSITY = 1.6
const FILL_LIGHT_INTENSITY = 0.3
const CAMERA_FOV = 50
const CAMERA_NEAR = 0.1
const CAMERA_FAR = 10000
const AXIS_GIZMO_SIZE = 100
const AXIS_LENGTH = 0.8
const AXIS_HEAD_LENGTH = 0.2
const AXIS_HEAD_WIDTH = 0.08
const AXIS_COLOR_X = 0xff4444
const AXIS_COLOR_Y = 0x44cc44
const AXIS_COLOR_Z = 0x4488ff
const AXIS_LABEL_SIZE = 14
const ZOOM_STEP_FACTOR = 0.85
const ROTATE_SENSITIVITY = 0.5
const SCALE_SENSITIVITY = 0.005
const CHECKER_SIZE = 3.0
const CHECKER_OPACITY = 0.85

const overhangVertexShader = `
  varying vec3 vObjPos;
  void main() {
    vObjPos = position;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`
const overhangFragmentShader = `
  uniform float checkerSize;
  uniform float opacity;
  varying vec3 vObjPos;
  void main() {
    float c = mod(floor(vObjPos.x / checkerSize) + floor(vObjPos.y / checkerSize) + floor(vObjPos.z / checkerSize), 2.0);
    vec3 yellow = vec3(1.0, 0.85, 0.0);
    vec3 dark = vec3(0.15, 0.12, 0.0);
    gl_FragColor = vec4(mix(dark, yellow, c), opacity);
  }
`

function initAxisGizmo() {
  axisScene = new THREE.Scene()
  axisCamera = new THREE.OrthographicCamera(-1.2, 1.2, 1.2, -1.2, 0.1, 10)
  axisCamera.position.set(0, 0, 3)

  const axes = [
    { dir: [1,0,0], color: AXIS_COLOR_X, label: 'X' },
    { dir: [0,1,0], color: AXIS_COLOR_Y, label: 'Y' },
    { dir: [0,0,1], color: AXIS_COLOR_Z, label: 'Z' },
  ]
  for (const a of axes) {
    axisScene.add(new THREE.ArrowHelper(
      new THREE.Vector3(...a.dir), new THREE.Vector3(0,0,0),
      AXIS_LENGTH, a.color, AXIS_HEAD_LENGTH, AXIS_HEAD_WIDTH
    ))
    addAxisLabel(a.label, new THREE.Vector3(...a.dir).multiplyScalar(1.05), a.color)
  }

  axisRenderer = new THREE.WebGLRenderer({ antialias: true, alpha: true })
  axisRenderer.setSize(AXIS_GIZMO_SIZE, AXIS_GIZMO_SIZE)
  axisRenderer.setPixelRatio(window.devicePixelRatio)
  axisRenderer.domElement.style.position = 'absolute'
  axisRenderer.domElement.style.bottom = '8px'
  axisRenderer.domElement.style.right = '8px'
  axisRenderer.domElement.style.pointerEvents = 'none'
  container.value.appendChild(axisRenderer.domElement)
}

function addAxisLabel(text, position, color) {
  const canvas = document.createElement('canvas')
  canvas.width = 32; canvas.height = 32
  const ctx = canvas.getContext('2d')
  ctx.font = `bold ${AXIS_LABEL_SIZE}px sans-serif`
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
  ctx.fillStyle = '#' + color.toString(16).padStart(6, '0')
  ctx.fillText(text, 16, 16)
  const texture = new THREE.CanvasTexture(canvas)
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, depthTest: false }))
  sprite.position.copy(position)
  sprite.scale.set(0.3, 0.3, 1)
  axisScene.add(sprite)
}

function updateAxisGizmo() {
  if (!axisCamera || !controls) return
  const dir = camera.position.clone().sub(controls.target).normalize()
  axisCamera.position.copy(dir.multiplyScalar(3))
  axisCamera.lookAt(0, 0, 0)
  axisRenderer.render(axisScene, axisCamera)
}

// --- Gizmo (TransformControls) ---

function initGizmo() {
  gizmo = new TransformControls(camera, renderer.domElement)
  gizmo.setSpace('world')
  gizmo.setSize(0.8)
  scene.add(gizmo.getHelper())

  gizmo.addEventListener('dragging-changed', (e) => {
    controls.enabled = !e.value
    if (e.value) {
      gizmoDragging = true
      basePosition = meshObject.position.clone()
      baseQuaternion = meshObject.quaternion.clone()
      baseScale = meshObject.scale.clone()
    } else if (gizmoDragging) {
      gizmoDragging = false
      emitTransformDelta()
    }
  })
}

// --- Shared: compute and emit transform delta from base → current meshObject state ---

function emitTransformDelta() {
  if (!meshObject) return
  const mode = gizmo.getMode()

  if (mode === 'translate') {
    const result = computeTranslationDelta(basePosition, meshObject.position)
    if (result) emit('orient', { type: 'translate', ...result })
  } else if (mode === 'rotate') {
    const result = computeRotationDelta(baseQuaternion, meshObject.quaternion)
    if (result) emit('orient', { type: 'rotate', ...result })
  } else if (mode === 'scale') {
    const result = computeScaleDelta(baseScale, meshObject.scale)
    if (result) emit('orient', { type: 'scale', ...result })
  }
}

// --- Hold-and-drag transform ---

function startHoldTransform(mode) {
  if (!meshObject) return
  holdMode.value = mode
  controls.enabled = false
  basePosition = meshObject.position.clone()
  baseQuaternion = meshObject.quaternion.clone()
  baseScale = meshObject.scale.clone()

  // Switch gizmo mode to match (visual feedback)
  if (gizmo && meshObject) gizmo.setMode(mode)

  container.value.addEventListener('mousemove', onHoldMouseMove)
}

function onHoldMouseMove(e) {
  if (!meshObject || !holdMode.value) return
  const dx = e.movementX
  const dy = e.movementY

  if (holdMode.value === 'rotate') {
    const yRot = new THREE.Quaternion()
    yRot.setFromAxisAngle(new THREE.Vector3(0, 1, 0), dx * ROTATE_SENSITIVITY * Math.PI / 180)
    const cameraRight = new THREE.Vector3(1, 0, 0).applyQuaternion(camera.quaternion)
    const xRot = new THREE.Quaternion()
    xRot.setFromAxisAngle(cameraRight, dy * ROTATE_SENSITIVITY * Math.PI / 180)
    meshObject.quaternion.premultiply(yRot)
    meshObject.quaternion.premultiply(xRot)

  } else if (holdMode.value === 'translate') {
    const dist = camera.position.distanceTo(controls.target)
    const pixelScale = dist / container.value.clientHeight
    const right = new THREE.Vector3(1, 0, 0).applyQuaternion(camera.quaternion)
    const up = new THREE.Vector3(0, 1, 0).applyQuaternion(camera.quaternion)
    meshObject.position.addScaledVector(right, dx * pixelScale)
    meshObject.position.addScaledVector(up, -dy * pixelScale)

  } else if (holdMode.value === 'scale') {
    const factor = 1 + (-dy * SCALE_SENSITIVITY)
    meshObject.scale.multiplyScalar(Math.max(0.01, factor))
  }
}

function endHoldTransform() {
  if (!holdMode.value) return
  container.value.removeEventListener('mousemove', onHoldMouseMove)
  controls.enabled = true
  holdMode.value = null
  emitTransformDelta()
}

function cancelHoldTransform() {
  if (!holdMode.value) return
  container.value.removeEventListener('mousemove', onHoldMouseMove)
  controls.enabled = true
  holdMode.value = null
  if (meshObject && basePosition) {
    meshObject.position.copy(basePosition)
    meshObject.quaternion.copy(baseQuaternion)
    meshObject.scale.copy(baseScale)
  }
}

// --- Scene setup ---

function init() {
  scene = new THREE.Scene()
  scene.background = new THREE.Color(BACKGROUND_COLOR)

  camera = new THREE.PerspectiveCamera(
    CAMERA_FOV,
    container.value.clientWidth / container.value.clientHeight,
    CAMERA_NEAR, CAMERA_FAR
  )
  camera.position.set(200, 200, 200)

  renderer = new THREE.WebGLRenderer({ antialias: true })
  renderer.setSize(container.value.clientWidth, container.value.clientHeight)
  renderer.setPixelRatio(window.devicePixelRatio)
  container.value.appendChild(renderer.domElement)

  controls = new OrbitControls(camera, renderer.domElement)
  controls.enableDamping = true
  controls.dampingFactor = 0.1

  scene.add(new THREE.AmbientLight(0xffffff, AMBIENT_LIGHT_INTENSITY))

  const dirLight = new THREE.DirectionalLight(0xffffff, DIR_LIGHT_INTENSITY)
  dirLight.position.set(1, 2, 1.5)
  scene.add(dirLight)

  const fillLight = new THREE.DirectionalLight(0x8899bb, FILL_LIGHT_INTENSITY)
  fillLight.position.set(-1, -0.5, -1)
  scene.add(fillLight)

  scene.add(new THREE.GridHelper(BUILD_PLATE_SIZE, BUILD_PLATE_DIVISIONS, GRID_COLOR_CENTER, GRID_COLOR_LINES))

  const plateGeo = new THREE.PlaneGeometry(BUILD_PLATE_SIZE, BUILD_PLATE_SIZE)
  const plateMat = new THREE.MeshBasicMaterial({ color: BUILD_PLATE_COLOR, transparent: true, opacity: 0.5, side: THREE.DoubleSide })
  const buildPlate = new THREE.Mesh(plateGeo, plateMat)
  buildPlate.rotation.x = -Math.PI / 2
  buildPlate.position.y = -0.01
  scene.add(buildPlate)

  overhangMaterial = new THREE.ShaderMaterial({
    uniforms: {
      checkerSize: { value: CHECKER_SIZE },
      opacity: { value: CHECKER_OPACITY },
    },
    vertexShader: overhangVertexShader,
    fragmentShader: overhangFragmentShader,
    transparent: true,
    depthWrite: false,
    side: THREE.DoubleSide,
    polygonOffset: true,
    polygonOffsetFactor: -1,
    polygonOffsetUnits: -1,
  })

  initAxisGizmo()
  initGizmo()

  window.addEventListener('resize', onResize)
  animate()
}

function animate() {
  animFrameId = requestAnimationFrame(animate)
  controls.update()
  renderer.render(scene, camera)
  updateAxisGizmo()
}

function onResize() {
  if (!container.value) return
  const w = container.value.clientWidth
  const h = container.value.clientHeight
  camera.aspect = w / h
  camera.updateProjectionMatrix()
  renderer.setSize(w, h)
}

function loadMesh(data) {
  if (gizmo) gizmo.detach()
  clearOverhangOverlay()

  if (meshPivot) {
    scene.remove(meshPivot)
    if (meshObject) {
      meshObject.geometry.dispose()
      meshObject.material.dispose()
    }
    meshPivot = null
    meshObject = null
  }
  holdMode.value = null

  if (!data) return

  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(data.positions, 3))
  geometry.setAttribute('normal', new THREE.BufferAttribute(data.normals, 3))
  geometry.setIndex(new THREE.BufferAttribute(data.indices, 1))

  geometry.computeBoundingBox()
  const box = geometry.boundingBox

  const material = new THREE.MeshPhongMaterial({
    color: MESH_COLOR,
    emissive: MESH_EMISSIVE,
    flatShading: true,
    shininess: MESH_SHININESS,
    side: THREE.DoubleSide,
  })

  meshObject = new THREE.Mesh(geometry, material)

  // No pivot offset — backend owns all positioning via the transform matrix.
  // Auto place_on_plate after load puts the mesh on the grid.
  meshPivot = new THREE.Group()
  meshPivot.add(meshObject)
  scene.add(meshPivot)

  const size = new THREE.Vector3()
  box.getSize(size)
  const maxDim = Math.max(size.x, size.y, size.z)
  const dist = maxDim * 2
  camera.position.set(dist * 0.7, dist * 0.7, dist * 0.7)
  controls.target.set(0, 0, 0)
  controls.update()

  gizmo.attach(meshObject)
  gizmo.setMode('translate')
}

function clearOverhangOverlay() {
  if (overhangMesh) {
    if (meshObject) meshObject.remove(overhangMesh)
    overhangMesh.geometry.dispose()
    overhangMesh = null
  }
}

function updateOverhangOverlay(triangleIndices) {
  clearOverhangOverlay()
  if (!triangleIndices || triangleIndices.length === 0 || !meshObject) return

  const origGeo = meshObject.geometry
  const origPos = origGeo.getAttribute('position').array
  const origIdx = origGeo.getIndex().array

  const positions = new Float32Array(triangleIndices.length * 9)
  for (let i = 0; i < triangleIndices.length; i++) {
    const t = triangleIndices[i]
    for (let v = 0; v < 3; v++) {
      const srcIdx = origIdx[t * 3 + v]
      positions[i * 9 + v * 3 + 0] = origPos[srcIdx * 3 + 0]
      positions[i * 9 + v * 3 + 1] = origPos[srcIdx * 3 + 1]
      positions[i * 9 + v * 3 + 2] = origPos[srcIdx * 3 + 2]
    }
  }

  const geo = new THREE.BufferGeometry()
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3))

  overhangMesh = new THREE.Mesh(geo, overhangMaterial)
  overhangMesh.visible = props.overhangVisible
  meshObject.add(overhangMesh)
}

function applyTransformMatrix(matrix16) {
  if (!meshObject || !matrix16) return
  const m = new THREE.Matrix4()
  m.fromArray(matrix16)
  const pos = new THREE.Vector3()
  const quat = new THREE.Quaternion()
  const scl = new THREE.Vector3()
  m.decompose(pos, quat, scl)
  meshObject.position.copy(pos)
  meshObject.quaternion.copy(quat)
  meshObject.scale.copy(scl)
}

// --- Camera controls ---

function snapToView(direction) {
  const target = controls.target.clone()
  const dist = camera.position.distanceTo(target)
  const views = {
    front: new THREE.Vector3(0, 0, dist), back: new THREE.Vector3(0, 0, -dist),
    right: new THREE.Vector3(dist, 0, 0), left: new THREE.Vector3(-dist, 0, 0),
    top: new THREE.Vector3(0, dist, 0.001), bottom: new THREE.Vector3(0, -dist, 0.001),
  }
  const offset = views[direction]
  if (!offset) return
  camera.position.copy(target).add(offset)
  camera.lookAt(target)
  controls.update()
}

function orbitStep(axis, angleDeg) {
  const angleRad = (angleDeg * Math.PI) / 180
  const offset = camera.position.clone().sub(controls.target)
  if (axis === 'y') {
    offset.applyAxisAngle(new THREE.Vector3(0, 1, 0), angleRad)
  } else if (axis === 'x') {
    const right = new THREE.Vector3().crossVectors(camera.up, offset).normalize()
    offset.applyAxisAngle(right, angleRad)
  }
  camera.position.copy(controls.target).add(offset)
  camera.lookAt(controls.target)
  controls.update()
}

function makePerspectiveCamera() {
  return new THREE.PerspectiveCamera(CAMERA_FOV, container.value.clientWidth / container.value.clientHeight, CAMERA_NEAR, CAMERA_FAR)
}

function swapCamera(newCam) {
  camera = newCam
  controls.object = camera
  if (gizmo) gizmo.camera = camera
  controls.update()
}

function toggleProjection() {
  if (camera.isPerspectiveCamera) {
    const dist = camera.position.distanceTo(controls.target)
    const aspect = container.value.clientWidth / container.value.clientHeight
    const halfH = dist * Math.tan((CAMERA_FOV / 2) * Math.PI / 180)
    const ortho = new THREE.OrthographicCamera(-halfH * aspect, halfH * aspect, halfH, -halfH, CAMERA_NEAR, CAMERA_FAR)
    ortho.position.copy(camera.position)
    ortho.quaternion.copy(camera.quaternion)
    ortho.zoom = 1
    swapCamera(ortho)
  } else {
    const persp = makePerspectiveCamera()
    persp.position.copy(camera.position)
    persp.quaternion.copy(camera.quaternion)
    swapCamera(persp)
  }
}

function resetCamera() {
  camera.position.set(200, 200, 200)
  controls.target.set(0, 0, 0)
  if (!camera.isPerspectiveCamera) {
    const persp = makePerspectiveCamera()
    persp.position.copy(camera.position)
    swapCamera(persp)
  } else { controls.update() }
}

function zoomStep(factor) {
  const offset = camera.position.clone().sub(controls.target)
  offset.multiplyScalar(factor)
  camera.position.copy(controls.target).add(offset)
  controls.update()
}

// --- Input handlers ---

function onKeyDown(e) {
  if (e.repeat) return

  // Hold-and-drag transform (G/R/S) — only if gizmo isn't being dragged
  if (!holdMode.value && !gizmoDragging && meshObject) {
    if (e.code === 'KeyG') { startHoldTransform('translate'); e.preventDefault(); return }
    if (e.code === 'KeyR') { startHoldTransform('rotate'); e.preventDefault(); return }
    if (e.code === 'KeyS') { startHoldTransform('scale'); e.preventDefault(); return }
  }

  if (holdMode.value && e.code === 'Escape') {
    cancelHoldTransform()
    e.preventDefault()
    return
  }

  switch (e.code) {
    case 'Numpad1': snapToView((e.ctrlKey || e.metaKey) ? 'back' : 'front'); break
    case 'Numpad3': snapToView((e.ctrlKey || e.metaKey) ? 'left' : 'right'); break
    case 'Numpad7': snapToView((e.ctrlKey || e.metaKey) ? 'bottom' : 'top'); break
    case 'Numpad5': toggleProjection(); break
    case 'Numpad0': resetCamera(); break
    case 'Numpad4': orbitStep('y', ORBIT_STEP_DEG); break
    case 'Numpad6': orbitStep('y', -ORBIT_STEP_DEG); break
    case 'Numpad8': orbitStep('x', -ORBIT_STEP_DEG); break
    case 'Numpad2': orbitStep('x', ORBIT_STEP_DEG); break
    case 'Numpad9': snapToView('back'); break
    case 'NumpadAdd': zoomStep(ZOOM_STEP_FACTOR); break
    case 'NumpadSubtract': zoomStep(1 / ZOOM_STEP_FACTOR); break
    default: return
  }
  e.preventDefault()
}

function onKeyUp(e) {
  if (!holdMode.value) return
  if ((holdMode.value === 'translate' && e.code === 'KeyG') ||
      (holdMode.value === 'rotate' && e.code === 'KeyR') ||
      (holdMode.value === 'scale' && e.code === 'KeyS')) {
    endHoldTransform()
    e.preventDefault()
  }
}

function onDragOver() { dragOver.value = true }
function onDragLeave() { dragOver.value = false }

function onDrop(e) {
  dragOver.value = false
  const files = e.dataTransfer?.files
  if (!files || files.length === 0) return
  const file = files[0]
  if (file.name.toLowerCase().endsWith('.stl') && window.electronIPC) {
    const path = window.electronIPC.getPathForFile(file)
    if (path) emit('drop-file', path)
  }
}

watch(() => props.meshData, (data) => { loadMesh(data) })
watch(() => props.transformMatrix, (matrix) => { applyTransformMatrix(matrix) })
watch(() => props.overhangIndices, (indices) => { updateOverhangOverlay(indices) })
watch(() => props.overhangVisible, (visible) => { if (overhangMesh) overhangMesh.visible = visible })

onMounted(() => { init(); container.value.focus() })

onBeforeUnmount(() => {
  if (holdMode.value) cancelHoldTransform()
  if (animFrameId) cancelAnimationFrame(animFrameId)
  window.removeEventListener('resize', onResize)
  clearOverhangOverlay()
  if (overhangMaterial) overhangMaterial.dispose()
  if (meshObject) {
    meshObject.geometry.dispose()
    meshObject.material.dispose()
  }
  if (gizmo) { gizmo.detach(); gizmo.dispose() }
  if (renderer) renderer.dispose()
  if (axisRenderer) axisRenderer.dispose()
  if (controls) controls.dispose()
})
</script>

<style scoped>
.viewport { flex: 1; outline: none; overflow: hidden; position: relative; }

.drop-overlay {
  position: absolute; inset: 0; display: flex; align-items: center; justify-content: center;
  background: rgba(74, 122, 74, 0.3); border: 2px dashed #7ddf7d; color: #7ddf7d;
  font-size: 18px; font-weight: 600; z-index: 10; pointer-events: none;
}

.mode-indicator {
  position: absolute; top: 8px; left: 8px; padding: 4px 10px;
  background: rgba(34, 34, 58, 0.9); border: 1px solid #555; border-radius: 4px;
  color: #eee; font-size: 12px; font-weight: 600; pointer-events: none; z-index: 5;
}
</style>
