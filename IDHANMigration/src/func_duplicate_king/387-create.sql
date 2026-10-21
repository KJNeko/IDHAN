-- Resolve the duplicate root; ungrouped records resolve to themselves.
CREATE OR REPLACE FUNCTION duplicate_king(
    record INTEGER
) RETURNS INTEGER AS
$$
SELECT COALESCE((SELECT duplicate_group.king_id
                 FROM duplicate_group_inferiors inferior
                          JOIN duplicate_groups duplicate_group USING (duplicate_id)
                 WHERE inferior.inferior_id = record), record);
$$ LANGUAGE sql STABLE;

-- Return all records resolving to the given king, including the king itself.
CREATE OR REPLACE FUNCTION duplicate_group_members(
    king INTEGER
) RETURNS SETOF INTEGER AS
$$
SELECT king
UNION
SELECT inferior.inferior_id
FROM duplicate_group_inferiors inferior
         JOIN duplicate_groups duplicate_group USING (duplicate_id)
WHERE duplicate_group.king_id = king;
$$ LANGUAGE sql STABLE;
