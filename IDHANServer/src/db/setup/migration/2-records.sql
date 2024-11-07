CREATE TABLE records
(
	record_id      SERIAL PRIMARY KEY,
	sha256         BYTEA UNIQUE                NOT NULL,
	creation_time  TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
	file_domain_id SMALLINT REFERENCES file_domains (file_domain_id)
);