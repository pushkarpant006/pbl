// CONCEPT: Saved analysis history.
// WHY SAFEBOX NEEDS IT: Database records must be readable after a run ends.
// HOW IT WORKS: List saved sessions for the demo user.
// Detail reads return each session's metrics, processes, samples, alerts, and actions.

const pool = require("./databaseService");

const HISTORY_LIMIT = 50;
const HISTORY_USERNAME = "demo";

// Return the latest saved runs without loading their full report documents.
async function listHistory() {
  const result = await pool.query(
    `SELECT session.session_id, application.name AS program_name,
            session.started_at, session.ended_at, session.status, session.exit_code,
            session.risk_score, session.threat_level,
            COUNT(DISTINCT process.process_id)::int AS process_count,
            COUNT(DISTINCT sample.memory_sample_id)::int AS memory_sample_count,
            report.summary::jsonb -> 'memory_analysis' AS memory_analysis
     FROM execution_sessions session
     JOIN applications application
       ON application.application_id = session.application_id
     LEFT JOIN processes process ON process.session_id = session.session_id
     LEFT JOIN memory_samples sample ON sample.session_id = session.session_id
     LEFT JOIN LATERAL (
       SELECT report_record.summary
       FROM reports report_record
       WHERE report_record.session_id = session.session_id
       ORDER BY report_record.generated_at DESC, report_record.report_id DESC
       LIMIT 1
     ) report ON TRUE
     WHERE session.user_id = (
       SELECT user_id FROM users WHERE username = $1 ORDER BY user_id LIMIT 1
     )
     GROUP BY session.session_id, application.name, report.summary
     ORDER BY session.started_at DESC, session.session_id DESC
     LIMIT $2`,
    [HISTORY_USERNAME, HISTORY_LIMIT]
  );
  return result.rows;
}

// Load the persisted report and related rows for one saved run.
async function getHistoryDetail(sessionId) {
  const runResult = await pool.query(
    `SELECT session.session_id, session.started_at, session.ended_at,
            session.status, session.exit_code, session.risk_score, session.threat_level,
            session.notes, application.name AS program_name,
            application.file_hash, application.file_size,
            report.summary::jsonb AS summary
     FROM execution_sessions session
     JOIN applications application
       ON application.application_id = session.application_id
     LEFT JOIN LATERAL (
       SELECT report_record.summary
       FROM reports report_record
       WHERE report_record.session_id = session.session_id
       ORDER BY report_record.generated_at DESC, report_record.report_id DESC
       LIMIT 1
     ) report ON TRUE
     WHERE session.session_id = $1
       AND session.user_id = (
         SELECT user_id FROM users WHERE username = $2 ORDER BY user_id LIMIT 1
       )`,
    [sessionId, HISTORY_USERNAME]
  );
  if (runResult.rows.length === 0) return null;

  const queries = await Promise.all([
    pool.query(
      `SELECT process.process_id, process.pid, process.parent_process_id,
              COALESCE(parent.pid, 0) AS parent_pid, process.process_name,
              process.status, process.cpu_usage, process.memory_usage,
              process.start_time, process.end_time
       FROM processes process
       LEFT JOIN processes parent ON parent.process_id = process.parent_process_id
       WHERE process.session_id = $1 ORDER BY process.process_id`,
      [sessionId]),
    pool.query(
      `SELECT sample.memory_sample_id, process.pid, sample.elapsed_ms,
              sample.rss_kb, sample.vsize_kb, sample.verdict, sample.reason,
              sample.sampled_at
       FROM memory_samples sample
       JOIN processes process ON process.process_id = sample.process_id
       WHERE sample.session_id = $1
       ORDER BY sample.elapsed_ms, sample.memory_sample_id`,
      [sessionId]),
    pool.query("SELECT * FROM alerts WHERE session_id = $1 ORDER BY alert_id", [sessionId]),
    pool.query("SELECT * FROM actions_taken WHERE session_id = $1 ORDER BY action_id", [sessionId]),
    pool.query("SELECT * FROM file_events WHERE session_id = $1 ORDER BY file_event_id", [sessionId]),
    pool.query(
      "SELECT * FROM network_events WHERE session_id = $1 ORDER BY network_event_id",
      [sessionId]),
    pool.query(
      "SELECT * FROM syscall_events WHERE session_id = $1 ORDER BY syscall_event_id",
      [sessionId]),
    pool.query(
      "SELECT * FROM execution_environments WHERE session_id = $1 ORDER BY env_id",
      [sessionId]),
    pool.query("SELECT * FROM system_info WHERE session_id = $1 ORDER BY info_id", [sessionId]),
    pool.query("SELECT * FROM threat_scores WHERE session_id = $1 ORDER BY score_id", [sessionId])
  ]);
  const detail = runResult.rows[0];
  return {
    session: {
      session_id: detail.session_id,
      started_at: detail.started_at,
      ended_at: detail.ended_at,
      status: detail.status,
      exit_code: detail.exit_code,
      risk_score: detail.risk_score,
      threat_level: detail.threat_level,
      notes: detail.notes
    },
    application: {
      name: detail.program_name,
      file_hash: detail.file_hash,
      file_size: detail.file_size
    },
    summary: detail.summary,
    processes: queries[0].rows,
    memory_samples: queries[1].rows,
    alerts: queries[2].rows,
    actions: queries[3].rows,
    file_events: queries[4].rows,
    network_events: queries[5].rows,
    syscall_events: queries[6].rows,
    execution_environments: queries[7].rows,
    system_info: queries[8].rows,
    threat_scores: queries[9].rows
  };
}

module.exports = { listHistory, getHistoryDetail };
