-- A transport failure (DNS, connect, timeout) never produces an HTTP status, and both the
-- error list endpoint and its 'none' status filter already read it as nullable.
ALTER TABLE download_session_errors
    ALTER COLUMN status DROP NOT NULL;
