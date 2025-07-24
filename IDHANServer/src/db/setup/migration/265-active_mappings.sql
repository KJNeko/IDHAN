CREATE VIEW active_tag_mappings AS
(
SELECT *
FROM tag_mappings
WHERE EXISTS (SELECT 1 FROM file_info WHERE file_info.record_id = tag_mappings.record_id AND file_info.mime_id IS NOT NULL) );

CREATE VIEW active_tag_mappings_virtual AS
(
SELECT *
FROM tag_mappings_virtual
WHERE EXISTS (SELECT 1 FROM file_info WHERE file_info.record_id = tag_mappings_virtual.record_id AND file_info.mime_id IS NOT NULL) );

