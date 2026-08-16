CREATE TABLE tag_displays
(
    tag_id       INTEGER PRIMARY KEY REFERENCES tags (tag_id),
    display_text TEXT NOT NULL
);
