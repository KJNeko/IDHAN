CREATE TABLE bandwidth_logs
(
    host           text   NOT NULL,
    timestamp      bigint NOT NULL,
    bytes_received bigint NOT NULL CHECK (bytes_received >= 0)
);

CREATE INDEX bandwidth_logs_timestamp_host_idx ON bandwidth_logs (timestamp, host);
