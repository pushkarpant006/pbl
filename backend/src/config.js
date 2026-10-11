// CONCEPT: Backend configuration
// WHY SAFEBOX NEEDS IT: Tool paths and resource limits should be defined in one place.
// HOW IT WORKS: Resolve project paths, allow a binary-directory override, and export limits.

const path = require("node:path");

const projectRoot = path.resolve(__dirname, "..", "..");
const binaryDirectory = process.env.SAFEBOX_BIN_DIR
  ? path.resolve(process.env.SAFEBOX_BIN_DIR)
  : path.join(projectRoot, "build");

module.exports = {
  projectRoot,
  binaryDirectory,
  uploadDirectory: path.join(__dirname, "..", "uploads"),
  publicDirectory: path.join(__dirname, "..", "public"),
  port: 3000,
  uploadLimitBytes: 10 * 1024 * 1024,
  outputLimitBytes: 2 * 1024 * 1024,
  minimumIntervalMs: 50,
  maximumIntervalMs: 5000,
  minimumLimitMb: 16,
  maximumLimitMb: 1024,
  minimumTimeoutSeconds: 1,
  maximumTimeoutSeconds: 60,
  minimumQuantumMs: 50,
  maximumQuantumMs: 1000,
  maximumProcesses: 8,
  maximumBurstMs: 5000
};
