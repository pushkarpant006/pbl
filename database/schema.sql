CREATE TABLE IF NOT EXISTS user_roles (
    role_id SERIAL PRIMARY KEY,
    role_name VARCHAR(20) UNIQUE NOT NULL,
    description TEXT
);

CREATE TABLE IF NOT EXISTS users (
    user_id SERIAL PRIMARY KEY,
    username VARCHAR(50),
    email VARCHAR(100),
    password_hash VARCHAR(255),
    role VARCHAR(20) REFERENCES user_roles(role_name),
    created_at TIMESTAMP,
    last_login TIMESTAMP
);

CREATE TABLE IF NOT EXISTS applications (
    application_id SERIAL PRIMARY KEY,
    user_id INT REFERENCES users(user_id),
    name VARCHAR(150),
    version VARCHAR(50),
    file_path TEXT,
    file_hash VARCHAR(64),
    file_size BIGINT,
    uploaded_at TIMESTAMP,
    description TEXT,
    is_trusted BOOLEAN DEFAULT FALSE
);

CREATE TABLE IF NOT EXISTS execution_sessions (
    session_id SERIAL PRIMARY KEY,
    application_id INT REFERENCES applications(application_id),
    user_id INT REFERENCES users(user_id),
    started_at TIMESTAMP,
    ended_at TIMESTAMP,
    status VARCHAR(20) CHECK (status IN ('RUNNING', 'PAUSED', 'STOPPED')),
    exit_code INT,
    risk_score INT,
    threat_level VARCHAR(20) CHECK (threat_level IN ('SAFE', 'LOW', 'MEDIUM', 'HIGH')),
    notes TEXT
);

CREATE TABLE IF NOT EXISTS processes (
    process_id SERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    parent_process_id BIGINT REFERENCES processes(process_id),
    pid INT,
    process_name VARCHAR(255),
    command_line TEXT,
    user_name VARCHAR(100),
    status VARCHAR(20) CHECK (status IN ('RUNNING', 'EXITED', 'KILLED')),
    start_time TIMESTAMP,
    end_time TIMESTAMP,
    cpu_usage DOUBLE PRECISION,
    memory_usage BIGINT
);

CREATE TABLE IF NOT EXISTS file_events (
    file_event_id BIGSERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    process_id BIGINT REFERENCES processes(process_id),
    event_type VARCHAR(20) CHECK (event_type IN ('CREATE', 'READ', 'WRITE', 'DELETE', 'RENAME')),
    file_path TEXT,
    file_name TEXT,
    file_size BIGINT,
    success BOOLEAN,
    event_time TIMESTAMP,
    details TEXT
);

CREATE TABLE IF NOT EXISTS network_events (
    network_event_id BIGSERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    process_id BIGINT REFERENCES processes(process_id),
    dest_ip INET,
    dest_port INT,
    protocol VARCHAR(10),
    connection_type VARCHAR(20) CHECK (connection_type IN ('TCP', 'UDP', 'ICMP')),
    bytes_sent BIGINT,
    bytes_received BIGINT,
    event_time TIMESTAMP,
    success BOOLEAN,
    details TEXT
);

CREATE TABLE IF NOT EXISTS syscall_events (
    syscall_event_id BIGSERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    process_id BIGINT REFERENCES processes(process_id),
    syscall_name VARCHAR(100),
    arguments TEXT,
    return_value BIGINT,
    success BOOLEAN,
    event_time TIMESTAMP
);

CREATE TABLE IF NOT EXISTS rule_categories (
    category_id SERIAL PRIMARY KEY,
    category_name VARCHAR(100) UNIQUE NOT NULL,
    description TEXT
);

CREATE TABLE IF NOT EXISTS threat_rules (
    rule_id SERIAL PRIMARY KEY,
    rule_name VARCHAR(150),
    category VARCHAR(50) REFERENCES rule_categories(category_name),
    description TEXT,
    condition_type VARCHAR(30) CHECK (condition_type IN ('COUNT', 'THRESHOLD', 'PATTERN')),
    metric VARCHAR(50),
    operator VARCHAR(10),
    threshold_value INT,
    severity_score INT,
    is_active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP
);

CREATE TABLE IF NOT EXISTS threat_scores (
    score_id SERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    rule_id INT REFERENCES threat_rules(rule_id),
    event_id BIGINT,
    score INT,
    matched_value TEXT,
    created_at TIMESTAMP
);

CREATE TABLE IF NOT EXISTS alerts (
    alert_id SERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    rule_id INT REFERENCES threat_rules(rule_id),
    event_id BIGINT,
    alert_time TIMESTAMP,
    alert_level VARCHAR(20) CHECK (alert_level IN ('LOW', 'MEDIUM', 'HIGH')),
    message TEXT,
    action_taken VARCHAR(50) CHECK (action_taken IN ('NONE', 'PAUSED', 'TERMINATED')),
    resolved BOOLEAN DEFAULT FALSE,
    resolved_at TIMESTAMP
);

CREATE TABLE IF NOT EXISTS actions_taken (
    action_id SERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    action_type VARCHAR(50) CHECK (action_type IN ('PAUSE', 'RESUME', 'TERMINATE', 'ALLOW')),
    action_time TIMESTAMP,
    performed_by VARCHAR(50),
    details TEXT
);

CREATE TABLE IF NOT EXISTS reports (
    report_id SERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    generated_at TIMESTAMP,
    summary TEXT,
    file_path TEXT
);

CREATE TABLE IF NOT EXISTS sensitive_paths (
    path_id SERIAL PRIMARY KEY,
    path TEXT,
    description TEXT,
    sensitivity_level VARCHAR(20) CHECK (sensitivity_level IN ('LOW', 'MEDIUM', 'HIGH')),
    is_active BOOLEAN DEFAULT TRUE
);

CREATE TABLE IF NOT EXISTS execution_environments (
    env_id SERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    os_type VARCHAR(50),
    container_id VARCHAR(100),
    cpu_limit INT,
    memory_limit BIGINT,
    disk_limit BIGINT,
    network_mode VARCHAR(50),
    started_at TIMESTAMP
);

CREATE TABLE IF NOT EXISTS system_info (
    info_id SERIAL PRIMARY KEY,
    session_id INT REFERENCES execution_sessions(session_id),
    os_name VARCHAR(100),
    os_version VARCHAR(100),
    kernel_version VARCHAR(100),
    host_name VARCHAR(100),
    collected_at TIMESTAMP,
    details JSONB
);
