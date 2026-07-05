<template>
  <div
    ref="container"
    class="viewport"
    tabindex="0"
    @keydown="onKeyDown"
    @dragover.prevent="onDragOver"
    @dragleave="onDragLeave"
    @drop.prevent="onDrop"
  >
    <div v-if="dragOver" class="drop-overlay">Drop STL file here</div>
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'

const props = defineProps({
  meshData: { type: Object, default: null },
})

const emit = defineEmits(['drop-file'])

const container = ref(null)
const dragOver = ref(false)

let scene, camera, renderer, controls, meshObject, gridHelper, buildPlate
let axisScene, axisCamera, axisRenderer
let animFrameId = null

const ORBIT_STEP_DEG = 15
const BUILD_PLATE_SIZE = 256
const BUILD_PLATE_DIVISIONS = 16
const GRID_COLOR_CENTER = 0x444466
const GRID_COLOR_LINES = 0x333344
const BUILD_PLATE_COLOR = 0x2a2a3a
const MESH_COLOR = 0x6699cc
const MESH_EMISSIVE = 0x112233
const BACKGROUND_COLOR = 0x1a1a2e
const AMBIENT_LIGHT_INTENSITY = 0.6
const DIR_LIGHT_INTENSITY = 0.8
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

function initAxisGizmo() {
  axisScene = new THREE.Scene()

  axisCamera = new THREE.OrthographicCamera(-1.2, 1.2, 1.2, -1.2, 0.1, 10)
  axisCamera.position.set(0, 0, 3)

  const xArrow = new THREE.ArrowHelper(
    new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 0, 0),
    AXIS_LENGTH, AXIS_COLOR_X, AXIS_HEAD_LENGTH, AXIS_HEAD_WIDTH
  )
  const yArrow = new THREE.ArrowHelper(
    new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 0),
    AXIS_LENGTH, AXIS_COLOR_Y, AXIS_HEAD_LENGTH, AXIS_HEAD_WIDTH
  )
  const zArrow = new THREE.ArrowHelper(
    new THREE.Vector3(0, 0, 1), new THREE.Vector3(0, 0, 0),
    AXIS_LENGTH, AXIS_COLOR_Z, AXIS_HEAD_LENGTH, AXIS_HEAD_WIDTH
  )

  axisScene.add(xArrow)
  axisScene.add(yArrow)
  axisScene.add(zArrow)

  addAxisLabel('X', new THREE.Vector3(1.05, 0, 0), AXIS_COLOR_X)
  addAxisLabel('Y', new THREE.Vector3(0, 1.05, 0), AXIS_COLOR_Y)
  addAxisLabel('Z', new THREE.Vector3(0, 0, 1.05), AXIS_COLOR_Z)

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
  canvas.width = 32
  canvas.height = 32
  const ctx = canvas.getContext('2d')
  ctx.font = `bold ${AXIS_LABEL_SIZE}px sans-serif`
  ctx.textAlign = 'center'
  ctx.textBaseline = 'middle'
  ctx.fillStyle = '#' + color.toString(16).padStart(6, '0')
  ctx.fillText(text, 16, 16)

  const texture = new THREE.CanvasTexture(canvas)
  const material = new THREE.SpriteMaterial({ map: texture, depthTest: false })
  const sprite = new THREE.Sprite(material)
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

