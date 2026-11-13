CREATE INDEX urls_query_fields_idx ON urls USING GIN ((string_to_array(split_part(split_part(url, '#', 1), '?', 2), '&')));
