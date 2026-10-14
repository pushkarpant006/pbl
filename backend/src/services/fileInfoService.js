
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");

function isElfFile(contents) {
  return contents.length >= 4 &&
    contents[0] === 0x7f &&
    contents[1] === 0x45 &&
    contents[2] === 0x4c &&
    contents[3] === 0x46;
}

function isShebangFile(contents) {
  return contents.length >= 3 &&
    contents[0] === 0x23 &&
    contents[1] === 0x21 &&
    contents[2] !== 0x0a &&
    contents[2] !== 0x0d;
}

function inspectFile(filePath, originalName) {
  const contents = fs.readFileSync(filePath);
  let fileType = "";

  if (isElfFile(contents)) {
    fileType = "ELF";
  } else if (isShebangFile(contents)) {
    fileType = "script";
  } else {
    throw new Error("Unsupported file type. Upload a Linux ELF executable or a script with a shebang; Windows .exe files will not run in WSL.");
  }

  const safeName = path.basename(originalName.replace(/\\/g, "/"));
  return {
    name: safeName || "uploaded-program",
    sizeBytes: contents.length,
    sha256: crypto.createHash("sha256").update(contents).digest("hex"),
    type: fileType
  };
}

module.exports = { inspectFile };
