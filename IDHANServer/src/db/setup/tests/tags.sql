-- Creates following tag aliases:
-- 'character:toujou koneko' -> 'character:shirone'
-- 'highschool_dxd' -> 'highschool dxd' -> 'series:highschool dxd'
-- Forms chain: highschool_dxd -> highschool dxd -> series:highschool dxd
INSERT INTO tag_aliases(aliased_id, alias_id, domain_id)
VALUES (createtag('character', 'toujou koneko'), createtag('character', 'shirone'), 1),
       (createtag('', 'highschool_dxd'), createtag('', 'highschool dxd'), 1),
       (createtag('', 'highschool dxd'), createtag('series', 'highschool dxd'), 1)
ON CONFLICT DO NOTHING;

SELECT *
FROM tags_combined;

SELECT tag_text(aliased_id), tag_text(alias_id), *
FROM tag_aliases;

-- Create a parent that is associated with `character:toujou koneko`
INSERT INTO tag_parents (parent_id, child_id, domain_id)
VALUES (createtag('', 'highschool_dxd'), createtag('character', 'toujou koneko'), 1);

-- child `character:toujou koneko` should actually be `character:shrione` as it's ideal tag
SELECT tag_text(parent_id) AS parent, tag_text(child_id) AS child, *
FROM aliased_parents;

DELETE
FROM tag_aliases
WHERE alias_id = 5 -- 'series:highschool dxd'
  AND aliased_id = 4; -- 'highschool dxd'

SELECT tag_text(aliased_id), tag_text(alias_id), *
FROM flattened_aliases;

-- parent that was previously `series:highschool dxd` should appear as `highschool dxd` now
SELECT tag_text(parent_id) AS parent, tag_text(child_id), *
FROM aliased_parents;

INSERT INTO tag_aliases (aliased_id, alias_id, domain_id)
VALUES (4, 5, 1);

SELECT tag_text(aliased_id), tag_text(alias_id), *
FROM flattened_aliases;

SELECT tag_text(parent_id) AS parent, tag_text(child_id), *
FROM aliased_parents;

DELETE
FROM tag_aliases
WHERE alias_id = 4
  AND aliased_id = 3;

SELECT tag_text(parent_id) AS parent, tag_text(child_id), *
FROM aliased_parents;

INSERT INTO tag_aliases (aliased_id, alias_id, domain_id)
VALUES (3, 4, 1);

SELECT tag_text(aliased_id) AS aliased, tag_text(alias_id) AS alias, *
FROM flattened_aliases;

SELECT tag_text(parent_id) AS parent, tag_text(child_id), *
FROM aliased_parents;