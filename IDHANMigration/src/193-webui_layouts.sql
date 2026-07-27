-- WebUI named layouts pushed to the server.
--
-- Layouts live primarily in the browser's localStorage; this table is the optional server-side copy a
-- user pushes so a layout can be pulled onto another browser. There is no user system and no ownership:
-- a layout is identified solely by its client-generated uuid.
--
-- layout_id is client-generated (not a server SERIAL) because the client owns identity — localStorage
-- is the source of truth, so a push is an upsert to a known id and "same layout, two browsers" stays
-- coherent without any reconciliation.
CREATE TABLE webui_layouts
(
    layout_id  UUID PRIMARY KEY,
    name       TEXT        NOT NULL,
    -- The whole LayoutDocument envelope (schema, id, name, engine tree, per-panel config). JSONB rather
    -- than TEXT for free validation on write and so we can later query document->'panels' (e.g. "which
    -- layouts use this plugin?").
    document   JSONB       NOT NULL,
    schema_ver INT         NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Case-insensitive unique name so pushing a second layout that collides on name gives a clean 409
-- instead of silently forking into two indistinguishable entries.
CREATE UNIQUE INDEX webui_layouts_name_lower_idx ON webui_layouts ( lower(name) );
