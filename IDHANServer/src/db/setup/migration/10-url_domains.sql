CREATE TABLE url_domains
(
	url_domain_id SERIAL PRIMARY KEY,
	url_domain    TEXT UNIQUE NOT NULL
);