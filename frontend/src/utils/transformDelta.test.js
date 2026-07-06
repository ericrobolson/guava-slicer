const { computeRotationDelta, computeTranslationDelta, computeScaleDelta } = require('./transformDelta.js')

describe('computeRotationDelta', () => {
  const IDENTITY = { x: 0, y: 0, z: 0, w: 1 }

  test('returns null for no rotation', () => {
    expect(computeRotationDelta(IDENTITY, IDENTITY)).toBeNull()
  })

  test('returns null for identical quaternions', () => {
    const q = { x: 0.5, y: 0.5, z: 0.5, w: 0.5 }
    expect(computeRotationDelta(q, q)).toBeNull()
  })

  test('90° around Y axis produces correct quaternion', () => {
    const angle = Math.PI / 2
    const q90y = { x: 0, y: Math.sin(angle / 2), z: 0, w: Math.cos(angle / 2) }
    const result = computeRotationDelta(IDENTITY, q90y)
    expect(result).not.toBeNull()
    expect(result.quaternion).toHaveLength(4)
    expect(result.quaternion[0]).toBeCloseTo(0, 5)
    expect(result.quaternion[1]).toBeCloseTo(Math.sin(angle / 2), 5)
    expect(result.quaternion[2]).toBeCloseTo(0, 5)
    expect(result.quaternion[3]).toBeCloseTo(Math.cos(angle / 2), 5)
  })

  test('delta from rotated base to further rotation', () => {
    const a1 = Math.PI / 4
    const base = { x: 0, y: Math.sin(a1 / 2), z: 0, w: Math.cos(a1 / 2) }
    const a2 = Math.PI / 2
    const current = { x: 0, y: Math.sin(a2 / 2), z: 0, w: Math.cos(a2 / 2) }
    const result = computeRotationDelta(base, current)
    expect(result).not.toBeNull()
    // delta should be a 45° rotation around Y (90° - 45°)
    const expectedAngle = Math.PI / 4
    const expectedW = Math.cos(expectedAngle / 2)
    const expectedY = Math.sin(expectedAngle / 2)
    expect(result.quaternion[1]).toBeCloseTo(expectedY, 4)
    expect(result.quaternion[3]).toBeCloseTo(expectedW, 4)
  })

  test('very small rotation returns null', () => {
    const tiny = { x: 0, y: 1e-8, z: 0, w: 1 }
    expect(computeRotationDelta(IDENTITY, tiny)).toBeNull()
  })
})

describe('computeTranslationDelta', () => {
  const ORIGIN = { x: 0, y: 0, z: 0 }

  test('returns null for no movement', () => {
    expect(computeTranslationDelta(ORIGIN, ORIGIN)).toBeNull()
  })

  test('returns null for sub-threshold movement', () => {
    expect(computeTranslationDelta(ORIGIN, { x: 1e-5, y: 0, z: 0 })).toBeNull()
  })

  test('returns correct delta for translation', () => {
    const result = computeTranslationDelta(ORIGIN, { x: 10, y: -5, z: 3 })
    expect(result).not.toBeNull()
    expect(result.delta).toEqual([10, -5, 3])
  })

  test('returns correct delta from non-origin base', () => {
    const result = computeTranslationDelta({ x: 5, y: 5, z: 5 }, { x: 15, y: 0, z: 8 })
    expect(result).not.toBeNull()
    expect(result.delta[0]).toBeCloseTo(10, 5)
    expect(result.delta[1]).toBeCloseTo(-5, 5)
    expect(result.delta[2]).toBeCloseTo(3, 5)
  })
})

describe('computeScaleDelta', () => {
  const UNIT = { x: 1, y: 1, z: 1 }

  test('returns null for no change', () => {
    expect(computeScaleDelta(UNIT, UNIT)).toBeNull()
  })

  test('returns null for sub-threshold change', () => {
    expect(computeScaleDelta(UNIT, { x: 1.0000001, y: 1, z: 1 })).toBeNull()
  })

  test('uniform 2x scale', () => {
    const result = computeScaleDelta(UNIT, { x: 2, y: 2, z: 2 })
    expect(result).not.toBeNull()
    expect(result.factor).toEqual([2, 2, 2])
  })

  test('non-uniform scale from non-unit base', () => {
    const result = computeScaleDelta({ x: 2, y: 3, z: 4 }, { x: 4, y: 6, z: 8 })
    expect(result).not.toBeNull()
    expect(result.factor[0]).toBeCloseTo(2, 5)
    expect(result.factor[1]).toBeCloseTo(2, 5)
    expect(result.factor[2]).toBeCloseTo(2, 5)
  })

  test('scale down', () => {
    const result = computeScaleDelta(UNIT, { x: 0.5, y: 0.5, z: 0.5 })
    expect(result).not.toBeNull()
    expect(result.factor).toEqual([0.5, 0.5, 0.5])
  })
})
