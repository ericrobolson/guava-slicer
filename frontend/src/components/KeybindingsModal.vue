<template>
  <div v-if="visible" class="modal-backdrop" @click.self="$emit('close')">
    <div class="modal">
      <div class="modal-header">
        <h3>Keybindings</h3>
        <button class="close-btn" @click="$emit('close')">&times;</button>
      </div>
      <div class="modal-columns">
        <div v-for="section in sections" :key="section.title" class="section">
          <h4>{{ section.title }}</h4>
          <div v-for="binding in section.bindings" :key="binding.key" class="binding-row">
            <kbd>{{ binding.key }}</kbd>
            <span>{{ binding.action }}</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({
  visible: { type: Boolean, default: false },
})

defineEmits(['close'])

const sections = [
  {
    title: 'Transforms',
    bindings: [
      { key: 'Hold G + Drag', action: 'Grab (translate) model' },
      { key: 'Hold R + Drag', action: 'Rotate model' },
      { key: 'Hold S + Drag', action: 'Scale model' },
      { key: 'Esc', action: 'Cancel active transform' },
    ],
  },
  {
    title: 'Gizmo',
    bindings: [
      { key: 'Click + Drag handle', action: 'Transform along axis' },
      { key: 'G', action: 'Switch gizmo to translate' },
      { key: 'R', action: 'Switch gizmo to rotate' },
      { key: 'S', action: 'Switch gizmo to scale' },
    ],
  },
  {
    title: 'Undo / Redo',
    bindings: [
      { key: 'Cmd+Z', action: 'Undo' },
      { key: 'Cmd+Shift+Z', action: 'Redo' },
      { key: 'Cmd+Y', action: 'Redo (alt)' },
    ],
  },
  {
    title: 'Camera Views',
    bindings: [
      { key: 'Numpad 1', action: 'Front view' },
      { key: 'Ctrl+Numpad 1', action: 'Back view' },
      { key: 'Numpad 3', action: 'Right view' },
      { key: 'Ctrl+Numpad 3', action: 'Left view' },
      { key: 'Numpad 7', action: 'Top view' },
      { key: 'Ctrl+Numpad 7', action: 'Bottom view' },
      { key: 'Numpad 9', action: 'Back view' },
    ],
  },
  {
    title: 'Camera Controls',
    bindings: [
      { key: 'Numpad 5', action: 'Toggle perspective / ortho' },
      { key: 'Numpad 0', action: 'Reset camera' },
      { key: 'Numpad 4 / 6', action: 'Orbit left / right' },
      { key: 'Numpad 8 / 2', action: 'Orbit up / down' },
      { key: 'Numpad + / -', action: 'Zoom in / out' },
      { key: 'Left-click + Drag', action: 'Orbit camera' },
      { key: 'Right-click + Drag', action: 'Pan camera' },
      { key: 'Scroll wheel', action: 'Zoom' },
    ],
  },
  {
    title: 'File',
    bindings: [
      { key: 'Drag & Drop', action: 'Load STL file' },
    ],
  },
]
</script>

<style scoped>
.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 100;
}

.modal {
  background: #1e1e36;
  border: 1px solid #444;
  border-radius: 8px;
  width: 80vw;
  max-height: 80vh;
  overflow-y: auto;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
}

.modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 20px;
  border-bottom: 1px solid #333;
}

.modal-header h3 {
  font-size: 15px;
  font-weight: 600;
  color: #ddd;
  margin: 0;
}

.close-btn {
  background: none;
  border: none;
  color: #888;
  font-size: 20px;
  cursor: pointer;
  padding: 0 4px;
  line-height: 1;
}

.close-btn:hover { color: #ddd; }

.modal-columns {
  padding: 16px 20px 20px;
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 24px;
}

.section h4 {
  font-size: 11px;
  font-weight: 600;
  color: #888;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin: 0 0 8px 0;
  padding-bottom: 4px;
  border-bottom: 1px solid #2a2a4a;
}

.binding-row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 3px 0;
  font-size: 13px;
}

.binding-row span {
  color: #ccc;
}

kbd {
  display: inline-block;
  padding: 2px 6px;
  background: #2a2a4a;
  border: 1px solid #444;
  border-radius: 3px;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 11px;
  color: #ddd;
  white-space: nowrap;
  flex-shrink: 0;
}
</style>
