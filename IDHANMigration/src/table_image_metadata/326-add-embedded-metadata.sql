ALTER TABLE image_metadata
    ADD COLUMN has_exif        BOOLEAN NULL,
    ADD COLUMN has_gps         BOOLEAN NULL,
    ADD COLUMN has_xmp         BOOLEAN NULL,
    ADD COLUMN has_iptc        BOOLEAN NULL,
    ADD COLUMN has_icc_profile BOOLEAN NULL;
