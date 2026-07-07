export function basename(filePath) {
  if (!filePath) return ''
  return filePath.split('/').pop().split('\\').pop()
}

export function stripExtension(filePath) {
  if (!filePath) return ''
  const name = basename(filePath)
  const dot = name.lastIndexOf('.')
  return dot > 0 ? name.substring(0, dot) : name
}
