// CONCEPT: Analysis history query tests.
// WHY SAFEBOX NEEDS IT: Saved runs and their related records must be readable later.
// HOW IT WORKS: A fake database pool supplies the rows returned by history queries.
// The test verifies session scoping and inclusion of the saved memory timeline.

const assert = require("node:assert/strict");
const test = require("node:test");
const pool = require("../src/services/databaseService");
const originalQuery = pool.query;
const { listHistory, getHistoryDetail } = require("../src/services/analysisHistoryService");

test("loads a run with its saved process and memory records", async function () {
  const statements = [];
  pool.query = async function queryMock(sql, parameters) {
    statements.push({ sql, parameters });
    if (sql.includes("COUNT(DISTINCT process.process_id)")) {
      return { rows: [{ session_id: 12, program_name: "sample", memory_sample_count: 2 }] };
    }
    if (sql.includes("SELECT session.session_id") && sql.includes("report.summary::jsonb AS summary")) {
      return {
        rows: [{
          session_id: 12,
          started_at: "2025-01-01T00:00:00.000Z",
          status: "STOPPED",
          program_name: "sample",
          file_hash: "abc",
          file_size: 123,
          summary: { memory_analysis: { peak_rss_kb: 200 } }
        }]
      };
    }
    if (sql.includes("FROM processes process")) {
      return { rows: [{ pid: 101, process_name: "sample" }] };
    }
    if (sql.includes("FROM memory_samples sample")) {
      return { rows: [{ pid: 101, elapsed_ms: 10, rss_kb: 200 }] };
    }
    return { rows: [] };
  };
  try {
    const history = await listHistory();
    assert.equal(history[0].program_name, "sample");
    assert.deepEqual(statements[0].parameters, ["demo", 50]);

    const detail = await getHistoryDetail(12);
    assert.equal(detail.session.session_id, 12);
    assert.equal(detail.application.name, "sample");
    assert.deepEqual(detail.processes, [{ pid: 101, process_name: "sample" }]);
    assert.deepEqual(detail.memory_samples, [{ pid: 101, elapsed_ms: 10, rss_kb: 200 }]);
    assert.equal(statements.slice(1).every(function scopesToSession(statement) {
      return statement.parameters[0] === 12;
    }), true);
    assert.deepEqual(detail.file_events, []);
    assert.deepEqual(detail.network_events, []);
    assert.deepEqual(detail.syscall_events, []);
  } finally {
    pool.query = originalQuery;
  }
});
