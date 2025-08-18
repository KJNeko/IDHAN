CREATE TABLE url_domains
(
    url_domain_id SERIAL PRIMARY KEY,
    url_domain TEXT NOT NULL UNIQUE
);
