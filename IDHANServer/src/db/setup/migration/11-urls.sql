CREATE TABLE urls
(
	url_id        SERIAL PRIMARY KEY,
	url_domain_id INTEGER     NOT NULL REFERENCES url_domains (url_domain_id),
	url           TEXT UNIQUE NOT NULL
);