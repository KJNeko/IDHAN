-- $1: RecordID; $2: width; $3: height; $4: channels; $5: layers.
INSERT INTO image_project_metadata (record_id, width, height, channels, layers)
VALUES ($1, $2, $3, $4, $5)
ON CONFLICT (record_id) DO UPDATE SET width    = $2,
                                      height   = $3,
                                      channels = $4,
                                      layers   = $5;
