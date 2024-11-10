CREATE TABLE hydrus_keys
(
	access_key_id INTEGER NOT NULL REFERENCES access_keys (access_key_id),
	permissions   JSON    NOT NULL
);