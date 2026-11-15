// CONCEPT: Analysis history API.
// WHY SAFEBOX NEEDS IT: Users need access to runs after the upload request ends.
// HOW IT WORKS: Routes return parameterized database reads.
// A list summarizes saved runs and a detail route returns their related records.

const express = require("express");
const { listHistory, getHistoryDetail } = require("../services/analysisHistoryService");

const router = express.Router();

// Return the latest saved analyses for the current demo account.
async function readHistory(request, response, next) {
  try {
    const history = await listHistory();
    response.json({ history });
  } catch (error) {
    next(error);
  }
}

// Return one saved analysis and reject malformed IDs before querying PostgreSQL.
async function readHistoryDetail(request, response, next) {
  const sessionId = Number(request.params.sessionId);
  if (!Number.isSafeInteger(sessionId) || sessionId <= 0) {
    response.status(400).json({ error: "Session ID must be a positive integer." });
    return;
  }
  try {
    const detail = await getHistoryDetail(sessionId);
    if (detail === null) {
      response.status(404).json({ error: "Saved analysis was not found." });
      return;
    }
    response.json(detail);
  } catch (error) {
    next(error);
  }
}

router.get("/api/history", readHistory);
router.get("/api/history/:sessionId", readHistoryDetail);

module.exports = router;
