// CONCEPT: Persistence transaction tests.
// WHY SAFEBOX NEEDS IT: A successful analysis must save its related records together.
// HOW IT WORKS: A fake PostgreSQL client records statements and returns expected rows.
// The test checks the run, processes, memory samples, and related decision records.

const assert = require("node:assert/strict");
const test = require("node:test");
const pool = require("../src/services/databaseService");
const originalConnect = pool.connect;
const { persistAnalysis } = require("../src/services/analysisPersistenceService");

test("persists an analysis and its memory samples in one transaction", async function () {
  const statements = [];
  pool.connect = async function connectMock() {
    return {
      async query(sql, parameters = []) {
        statements.push({ sql, parameters });
        if (sql === "SELECT user_id FROM users WHERE username = $1 ORDER BY user_id LIMIT 1") {
          return { rows: [{ user_id: 9 }] };
        }
        if (sql.includes("FROM threat_rules")) {
          return { rows: [{ rule_id: 7, rule_name: "TIMEOUT", severity_score: 70 }] };
        }
        if (sql.startsWith("INSERT INTO applications")) {
          return { rows: [{ application_id: 20 }] };
        }
        if (sql.startsWith("INSERT INTO execution_sessions")) {
          return { rows: [{ session_id: 30 }] };
        }
        if (sql.startsWith("INSERT INTO processes")) {
          return { rows: [{ process_id: 40 }] };
        }
        if (sql.startsWith("INSERT INTO alerts")) {
          return { rows: [{ alert_id: 50 }] };
        }
        if (sql.startsWith("SELECT * FROM execution_sessions")) {
          return { rows: [{ session_id: 30 }] };
        }
        if (sql.startsWith("SELECT process_record.*")) {
          return {
            rows: [{
              process_id: 40,
              pid: 101,
              parent_pid: 0,
              status: "KILLED",
              memory_usage: 204800
            }]
          };
        }
        if (sql.startsWith("SELECT memory_sample.*")) {
          return {
            rows: [{
              memory_sample_id: 1,
              pid: 101,
              elapsed_ms: 10,
              rss_kb: 200,
              vsize_kb: 400,
              verdict: "SAFE",
              reason: ""
            }]
          };
        }
        return { rows: [] };
      },
      release() {}
    };
  };
  try {
    const saved = await persistAnalysis(
      { name: "sample", sha256: "abc", sizeBytes: 123 },
      {
        duration_ms: 30,
        verdict: "TIMEOUT",
        findings: [{ type: "TIMEOUT", severity: "HIGH", message: "Timed out" }],
        processes: [{
          pid: 101,
          parent_pid: 0,
          name: "sample",
          final_state: "TERMINATED",
          exit_status: 143,
          cpu_ms: 10
        }]
      },
      [{ action: "TERMINATE", pid: 0, time_ms: 30 }],
      [{ pid: 101, time_ms: 10, rss_kb: 200, vsize_kb: 400, verdict: "SAFE", reason: "" }]
    );

    const insertedTables = statements
      .filter(function isInsert(statement) {
        return statement.sql.startsWith("INSERT INTO ");
      })
      .map(function getTable(statement) {
        return statement.sql.match(/^INSERT INTO (\w+)/)[1];
      });
    for (const table of [
      "applications", "execution_sessions", "processes", "memory_samples",
      "execution_environments", "system_info", "alerts", "actions_taken",
      "threat_scores", "reports"
    ]) {
      assert.ok(insertedTables.includes(table), `${table} should be saved`);
    }

    const memoryInsert = statements.find(function findMemoryInsert(statement) {
      return statement.sql.startsWith("INSERT INTO memory_samples");
    });
    assert.deepEqual(memoryInsert.parameters.slice(0, 7), [30, 40, 10, 200, 400, "SAFE", ""]);
    assert.ok(memoryInsert.parameters[7] instanceof Date);
    assert.equal(saved.memory_samples[0].rss_kb, 200);
    assert.equal(saved.processes[0].memory_usage, 204800);
    assert.equal(saved.risk_score, 70);
    assert.equal(saved.threat_level, "HIGH");
    assert.ok(statements.some(function committed(statement) {
      return statement.sql === "COMMIT";
    }));
  } finally {
    pool.connect = originalConnect;
  }
});
