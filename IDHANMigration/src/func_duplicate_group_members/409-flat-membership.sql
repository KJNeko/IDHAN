CREATE OR REPLACE FUNCTION duplicate_group_members(king INTEGER) RETURNS SETOF INTEGER AS
$$
SELECT record_id
FROM duplicate_members
WHERE king_id = king
UNION ALL
SELECT king
WHERE NOT EXISTS (SELECT 1 FROM duplicate_members WHERE record_id = king);
$$ LANGUAGE sql STABLE;
