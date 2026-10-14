
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
  minimumTimeoutSeconds: 1,
  maximumTimeoutSeconds: 60,
  minimumQuantumMs: 50,
  maximumQuantumMs: 1000,
  database: {
    connectionString: process.env.DATABASE_URL,
    host: process.env.DB_HOST || "localhost",
    port: Number(process.env.DB_PORT || 5432),
    user: process.env.DB_USER || "safebox_app",
    password: process.env.DB_PASSWORD || "",
    name: process.env.DB_NAME || "safebox"
  }
};
