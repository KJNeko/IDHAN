-- $1: URLs; $2: matching domains. Each domain has already been inserted into url_domains.
INSERT INTO urls (url, url_domain_id)
SELECT pairs.url, ud.url_domain_id
FROM UNNEST($1, $2) AS pairs(url, domain)
         JOIN url_domains ud ON ud.url_domain = pairs.domain
ON CONFLICT (url) DO NOTHING;
