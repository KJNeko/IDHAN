-- Average hashes cannot be compared with the new DCT shape hashes. Metadata regeneration will
-- populate phash again and its existing trigger will queue fresh distance calculations.
UPDATE image_metadata SET phash = NULL;
-- Clearing a hash also queues it; discard those entries until a new hash is available.
TRUNCATE hamming_distance, hamming_distance_queue;
