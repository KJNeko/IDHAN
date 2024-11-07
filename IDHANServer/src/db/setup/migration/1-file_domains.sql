CREATE TABLE file_domains
(
	file_domain_id SMALLSERIAL PRIMARY KEY,
	domain_name    TEXT NOT NULL UNIQUE
);