-- URL comparisons now search url directly, e.g. url LIKE 'https://gelbooru.com/index.php%'.
DROP INDEX urls_query_fields_idx;
DROP INDEX urls_comparison_base_idx;

CREATE INDEX urls_url_pattern_idx ON urls (url text_pattern_ops);
