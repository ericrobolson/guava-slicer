module.exports = {
  process(src) {
    const code = src.replace(
      /export\s*\{([^}]+)\}/g,
      (_, names) => `module.exports = {${names}}`
    )
    return { code }
  },
}
