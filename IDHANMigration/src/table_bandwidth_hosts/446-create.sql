CREATE TABLE bandwidth_hosts
(
    host_id SERIAL PRIMARY KEY,
    host    text NOT NULL UNIQUE
);

INSERT INTO bandwidth_hosts (host)
SELECT DISTINCT host
FROM bandwidth_logs
ORDER BY host;
