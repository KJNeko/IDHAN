CREATE VIEW display_mappings AS
(
SELECT record_id, tag_id, domain_id
FROM active_filtered_mappings );