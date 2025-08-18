CREATE TABLE urls
(
    url_id        SERIAL PRIMARY KEY,
    url           TEXT                                           NOT NULL UNIQUE,
    url_domain_id INTEGER REFERENCES url_domains (url_domain_id) NOT NULL
);