CREATE OR REPLACE FUNCTION duplicate_king(record INTEGER) RETURNS INTEGER AS
$$
SELECT COALESCE((SELECT king_id FROM duplicate_members WHERE record_id = record), record);
$$ LANGUAGE sql STABLE;
