-- Limit comparison candidates to the same resource before normalizing query parameters.
-- Original URLs, including expiring signatures, remain unchanged.
CREATE INDEX urls_comparison_base_idx ON urls ((split_part(split_part(url, '#', 1), '?', 1)));
