// CONCEPT: Module status endpoint
// WHY SAFEBOX NEEDS IT: The interface must distinguish implemented features from future plans.
// HOW IT WORKS: Return a fixed list whose status is based on what is actually present.

const express = require("express");
const router = express.Router();

const modules = [
  { name: "Round Robin scheduler", status: "built" },
  { name: "Memory tracking", status: "built" },
  { name: "Isolation", status: "not_built" },
  { name: "Deadlock", status: "not_built" },
  { name: "Filesystem", status: "not_built" },
  { name: "Syscall monitor", status: "not_built" },
  { name: "Policy engine", status: "not_built" }
];

// Send the current built and planned module list to the page.
function getModules(request, response) {
  response.json({ modules });
}

router.get("/api/modules", getModules);

module.exports = router;
