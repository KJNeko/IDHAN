ALTER TABLE bandwidth_logs
    ADD COLUMN host_id integer REFERENCES bandwidth_hosts (host_id);

UPDATE bandwidth_logs AS logs
SET host_id = hosts.host_id
FROM bandwidth_hosts AS hosts
WHERE logs.host = hosts.host;

ALTER TABLE bandwidth_logs
    ALTER COLUMN host_id SET NOT NULL,
    DROP COLUMN host;

CREATE INDEX bandwidth_logs_timestamp_host_id_idx ON bandwidth_logs (timestamp, host_id);
