CREATE MATERIALIZED VIEW parented_siblings AS
(
SELECT DISTINCT fs.domain_id, fp.child_id as older_id, fs.younger_id as younger_id
FROM flattened_siblings fs
         JOIN flattened_parents fp ON fp.domain_id = fs.domain_id AND fp.parent_id = fs.older_id
UNION
DISTINCT
SELECT domain_id, older_id, younger_id
FROM flattened_siblings
    );