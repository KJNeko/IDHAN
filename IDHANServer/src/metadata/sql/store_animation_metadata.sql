-- $1: RecordID; $2: width; $3: height; $4: frame count; $5: duration; $6: loops.
INSERT INTO animation_metadata (record_id, width, height, frame_count, duration, loops)
VALUES ($1, $2, $3, $4, $5, $6)
ON CONFLICT (record_id) DO UPDATE SET width       = $2,
                                      height      = $3,
                                      frame_count = $4,
                                      duration    = $5,
                                      loops       = $6;
