-- FK columns on record_notes (PG does not auto-index FK columns)
CREATE INDEX idx_record_notes_record_id ON record_notes (record_id);
CREATE INDEX idx_record_notes_note_id ON record_notes (note_id);
