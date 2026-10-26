-- $1: RecordID; $2: duration; $3: bitrate; $4: channels; $5: sample rate.
INSERT INTO audio_metadata (record_id, duration, bitrate, channels, sample_rate)
VALUES ($1, $2, $3, $4, $5)
ON CONFLICT (record_id) DO UPDATE SET duration    = $2,
                                      bitrate     = $3,
                                      channels    = $4,
                                      sample_rate = $5;
