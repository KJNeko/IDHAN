INSERT INTO hamming_distance_queue (record_id)
SELECT record_id
FROM image_metadata
WHERE phash IS NOT NULL;