CREATE EXTENSION IF NOT EXISTS pg_trgm;

-- FK columns on record_notes (PG does not auto-index FK columns)
CREATE INDEX idx_record_notes_record_id ON record_notes (record_id);
CREATE INDEX idx_record_notes_note_id   ON record_notes (note_id);

-- FK columns on label_mappings
CREATE INDEX idx_label_mappings_note_id  ON label_mappings (note_id);
CREATE INDEX idx_label_mappings_label_id ON label_mappings (label_id);

-- Trigram index on note text for fast ILIKE / similarity searches
CREATE INDEX idx_notes_note_trgm ON notes USING GIN (note gin_trgm_ops);

-- Trigram index on label text for fast label autocomplete / filtering
CREATE INDEX idx_note_labels_label_trgm ON note_labels USING GIN (label gin_trgm_ops);
