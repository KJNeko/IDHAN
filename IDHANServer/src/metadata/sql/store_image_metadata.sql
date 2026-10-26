-- $1: RecordID; $2: width; $3: height; $4: channels; $5: optional perceptual hash.
-- $6..$10: EXIF, GPS, XMP, IPTC, and ICC-profile flags.
-- $11: lossless; $12: quality; $13: subsampling; $14: progressive; $15: comment.
INSERT INTO image_metadata (record_id, width, height, channels, phash,
                            has_exif, has_gps, has_xmp, has_iptc, has_icc_profile,
                            lossless, quality, subsampling, progressive, comment)
VALUES ($1, $2, $3, $4, $5::bit(64), $6, $7, $8, $9, $10, $11, $12, $13, $14, $15)
ON CONFLICT (record_id) DO UPDATE SET width           = $2,
                                      height          = $3,
                                      channels        = $4,
                                      phash           = $5::bit(64),
                                      has_exif        = $6,
                                      has_gps         = $7,
                                      has_xmp         = $8,
                                      has_iptc        = $9,
                                      has_icc_profile = $10,
                                      lossless        = $11,
                                      quality         = $12,
                                      subsampling     = $13,
                                      progressive     = $14,
                                      comment         = $15;
