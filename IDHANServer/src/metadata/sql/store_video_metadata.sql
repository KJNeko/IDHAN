-- $1: RecordID; $2: width; $3: height; $4: bitrate; $5: duration; $6: frame rate; $7: has audio.
INSERT INTO video_metadata (record_id, width, height, bitrate, duration, framerate, has_audio)
VALUES ($1, $2, $3, $4, $5, $6, $7)
ON CONFLICT (record_id) DO UPDATE SET width     = $2,
                                      height    = $3,
                                      bitrate   = $4,
                                      duration  = $5,
                                      framerate = $6,
                                      has_audio = $7;
