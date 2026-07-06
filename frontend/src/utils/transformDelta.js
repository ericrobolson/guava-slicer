/// @file transformDelta.js
/// @brief Pure functions for computing transform deltas from base/current state.

const ROTATION_THRESHOLD = 1e-6
const TRANSLATION_THRESHOLD_SQ = 1e-8
const SCALE_THRESHOLD = 1e-6

/**
 * @brief Compute the world-space rotation delta quaternion.
 * @param {{x:number, y:number, z:number, w:number}} baseQuat
 * @param {{x:number, y:number, z:number, w:number}} currentQuat
 * @returns {{quaternion: number[]}|null} Quaternion [x,y,z,w] or null if no rotation.
 */
function computeRotationDelta(baseQuat, currentQuat) {
  const bx = -baseQuat.x, by = -baseQuat.y, bz = -baseQuat.z, bw = baseQuat.w

  const dx = currentQuat.w * bx + currentQuat.x * bw + currentQuat.y * bz - currentQuat.z * by
  const dy = currentQuat.w * by - currentQuat.x * bz + currentQuat.y * bw + currentQuat.z * bx
  const dz = currentQuat.w * bz + currentQuat.x * by - currentQuat.y * bx + currentQuat.z * bw
  const dw = currentQuat.w * bw - currentQuat.x * bx - currentQuat.y * by - currentQuat.z * bz

  const angle = 2 * Math.acos(Math.min(1, Math.abs(dw)))
  if (angle < ROTATION_THRESHOLD) return null

  return { quaternion: [dx, dy, dz, dw] }
}

/**
 * @brief Compute translation delta between base and current position.
 * @param {{x:number, y:number, z:number}} basePos
 * @param {{x:number, y:number, z:number}} currentPos
 * @returns {{delta: number[]}|null} [x,y,z] delta or null if no movement.
 */
function computeTranslationDelta(basePos, currentPos) {
  const dx = currentPos.x - basePos.x
  const dy = currentPos.y - basePos.y
  const dz = currentPos.z - basePos.z
  if (dx * dx + dy * dy + dz * dz < TRANSLATION_THRESHOLD_SQ) return null
  return { delta: [dx, dy, dz] }
}

/**
 * @brief Compute scale factor between base and current scale.
 * @param {{x:number, y:number, z:number}} baseScale
 * @param {{x:number, y:number, z:number}} currentScale
 * @returns {{factor: number[]}|null} [x,y,z] scale factors or null if no change.
 */
function computeScaleDelta(baseScale, currentScale) {
  const fx = currentScale.x / baseScale.x
  const fy = currentScale.y / baseScale.y
  const fz = currentScale.z / baseScale.z
  if (Math.abs(fx - 1) < SCALE_THRESHOLD && Math.abs(fy - 1) < SCALE_THRESHOLD && Math.abs(fz - 1) < SCALE_THRESHOLD) return null
  return { factor: [fx, fy, fz] }
}

export { computeRotationDelta, computeTranslationDelta, computeScaleDelta }
