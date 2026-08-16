CREATE TABLE webui_layouts
(
    layout_id  UUID PRIMARY KEY,
    name       TEXT        NOT NULL,
    document   JSONB       NOT NULL,
    schema_ver INT         NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX webui_layouts_name_lower_idx ON webui_layouts ( lower(name) );
