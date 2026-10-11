// CONCEPT: Local SafeBox web server
// WHY SAFEBOX NEEDS IT: A local page needs a small API that can invoke the existing C++ tools.
// HOW IT WORKS: Prepare private upload storage, serve the static page, mount API routes, and bind localhost.

const express = require("express");
const fs = require("node:fs");
const multer = require("multer");
const config = require("./config");
const analyzeRoute = require("./routes/analyzeRoute");
const modulesRoute = require("./routes/modulesRoute");
const schedulerRoute = require("./routes/schedulerRoute");

const app = express();

// Create private upload storage so uploaded executable files are not public.
function prepareUploadDirectory() {
  fs.mkdirSync(config.uploadDirectory, { recursive: true, mode: 0o700 });
  fs.chmodSync(config.uploadDirectory, 0o700);
}

// Convert request and tool failures into clear JSON errors.
function handleError(error, request, response, next) {
  if (response.headersSent) {
    next(error);
    return;
  }
  if (error instanceof multer.MulterError) {
    const statusCode = error.code === "LIMIT_FILE_SIZE" ? 413 : 400;
    response.status(statusCode).json({ error: error.message });
    return;
  }
  const statusCode = error.statusCode || 400;
  response.status(statusCode).json({ error: error.message || "Request failed." });
}

// Register static content, JSON parsing, API handlers, and the final error response.
function configureApplication() {
  prepareUploadDirectory();
  app.disable("x-powered-by");
  app.use(express.json({ limit: "32kb" }));
  app.use(express.static(config.publicDirectory));
  app.use(modulesRoute);
  app.use(analyzeRoute);
  app.use(schedulerRoute);
  app.use(handleError);
}

// Start the HTTP listener on the loopback interface only.
function startServer() {
  configureApplication();
  app.listen(config.port, "127.0.0.1", function reportServerStart() {
    console.log(`SafeBox web UI is available at http://localhost:${config.port}`);
  });
}

startServer();
