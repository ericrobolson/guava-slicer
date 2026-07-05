export function basename(filePath) {
  if (!filePath) return ''
  return filePath.split('/').pop().split('\\').pop()
}
