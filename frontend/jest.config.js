module.exports = {
  testMatch: ['**/*.test.js'],
  transform: {
    '\\.js$': './jest-esm-transform.js',
  },
  transformIgnorePatterns: ['/node_modules/'],
}