function init() {
  scene = new THREE.Scene()
  scene.background = new THREE.Color(BACKGROUND_COLOR)

  camera = new THREE.PerspectiveCamera(
    CAMERA_FOV,
    container.value.clientWidth / container.value.clientHeight,
    CAMERA_NEAR,
    CAMERA_FAR
  )
  camera.position.set(200, 200, 200)

  renderer = new THREE.WebGLRenderer({ antialias: true })
  renderer.setSize(container.value.clientWidth, container.value.clientHeight)
  renderer.setPixelRatio(window.devicePixelRatio)
  container.value.appendChild(renderer.domElement)

  controls = new OrbitControls(camera, renderer.domElement)
  controls.enableDamping = true
  controls.dampingFactor = 0.1

  const ambient = new THREE.AmbientLight(0xffffff, AMBIENT_LIGHT_INTENSITY)
  scene.add(ambient)

  const dirLight = new THREE.DirectionalLight(0xffffff, DIR_LIGHT_INTENSITY)
  dirLight.position.set(1, 2, 1.5)
  scene.add(dirLight)

  gridHelper = new THREE.GridHelper(BUILD_PLATE_SIZE, BUILD_PLATE_DIVISIONS, GRID_COLOR_CENTER, GRID_COLOR_LINES)
  scene.add(gridHelper)

  const plateGeo = new THREE.PlaneGeometry(BUILD_PLATE_SIZE, BUILD_PLATE_SIZE)
  const plateMat = new THREE.MeshBasicMaterial({
    color: BUILD_PLATE_COLOR,
    transparent: true,
    opacity: 0.5,
    side: THREE.DoubleSide,
  })
  buildPlate = new THREE.Mesh(plateGeo, plateMat)
  buildPlate.rotation.x = -Math.PI / 2
  buildPlate.position.y = -0.01
  scene.add(buildPlate)

  initAxisGizmo()

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
  if (meshObject) {
    scene.remove(meshObject)
    meshObject.geometry.dispose()
    meshObject.material.dispose()
    meshObject = null
  }

  if (!data) return

  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(data.positions, 3))
  geometry.setAttribute('normal', new THREE.BufferAttribute(data.normals, 3))
  geometry.setIndex(new THREE.BufferAttribute(data.indices, 1))

  const material = new THREE.MeshPhongMaterial({
    color: MESH_COLOR,
    emissive: MESH_EMISSIVE,
    flatShading: true,
    side: THREE.DoubleSide,
  })

  meshObject = new THREE.Mesh(geometry, material)
  scene.add(meshObject)

  geometry.computeBoundingBox()
  const box = geometry.boundingBox
  const center = new THREE.Vector3()
  box.getCenter(center)
  meshObject.position.set(-center.x, -box.min.y, -center.z)

  const size = new THREE.Vector3()
  box.getSize(size)
  const maxDim = Math.max(size.x, size.y, size.z)
  const dist = maxDim * 2
  camera.position.set(dist * 0.7, dist * 0.7, dist * 0.7)
  controls.target.set(0, size.y / 2 - box.min.y - center.y, 0)
  controls.update()
}

function snapToView(direction) {
  const target = controls.target.clone()
  const dist = camera.position.distanceTo(target)

  const views = {
    front:  new THREE.Vector3(0, 0, dist),
    back:   new THREE.Vector3(0, 0, -dist),
    right:  new THREE.Vector3(dist, 0, 0),
    left:   new THREE.Vector3(-dist, 0, 0),
    top:    new THREE.Vector3(0, dist, 0.001),
    bottom: new THREE.Vector3(0, -dist, 0.001),
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
  return new THREE.PerspectiveCamera(
    CAMERA_FOV,
    container.value.clientWidth / container.value.clientHeight,
    CAMERA_NEAR,
    CAMERA_FAR
  )
}

function swapCamera(newCam) {
  camera = newCam
  controls.object = camera
  controls.update()
}

function toggleProjection() {
  if (camera.isPerspectiveCamera) {
    const dist = camera.position.distanceTo(controls.target)
    const aspect = container.value.clientWidth / container.value.clientHeight
    const halfH = dist * Math.tan((CAMERA_FOV / 2) * Math.PI / 180)

    const ortho = new THREE.OrthographicCamera(
      -halfH * aspect, halfH * aspect, halfH, -halfH, CAMERA_NEAR, CAMERA_FAR
    )
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
  } else {
    controls.update()
  }
}

function zoomStep(factor) {
  const offset = camera.position.clone().sub(controls.target)
  offset.multiplyScalar(factor)
  camera.position.copy(controls.target).add(offset)
  controls.update()
}

function onKeyDown(e) {
  const ctrl = e.ctrlKey || e.metaKey

  switch (e.code) {
    case 'Numpad1': snapToView(ctrl ? 'back' : 'front'); break
    case 'Numpad3': snapToView(ctrl ? 'left' : 'right'); break
    case 'Numpad7': snapToView(ctrl ? 'bottom' : 'top'); break
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

function onDragOver() {
  dragOver.value = true
}

function onDragLeave() {
  dragOver.value = false
}

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

watch(() => props.meshData, (data) => {
  loadMesh(data)
})

onMounted(() => {
  init()
  container.value.focus()
})

onBeforeUnmount(() => {
  if (animFrameId) cancelAnimationFrame(animFrameId)
  window.removeEventListener('resize', onResize)
  if (renderer) renderer.dispose()
  if (axisRenderer) axisRenderer.dispose()
  if (controls) controls.dispose()
})
</script>

<style scoped>
.viewport {
  flex: 1;
  outline: none;
  overflow: hidden;
  position: relative;
}

.drop-overlay {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(74, 122, 74, 0.3);
  border: 2px dashed #7ddf7d;
  color: #7ddf7d;
  font-size: 18px;
  font-weight: 600;
  z-index: 10;
  pointer-events: none;
}
</style>
