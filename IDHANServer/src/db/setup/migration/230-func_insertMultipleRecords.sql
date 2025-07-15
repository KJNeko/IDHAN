CREATE OR REPLACE FUNCTION insertmultiplerecords(VARIADIC bytes bytea[])
    RETURNS TABLE
            (
                record_id INTEGER
            )
AS
$$
BEGIN
    LOCK TABLE records IN EXCLUSIVE MODE;

    RETURN QUERY
        WITH inserted_records AS (
            SELECT UNNEST(bytes) AS byte_data
            )
            INSERT INTO records (sha256)
                SELECT byte_data
                FROM inserted_records
                ON CONFLICT (sha256) DO UPDATE SET sha256 = records.sha256
                RETURNING records.record_id;
END;
$$ LANGUAGE plpgsql;