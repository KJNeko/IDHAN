CREATE TYPE job_type AS ENUM ('cluster_scan');
CREATE TYPE job_status AS ENUM ('PENDING','STARTED', 'FAILED', 'COMPLETED', 'AWAIT_DEPENDENCY');

CREATE TABLE jobs
(
    job_id         UUID PRIMARY KEY            NOT NULL,
    job_type       job_type                    NOT NULL,
    context_data   JSONB                       NOT NULL,
    job_data       JSONB                       NOT NULL,
    job_response   JSONB,
    time_requested TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT now(),
    time_completed TIMESTAMP WITHOUT TIME ZONE,
    job_status     job_status                  NOT NULL DEFAULT 'PENDING'
);