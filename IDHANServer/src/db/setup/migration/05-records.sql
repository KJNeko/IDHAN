CREATE SEQUENCE records_record_id_seq;

CREATE TABLE records
(
    record_id recordid PRIMARY KEY DEFAULT NEXTVAL('records_record_id_seq'),
    sha256    bytea UNIQUE NOT NULL,
    CHECK ( LENGTH(sha256) = 32 )
);