CREATE TABLE session_keys
(
	issuer_id   INTEGER REFERENCES access_keys (access_key_id) NOT NULL,
	session_key BYTEA UNIQUE                                   NOT NULL,
	valid_until TIMESTAMP WITHOUT TIME ZONE                    NOT NULL
);
