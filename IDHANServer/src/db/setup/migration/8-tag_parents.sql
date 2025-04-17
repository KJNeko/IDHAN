CREATE TABLE tag_parents
(
	parent_id INTEGER REFERENCES tags (tag_id),
	child_id  INTEGER REFERENCES tags (tag_id),
	domain_id SMALLINT REFERENCES tag_domains (tag_domain_id),
	UNIQUE (parent_id, child_id, domain_id)
) PARTITION BY LIST (domain_id);