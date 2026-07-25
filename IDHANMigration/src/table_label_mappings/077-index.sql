-- FK columns on label_mappings
CREATE INDEX idx_label_mappings_note_id ON label_mappings (note_id);
CREATE INDEX idx_label_mappings_label_id ON label_mappings (label_id);
