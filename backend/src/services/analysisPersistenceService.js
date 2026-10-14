
const os = require("node:os");
const pool = require("./databaseService");
const { calculateRiskScore } = require("./riskService");

const findingRules = {
  TIMEOUT: "TIMEOUT",
  TOO_MANY_PROCESSES: "TOO_MANY_PROCESSES"
};

function databaseProcessStatus(process, actions) {
  if (process.final_state !== "TERMINATED") return "RUNNING";
  if (Number(process.exit_status) >= 128 ||
      actions.some(function killedGroup(action) {
        return action.action === "TERMINATE" && action.pid === 0;
      })) return "KILLED";
  return "EXITED";
}

function findDatabaseProcessId(processIds, pid) {
  for (let index = 0; index < processIds.length; index += 1) {
    if (processIds[index].pid === pid) return processIds[index].processId;
  }
  return null;
}

async function insertProcesses(client, sessionId, summary, actions, startedAt) {
  const pending = summary.processes.slice();
  const processIds = [];
  while (pending.length > 0) {
    let insertedThisPass = 0;
    for (let index = 0; index < pending.length;) {
      const process = pending[index];
      const parentProcessId = process.parent_pid === 0
        ? null
        : findDatabaseProcessId(processIds, process.parent_pid);
      const parentIsInSummary = summary.processes.some(function hasParent(candidate) {
        return candidate.pid === process.parent_pid;
      });
      if (process.parent_pid !== 0 && parentIsInSummary && parentProcessId === null) {
        index += 1;
        continue;
      }
      const status = databaseProcessStatus(process, actions);
      const result = await client.query(
        `INSERT INTO processes
          (session_id, parent_process_id, pid, process_name, command_line, user_name,
           status, start_time, end_time, cpu_usage, memory_usage)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
         RETURNING process_id`,
        [sessionId, parentProcessId, process.pid, process.name, process.name,
          os.userInfo().username, status, startedAt,
          status === "RUNNING" ? null : new Date(), process.cpu_ms, null]
      );
      processIds.push({ pid: process.pid, processId: result.rows[0].process_id });
      pending.splice(index, 1);
      insertedThisPass += 1;
    }
    if (insertedThisPass === 0) {
      throw new Error("Could not order processes by parent before database insertion.");
    }
  }
  return processIds;
}

async function readMatchedRules(client, findings) {
  const ruleNames = [];
  for (let index = 0; index < findings.length; index += 1) {
    const ruleName = findingRules[findings[index].type];
    if (ruleName && !ruleNames.includes(ruleName)) ruleNames.push(ruleName);
  }
  if (ruleNames.length === 0) return [];
  const result = await client.query(
    "SELECT rule_id, rule_name, severity_score FROM threat_rules WHERE is_active = TRUE AND rule_name = ANY($1)",
    [ruleNames]
  );
  return result.rows;
}

async function insertAlerts(client, sessionId, findings, matchedRules, actions) {
  const alerts = [];
  for (let index = 0; index < matchedRules.length; index += 1) {
    const rule = matchedRules[index];
    let finding = null;
    for (let findingIndex = 0; findingIndex < findings.length; findingIndex += 1) {
      if (findingRules[findings[findingIndex].type] === rule.rule_name) {
        finding = findings[findingIndex];
      }
    }
    if (finding === null) continue;
    const action = actions.length > 0 ? actions[actions.length - 1].action : "NONE";
    const actionTaken = action === "PAUSE" ? "PAUSED" : action === "TERMINATE" ? "TERMINATED" : "NONE";
    const result = await client.query(
      `INSERT INTO alerts
        (session_id, rule_id, alert_time, alert_level, message, action_taken)
       VALUES ($1, $2, CURRENT_TIMESTAMP, $3, $4, $5)
       RETURNING alert_id`,
      [sessionId, rule.rule_id, finding.severity, finding.message, actionTaken]
    );
    alerts.push({ alertId: result.rows[0].alert_id, ruleId: rule.rule_id });
  }
  return alerts;
}

async function insertActions(client, sessionId, actions, findings, startedAt) {
  const rows = actions.length > 0 ? actions : findings.length > 0
    ? [{ action: "NONE", pid: 0, time_ms: 0 }]
    : [];
  for (let index = 0; index < rows.length; index += 1) {
    const actionType = rows[index].action === "PAUSE" ? "PAUSE"
      : rows[index].action === "TERMINATE" ? "TERMINATE" : "ALLOW";
    await client.query(
      `INSERT INTO actions_taken (session_id, action_type, action_time, performed_by, details)
       VALUES ($1, $2, $3, $4, $5)`,
      [sessionId, actionType, new Date(startedAt.getTime() + rows[index].time_ms),
        "SafeBox analyzer", `pid=${rows[index].pid}`]
    );
  }
}

