-- Trigram index on label text for fast label autocomplete / filtering
CREATE INDEX idx_note_labels_label_trgm ON note_labels USING GIN (label gin_trgm_ops);
