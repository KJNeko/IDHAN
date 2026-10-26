ALTER TABLE image_metadata
    ADD COLUMN lossless    BOOLEAN  NULL,
    ADD COLUMN quality     SMALLINT NULL,
    ADD COLUMN subsampling SMALLINT NULL,
    ADD COLUMN progressive BOOLEAN  NULL;
