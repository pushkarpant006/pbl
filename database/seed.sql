INSERT INTO user_roles (role_name, description)
SELECT 'admin', 'SafeBox administrator'
WHERE NOT EXISTS (SELECT 1 FROM user_roles WHERE role_name = 'admin');

INSERT INTO user_roles (role_name, description)
SELECT 'user', 'SafeBox standard user'
WHERE NOT EXISTS (SELECT 1 FROM user_roles WHERE role_name = 'user');

INSERT INTO users (username, email, password_hash, role, created_at)
SELECT 'demo', 'demo@safebox.local', 'demo-account-not-for-authentication',
       'user', CURRENT_TIMESTAMP
WHERE NOT EXISTS (SELECT 1 FROM users WHERE username = 'demo');

INSERT INTO rule_categories (category_name, description)
SELECT 'Resource', 'Resource use and execution limits'
WHERE NOT EXISTS (SELECT 1 FROM rule_categories WHERE category_name = 'Resource');

INSERT INTO rule_categories (category_name, description)
SELECT 'Process', 'Process behavior and process-tree limits'
WHERE NOT EXISTS (SELECT 1 FROM rule_categories WHERE category_name = 'Process');

INSERT INTO threat_rules
    (rule_name, category, description, condition_type, metric, operator,
     threshold_value, severity_score, is_active, created_at)
SELECT 'TOO_MANY_PROCESSES', 'Process', 'Application created more than ten processes.',
       'THRESHOLD', 'process_count', '>', 10, 40, TRUE, CURRENT_TIMESTAMP
WHERE NOT EXISTS (SELECT 1 FROM threat_rules WHERE rule_name = 'TOO_MANY_PROCESSES');

INSERT INTO threat_rules
    (rule_name, category, description, condition_type, metric, operator,
     threshold_value, severity_score, is_active, created_at)
SELECT 'TIMEOUT', 'Resource', 'Application exceeded its analysis timeout.',
       'THRESHOLD', 'duration_seconds', '>', 15, 30, TRUE, CURRENT_TIMESTAMP
WHERE NOT EXISTS (SELECT 1 FROM threat_rules WHERE rule_name = 'TIMEOUT');
