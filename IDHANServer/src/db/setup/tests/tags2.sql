INSERT INTO tag_aliases(aliased_id, alias_id, domain_id)
VALUES (createtag('', 'one'), createtag('', 'two'), 1),
       (createtag('', 'three'), createtag('', 'four'), 1),
       (createtag('', 'five'), createtag('', 'six'), 1);

SELECT aliased_id, alias_id, tag_text(aliased_id) AS aliased, tag_text(alias_id) AS alias
FROM flattened_aliases
ORDER BY aliased_id, alias_id;

INSERT INTO tag_aliases(aliased_id, alias_id, domain_id)
VALUES (createtag('', 'two'), createtag('', 'three'), 1);

SELECT aliased_id, alias_id, tag_text(aliased_id) AS aliased, tag_text(alias_id) AS alias
FROM flattened_aliases
ORDER BY aliased_id, alias_id;

INSERT INTO tag_aliases(aliased_id, alias_id, domain_id)
VALUES (createtag('', 'four'), createtag('', 'five'), 1);


SELECT aliased_id, alias_id, tag_text(aliased_id) AS aliased, tag_text(alias_id) AS alias
FROM flattened_aliases
ORDER BY aliased_id, alias_id;

DELETE
FROM tag_aliases
WHERE aliased_id = 3
  AND alias_id = 4;

SELECT aliased_id, alias_id, tag_text(aliased_id) AS aliased, tag_text(alias_id) AS alias
FROM flattened_aliases
ORDER BY aliased_id, alias_id;

INSERT INTO tag_parents (parent_id, child_id, domain_id)
VALUES (createtag('parent', 'four'), createtag('', 'four'), 1);

SELECT parent_id, child_id, tag_text(parent_id) AS parent, tag_text(child_id) AS child
FROM aliased_parents
ORDER BY parent_id, child_id;

DELETE
FROM tag_aliases
WHERE aliased_id = 5;

SELECT parent_id, child_id, tag_text(parent_id) AS parent, tag_text(child_id) AS child
FROM aliased_parents
ORDER BY parent_id, child_id;