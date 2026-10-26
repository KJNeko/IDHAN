-- $1: modification time in epoch microseconds; $2: optional birth time in epoch microseconds; $3: RecordID.
UPDATE file_info
SET file_mtime = TIMESTAMP 'epoch' + $1::bigint * INTERVAL '1 microsecond',
    file_ctime = TIMESTAMP 'epoch' + $2::bigint * INTERVAL '1 microsecond'
WHERE record_id = $3;
