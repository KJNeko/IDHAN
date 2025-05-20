CREATE VIEW tags_combined AS
(
SELECT tag_id, CASE WHEN namespace_text = '' THEN subtag_text ELSE namespace_text || ':' || subtag_text END as tag_text
FROM tags
         NATURAL JOIN tag_namespaces
         NATURAL JOIN tag_subtags
    );