CREATE OR REPLACE FUNCTION insertmultiplerecords(VARIADIC bytes bytea[])
    RETURNS TABLE
            (
                record_id INTEGER
            )
AS
$$
BEGIN
    WITH inserted_records AS (SELECT UNNEST(bytes) AS byte_data)
    INSERT
    INTO records (sha256)
    SELECT byte_data
    FROM inserted_records
    ON CONFLICT DO NOTHING;

    RETURN QUERY WITH inserted_records AS (SELECT UNNEST(bytes) AS byte_data)
                 SELECT r.record_id
                 FROM inserted_records ir
                          JOIN records r ON ir.byte_data = r.sha256;
END;
$$ LANGUAGE plpgsql;