CREATE INDEX image_metadata_phash_idx ON image_metadata (phash) WHERE phash IS NOT NULL;
