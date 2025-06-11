CREATE TABLE tag_domains
(
	tag_domain_id SMALLSERIAL PRIMARY KEY,
	domain_name   TEXT UNIQUE NOT NULL
);