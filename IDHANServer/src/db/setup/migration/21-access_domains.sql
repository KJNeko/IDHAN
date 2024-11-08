CREATE TABLE access_domains
(
	access_key_id INTEGER    NOT NULL REFERENCES access_keys (access_key_id),
	tag_domains   INTEGER [] NOT NULL,
	file_domains  INTEGER [] NOT NULL
);