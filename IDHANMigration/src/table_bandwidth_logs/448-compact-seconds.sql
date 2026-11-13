-- Keep one received-byte total for each exact host and Unix second.
WITH previous AS (
    DELETE FROM bandwidth_logs
        RETURNING host_id, timestamp, bytes_received)
INSERT
INTO bandwidth_logs (host_id, timestamp, bytes_received)
SELECT host_id, timestamp, sum(bytes_received)::bigint
FROM previous
GROUP BY host_id, timestamp;

ALTER TABLE bandwidth_logs
    ADD PRIMARY KEY (host_id, timestamp);
