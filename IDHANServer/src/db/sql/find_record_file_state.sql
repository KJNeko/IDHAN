-- $1: SHA-256. Column order matches RecordFileState decoding in fileInfo.cpp.
SELECT r.record_id,
       fi.cluster_id,
       fi.mime_id,
       fi.extension,
       m.best_extension,
       fi.size,
       (extract(epoch FROM fi.file_mtime) * 1000000)::bigint,
       (extract(epoch FROM fi.file_ctime) * 1000000)::bigint,
       fi.cluster_store_time IS NOT NULL,
       EXISTS (SELECT 1 FROM metadata md WHERE md.record_id = r.record_id)
FROM records r
         LEFT JOIN file_info fi USING (record_id)
         LEFT JOIN mime m USING (mime_id)
WHERE r.sha256 = $1;
