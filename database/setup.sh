#!/usr/bin/env bash
set -euo pipefail

DB_ADMIN_USER="${DB_ADMIN_USER:-postgres}"
DB_USER="${DB_USER:-safebox_app}"
DB_NAME="${DB_NAME:-safebox}"
DB_PASSWORD="${DB_PASSWORD:?Set DB_PASSWORD to a strong local database password.}"
DB_HOST="${DB_HOST:-localhost}"
export DB_NAME DB_PASSWORD DB_USER

PGPASSWORD="${DB_ADMIN_PASSWORD:-}" psql -h "$DB_HOST" -U "$DB_ADMIN_USER" -d postgres \
  -v ON_ERROR_STOP=1 \
  -v db_user="$DB_USER" \
  -v db_name="$DB_NAME" <<'SQL'
\getenv db_password DB_PASSWORD
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L', :'db_user', :'db_password')
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = :'db_user')
\gexec
SELECT format('ALTER ROLE %I WITH LOGIN PASSWORD %L', :'db_user', :'db_password')
\gexec
SELECT format('CREATE DATABASE %I OWNER %I', :'db_name', :'db_user')
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = :'db_name')
\gexec
SELECT format('ALTER DATABASE %I OWNER TO %I', :'db_name', :'db_user')
\gexec
SQL

PGPASSWORD="$DB_PASSWORD" psql -h "$DB_HOST" -U "$DB_USER" -d "$DB_NAME" \
  -v ON_ERROR_STOP=1 -f "$(dirname "$0")/schema.sql"
PGPASSWORD="$DB_PASSWORD" psql -h "$DB_HOST" -U "$DB_USER" -d "$DB_NAME" \
  -v ON_ERROR_STOP=1 -f "$(dirname "$0")/seed.sql"
PGPASSWORD="$DB_PASSWORD" psql -h "$DB_HOST" -U "$DB_USER" -d "$DB_NAME" \
  -v ON_ERROR_STOP=1 <<'SQL'
CREATE INDEX IF NOT EXISTS users_role_idx ON users(role);
CREATE INDEX IF NOT EXISTS applications_user_id_idx ON applications(user_id);
CREATE INDEX IF NOT EXISTS execution_sessions_application_id_idx ON execution_sessions(application_id);
CREATE INDEX IF NOT EXISTS execution_sessions_user_id_idx ON execution_sessions(user_id);
CREATE INDEX IF NOT EXISTS processes_session_id_idx ON processes(session_id);
CREATE INDEX IF NOT EXISTS processes_parent_process_id_idx ON processes(parent_process_id);
CREATE INDEX IF NOT EXISTS file_events_session_id_idx ON file_events(session_id);
CREATE INDEX IF NOT EXISTS file_events_process_id_idx ON file_events(process_id);
CREATE INDEX IF NOT EXISTS network_events_session_id_idx ON network_events(session_id);
CREATE INDEX IF NOT EXISTS network_events_process_id_idx ON network_events(process_id);
CREATE INDEX IF NOT EXISTS syscall_events_session_id_idx ON syscall_events(session_id);
CREATE INDEX IF NOT EXISTS syscall_events_process_id_idx ON syscall_events(process_id);
CREATE INDEX IF NOT EXISTS threat_rules_category_idx ON threat_rules(category);
CREATE INDEX IF NOT EXISTS threat_scores_session_id_idx ON threat_scores(session_id);
CREATE INDEX IF NOT EXISTS threat_scores_rule_id_idx ON threat_scores(rule_id);
CREATE INDEX IF NOT EXISTS alerts_session_id_idx ON alerts(session_id);
CREATE INDEX IF NOT EXISTS alerts_rule_id_idx ON alerts(rule_id);
CREATE INDEX IF NOT EXISTS actions_taken_session_id_idx ON actions_taken(session_id);
CREATE INDEX IF NOT EXISTS reports_session_id_idx ON reports(session_id);
CREATE INDEX IF NOT EXISTS execution_environments_session_id_idx ON execution_environments(session_id);
CREATE INDEX IF NOT EXISTS system_info_session_id_idx ON system_info(session_id);
SQL
