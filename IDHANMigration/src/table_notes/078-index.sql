-- Trigram index on note text for fast ILIKE / similarity searches
CREATE INDEX idx_notes_note_trgm ON notes USING GIN (note gin_trgm_ops);