async function insertHostInformation(client, sessionId) {
  await client.query(
    `INSERT INTO execution_environments
      (session_id, os_type, cpu_limit, memory_limit, disk_limit, network_mode, started_at)
     VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP)`,
    [sessionId, "Linux", 15, 512 * 1024 * 1024, 10 * 1024 * 1024, "host"]
  );
  await client.query(
    `INSERT INTO system_info
      (session_id, os_name, os_version, kernel_version, host_name, collected_at, details)
     VALUES ($1, $2, $3, $4, $5, CURRENT_TIMESTAMP, $6::jsonb)`,
    [sessionId, os.type(), os.release(), os.release(), os.hostname(),
      JSON.stringify({ platform: os.platform(), architecture: os.arch(), node: process.version })]
  );
}

async function insertThreatScores(client, sessionId, matchedRules, findings) {
  for (let index = 0; index < matchedRules.length; index += 1) {
    const rule = matchedRules[index];
    let matchedValue = "";
    for (let findingIndex = 0; findingIndex < findings.length; findingIndex += 1) {
      if (findingRules[findings[findingIndex].type] === rule.rule_name) {
        matchedValue = findings[findingIndex].message;
      }
    }
    await client.query(
      `INSERT INTO threat_scores (session_id, rule_id, score, matched_value, created_at)
       VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)`,
      [sessionId, rule.rule_id, rule.severity_score, matchedValue]
    );
  }
}

async function readSavedAnalysis(client, sessionId) {
  const session = await client.query(
    "SELECT * FROM execution_sessions WHERE session_id = $1", [sessionId]);
  const processes = await client.query(
    `SELECT process_record.*,
            COALESCE(parent_process.pid, 0) AS parent_pid
     FROM processes process_record
     LEFT JOIN processes parent_process
       ON parent_process.process_id = process_record.parent_process_id
     WHERE process_record.session_id = $1
     ORDER BY process_record.process_id`,
    [sessionId]);
  const alerts = await client.query(
    "SELECT * FROM alerts WHERE session_id = $1 ORDER BY alert_id", [sessionId]);
  const actions = await client.query(
    "SELECT * FROM actions_taken WHERE session_id = $1 ORDER BY action_id", [sessionId]);
  return {
    session: session.rows[0],
    processes: processes.rows,
    alerts: alerts.rows,
    actions: actions.rows
  };
}

async function persistAnalysis(fileInfo, summary, actions) {
  const client = await pool.connect();
  const startedAt = new Date(Date.now() - summary.duration_ms);
  try {
    await client.query("BEGIN");
    const user = await client.query(
      "SELECT user_id FROM users WHERE username = $1 ORDER BY user_id LIMIT 1", ["demo"]);
    if (user.rows.length === 0) throw new Error("Demo database user is missing; run database/setup.sh.");
    const application = await client.query(
      `INSERT INTO applications (user_id, name, file_hash, file_size, file_path, uploaded_at)
       VALUES ($1, $2, $3, $4, $5, CURRENT_TIMESTAMP) RETURNING application_id`,
      [user.rows[0].user_id, fileInfo.name, fileInfo.sha256, fileInfo.sizeBytes, null]
    );
    const findings = summary.findings || [];
    const matchedRules = await readMatchedRules(client, findings);
    const risk = calculateRiskScore(matchedRules);
    const root = summary.processes.find(function isRoot(process) {
      return process.parent_pid === 0;
    });
    const sessionResult = await client.query(
      `INSERT INTO execution_sessions
        (application_id, user_id, started_at, ended_at, status, exit_code,
         risk_score, threat_level, notes)
       VALUES ($1, $2, $3, CURRENT_TIMESTAMP, 'STOPPED', $4, $5, $6, $7)
       RETURNING session_id`,
      [application.rows[0].application_id, user.rows[0].user_id, startedAt,
        root ? root.exit_status : null, risk.riskScore, risk.threatLevel, summary.verdict]
    );
    const sessionId = sessionResult.rows[0].session_id;
    await insertProcesses(client, sessionId, summary, actions, startedAt);
    await insertHostInformation(client, sessionId);
    await insertAlerts(client, sessionId, findings, matchedRules, actions);
    await insertActions(client, sessionId, actions, findings, startedAt);
    await insertThreatScores(client, sessionId, matchedRules, findings);
    await client.query(
      `INSERT INTO reports (session_id, generated_at, summary)
       VALUES ($1, CURRENT_TIMESTAMP, $2)`,
      [sessionId, JSON.stringify(summary)]
    );
    await client.query("COMMIT");
    const saved = await readSavedAnalysis(client, sessionId);
    for (let index = 0; index < saved.processes.length; index += 1) {
      const analyzedProcess = summary.processes.find(function matchProcess(process) {
        return process.pid === saved.processes[index].pid;
      });
      saved.processes[index].exit_status = analyzedProcess ? analyzedProcess.exit_status : null;
    }
    return { ...saved, risk_score: risk.riskScore, threat_level: risk.threatLevel };
  } catch (error) {
    await client.query("ROLLBACK");
    throw error;
  } finally {
    client.release();
  }
}

module.exports = { persistAnalysis };
