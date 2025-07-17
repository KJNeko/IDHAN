SELECT tag_text(tag_id), *
FROM tag_mappings;

INSERT INTO tag_aliases
    (aliased_id, alias_id, domain_id)
VALUES (createtag('r-18'), createtag('rating:explicit'), 2);

SELECT tag_text(aliased_id), tag_text(alias_id), *
FROM flattened_aliases;

SELECT tag_text(tag_id), *
FROM tag_mappings
WHERE ideal_tag_id IS NOT NULL;
