#!/usr/bin/env bash
set -euo pipefail

DB_ADMIN_USER="${DB_ADMIN_USER:-postgres}"
DB_USER="${DB_USER:-safebox_app}"
DB_NAME="${DB_NAME:-safebox}"
DB_HOST="${DB_HOST:-localhost}"
export DB_NAME DB_USER

if [[ -z "${DATABASE_URL:-}" ]]; then
  ENV_FILE="$(dirname "$0")/../backend/.env"
  if [[ -f "$ENV_FILE" ]]; then
    while IFS= read -r env_line || [[ -n "$env_line" ]]; do
      if [[ "$env_line" =~ ^[[:space:]]*(export[[:space:]]+)?DATABASE_URL[[:space:]]*=[[:space:]]*(.*)$ ]]; then
        DATABASE_URL="${BASH_REMATCH[2]}"
        DATABASE_URL="${DATABASE_URL%$'\r'}"
        DATABASE_URL="${DATABASE_URL#"${DATABASE_URL%%[![:space:]]*}"}"
        DATABASE_URL="${DATABASE_URL%"${DATABASE_URL##*[![:space:]]}"}"
        if [[ "${DATABASE_URL:0:1}" == '"' && "${DATABASE_URL: -1}" == '"' ]] ||
           [[ "${DATABASE_URL:0:1}" == "'" && "${DATABASE_URL: -1}" == "'" ]]; then
          DATABASE_URL="${DATABASE_URL:1:${#DATABASE_URL}-2}"
        fi
        export DATABASE_URL
        break
      fi
    done < "$ENV_FILE"
  fi
fi

if [[ -z "${DATABASE_URL:-}" ]]; then
  DB_PASSWORD="${DB_PASSWORD:?Set DATABASE_URL for Neon or DB_PASSWORD for local PostgreSQL.}"
  export DB_PASSWORD

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
  DB_CONNECTION="host=$DB_HOST port=${DB_PORT:-5432} user=$DB_USER dbname=$DB_NAME"
  export PGPASSWORD="$DB_PASSWORD"
else
  DB_CONNECTION="$DATABASE_URL"
fi

psql "$DB_CONNECTION" -v ON_ERROR_STOP=1 -f "$(dirname "$0")/schema.sql"
psql "$DB_CONNECTION" -v ON_ERROR_STOP=1 -f "$(dirname "$0")/seed.sql"
psql "$DB_CONNECTION" -v ON_ERROR_STOP=1 <<'SQL'
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
