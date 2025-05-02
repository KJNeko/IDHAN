CREATE MATERIALIZED VIEW active_mappings AS
(
SELECT DISTINCT tm.record_id, tm.tag_id, tm.domain_id
FROM active_records ar
         JOIN tag_mappings tm ON ar.record_id = tm.record_id);