-- Query fields are searched anywhere in url, e.g. url LIKE '%id=13004%'.
CREATE INDEX urls_url_trigram_idx ON urls USING gin (url gin_trgm_ops);
