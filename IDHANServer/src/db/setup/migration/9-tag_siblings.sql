CREATE TABLE tag_siblings
(
	older_id   INTEGER REFERENCES tags (tag_id),
	younger_id INTEGER REFERENCES tags (tag_id),
	domain_id  SMALLINT REFERENCES tag_domains (tag_domain_id),
	UNIQUE (older_id, younger_id, domain_id)
);