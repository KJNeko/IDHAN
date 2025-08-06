CREATE INDEX
    tag_mappings_ideal_id ON tag_mappings (ideal_tag_id) WHERE ideal_tag_id IS NOT NULL;
CREATE INDEX
    tag_mappings_tag_id_ideal_null ON tag_mappings (tag_id) WHERE ideal_tag_id IS NULL;


COMMENT ON INDEX tag_mappings_ideal_id IS 'Used for storage counts';
COMMENT ON INDEX tag_mappings_tag_id_ideal_null IS 'Used for storage counts';

CREATE VIEW storage_tag_counts AS
(
SELECT tag_id, COUNT(*) AS count
FROM tag_mappings
GROUP BY tag_id
    );

CREATE VIEW display_tag_counts(tag_id, count) AS
(
SELECT COALESCE(ideal_tag_id, tag_id) AS id, COUNT(*) AS count
FROM tag_mappings
GROUP BY COALESCE(ideal_tag_id, tag_id));

CREATE VIEW tag_counts AS
(
SELECT tag_id, dc.count AS display_count, sc.count AS storage_count
FROM display_tag_counts dc
         JOIN storage_tag_counts sc USING (tag_id));