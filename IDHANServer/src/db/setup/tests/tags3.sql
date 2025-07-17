INSERT INTO tag_siblings (older_id, younger_id, domain_id)
VALUES (createtag('rating:e'), createtag('rating:questionable'), 1);

SELECT tag_text(older_id), tag_text(younger_id), *
FROM aliased_siblings;

INSERT INTO tag_aliases (aliased_id, alias_id, domain_id)
VALUES (createtag('explicit'), createtag('rating:explicit'), 1),
       (createtag('rating:e'), createtag('explicit'), 1);

SELECT tag_text(older_id), tag_text(younger_id), *
FROM aliased_siblings;

DELETE
FROM tag_aliases
WHERE alias_id = createtag('rating:explicit');

SELECT tag_text(older_id), tag_text(younger_id), *
FROM aliased_siblings;

INSERT INTO tag_aliases (aliased_id, alias_id, domain_id)
VALUES (createtag('explicit'), createtag('rating:explicit'), 1);

SELECT tag_text(older_id), tag_text(younger_id), *
FROM aliased_siblings;


SELECT tag_text(aliased_id) AS aliased, tag_text(alias_id) AS alias
FROM flattened_aliases;

DELETE
FROM tag_aliases
WHERE alias_id = createtag('explicit');

SELECT tag_text(aliased_id) AS aliased, tag_text(alias_id) AS alias
FROM flattened_aliases;

SELECT tag_text(older_id), tag_text(younger_id), *
FROM aliased_siblings;